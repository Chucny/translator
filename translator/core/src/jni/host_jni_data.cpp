// JNI host calls: strings, arrays, direct buffers.
#include "host_jni_internal.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>

#include "zb/log.h"
#include "zb/runtime_report.h"

namespace zb {

namespace {

// Bytes of one element of a primitive type letter; 0 for anything else.
std::size_t element_size(char type) {
    switch (type) {
    case 'Z':
    case 'B':
        return 1;
    case 'C':
    case 'S':
        return 2;
    case 'I':
    case 'F':
        return 4;
    case 'J':
    case 'D':
        return 8;
    default:
        return 0;
    }
}

}  // namespace

std::uint32_t HostJni::Impl::mirror_direct_buffer(JniBackend::Env env, JniBackend::Ref buffer,
                                                  const void* host, std::int64_t capacity,
                                                  const char*& failure) {
    if (host == nullptr || capacity <= 0) {
        failure = "empty";
        return 0;
    }
    if (static_cast<std::uint64_t>(capacity) > kMirrorCapBytes) {
        failure = "over-cap";
        return 0;
    }
    const auto size = static_cast<std::uint64_t>(capacity);
    std::uint32_t guest = 0;
    {
        std::lock_guard<std::mutex> lock(mirror_mutex);
        auto found = mirrors.find(host);
        if (found != mirrors.end()) guest = found->second.guest;
        if (guest == 0 && mirrored_bytes + size > kMirrorCapBytes) {
            ++mirror_failures;
            failure = "over-cap";
            return 0;
        }
    }
    if (guest == 0) {
        // Guest malloc runs guest code on this thread, so it must never run under mirror_mutex:
        // another thread holding the guest allocator's own lock and waiting here would deadlock.
        runtime_report().note_jni_detail("direct-buffer-allocating", "size=" + std::to_string(size), true);
        GuestCall args;
        args.regs = {static_cast<std::uint32_t>(size), 0, 0, 0};
        const auto allocated = runtime.call_on_current(runtime.service_api().malloc_fn, args);
        if (!allocated || allocated->r0 == 0) {
            std::lock_guard<std::mutex> lock(mirror_mutex);
            ++mirror_failures;
            failure = "no-guest-memory";
            return 0;
        }
        guest = allocated->r0;
    }
    std::lock_guard<std::mutex> lock(mirror_mutex);
    auto found = mirrors.find(host);
    if (found == mirrors.end()) {
        // A concurrent call may have mirrored the same buffer first; then its allocation wins and
        // ours is simply left unused (the guest heap keeps it, which is bounded by the cap).
        found = mirrors.emplace(host, BufferMirror{guest, size, backend.new_global_ref(env, buffer)}).first;
        mirrored_bytes += size;
    }
    // Java may have written into the buffer since the last call, so refresh the copy. A guest
    // write made before this point is overwritten; that is the documented cost of the mirror.
    const std::uint64_t bytes = std::min(size, found->second.size);
    std::memcpy(runtime.memory().base() + found->second.guest, host, bytes);
    return found->second.guest;
}

void HostJni::Impl::flush_buffer_mirrors(JniBackend::Env env) {
    std::lock_guard<std::mutex> lock(mirror_mutex);
    const std::uint8_t* base = runtime.memory().base();
    for (auto it = mirrors.begin(); it != mirrors.end();) {
        const BufferMirror& mirror = it->second;
        // Ask Java again rather than trusting the address we stored: a buffer whose memory moved,
        // shrank or went away must never be written through a stale pointer.
        const void* host = mirror.global != 0 ? backend.get_direct_buffer_address(env, mirror.global) : nullptr;
        const std::int64_t capacity =
            host != nullptr ? backend.get_direct_buffer_capacity(env, mirror.global) : 0;
        if (host != it->first || capacity < 0 ||
            static_cast<std::uint64_t>(capacity) < mirror.size) {
            log("direct buffer mirror dropped: the Java buffer changed or went away");
            if (mirror.global != 0) backend.delete_global_ref(env, mirror.global);
            mirrored_bytes -= mirror.size;
            it = mirrors.erase(it);
            continue;
        }
        std::memcpy(const_cast<void*>(host), base + mirror.guest, mirror.size);
        ++it;
    }
}

bool HostJni::Impl::serve_data(JniCall& call) {
    JniThread& state = call.state();
    const char* name = jni_host_call_name(call.index());
    const auto ref = [&](unsigned position) { return resolve(state, call.arg(position), name); };
    const auto sint = [&](unsigned position) { return static_cast<std::int32_t>(call.arg(position)); };
    switch (call.index()) {
    case ZB_JNI_HC_NewString: {
        const JniBackend::Env env = call.env();
        const std::int32_t length = sint(1);
        const void* chars = length > 0 ? readable(env, call.arg(0), 2 * static_cast<std::uint64_t>(length), name) : nullptr;
        call.set(local(state, backend.new_string(env, static_cast<const std::uint16_t*>(chars), length)));
        return true;
    }
    case ZB_JNI_HC_NewStringUTF: {
        const JniBackend::Env env = call.env();
        if (call.arg(0) == 0) return true;  // ART returns null for a null string
        const std::string utf = read_string(env, call.arg(0), name);
        call.set(local(state, backend.new_string_utf(env, utf.c_str())));
        return true;
    }
    case ZB_JNI_HC_GetStringLength:
        call.set(static_cast<std::uint32_t>(backend.get_string_length(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_GetStringUTFLength:
        call.set(static_cast<std::uint32_t>(backend.get_string_utf_length(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_GetStringRegion: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref str = ref(0);
        const std::int32_t length = sint(2);
        void* out = length > 0 ? writable(env, call.arg(3), 2 * static_cast<std::uint64_t>(length), name) : nullptr;
        backend.get_string_region(env, str, sint(1), length, out);
        return true;
    }
    case ZB_JNI_HC_GetStringUTFRegion: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref str = ref(0);
        std::string utf;
        if (!backend.get_string_utf_region(env, str, sint(1), sint(2), utf)) return true;
        if (call.arg(3) == 0 && utf.empty()) return true;
        // ART writes the modified UTF-8 bytes and a NUL.
        std::memcpy(writable(env, call.arg(3), utf.size() + 1, name), utf.c_str(), utf.size() + 1);
        return true;
    }
    case ZB_JNI_HC_GetArrayLength:
        call.set(static_cast<std::uint32_t>(backend.get_array_length(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_GetArrayElementType:
        call.set(static_cast<std::uint8_t>(backend.get_array_element_type(call.env(), ref(0))));
        return true;
    case ZB_JNI_HC_NewObjectArray:
        call.set(local(state, backend.new_object_array(call.env(), sint(0), ref(1), ref(2))));
        return true;
    case ZB_JNI_HC_GetObjectArrayElement:
        call.set(local(state, backend.get_object_array_element(call.env(), ref(0), sint(1))));
        return true;
    case ZB_JNI_HC_SetObjectArrayElement:
        backend.set_object_array_element(call.env(), ref(0), sint(1), ref(2));
        return true;
    case ZB_JNI_HC_NewPrimitiveArray: {
        const JniBackend::Env env = call.env();
        const char type = static_cast<char>(call.arg(0));
        if (element_size(type) == 0) fatal(env, "NewPrimitiveArray: bad type 0x%x", call.arg(0));
        call.set(local(state, backend.new_primitive_array(env, type, sint(1))));
        return true;
    }
    case ZB_JNI_HC_GetPrimitiveArrayRegion:
    case ZB_JNI_HC_SetPrimitiveArrayRegion: {
        const JniBackend::Env env = call.env();
        const char type = static_cast<char>(call.arg(0));
        const std::size_t size = element_size(type);
        if (size == 0) fatal(env, "%s: bad type 0x%x", name, call.arg(0));
        const JniBackend::Ref array = ref(1);
        const std::int32_t length = sint(3);
        const std::uint64_t bytes = length > 0 ? static_cast<std::uint64_t>(length) * size : 0;
        if (call.index() == ZB_JNI_HC_GetPrimitiveArrayRegion) {
            void* out = bytes != 0 ? writable(env, call.arg(4), bytes, name) : nullptr;
            backend.get_primitive_array_region(env, type, array, sint(2), length, out);
        } else {
            const void* in = bytes != 0 ? readable(env, call.arg(4), bytes, name) : nullptr;
            backend.set_primitive_array_region(env, type, array, sint(2), length, in);
        }
        return true;
    }
    case ZB_JNI_HC_NewDirectByteBuffer: {
        const JniBackend::Env env = call.env();
        const std::uint32_t address = call.arg(0);
        const auto capacity = static_cast<std::int64_t>(call.arg(1) | (static_cast<std::uint64_t>(call.arg(2)) << 32));
        if (capacity < 0 || capacity > std::numeric_limits<std::int32_t>::max()) {
            fatal(env, "NewDirectByteBuffer: capacity %lld is outside the 32-bit guest range",
                  static_cast<long long>(capacity));
        }
        if (capacity > 0 && static_cast<std::uint64_t>(address) + static_cast<std::uint64_t>(capacity) > kGuestSpaceSize) {
            fatal(env, "NewDirectByteBuffer: 0x%08x + %lld leaves the guest address space", address,
                  static_cast<long long>(capacity));
        }
        void* host = address != 0 ? runtime.memory().base() + address : nullptr;
        call.set(local(state, backend.new_direct_byte_buffer(env, host, capacity)));
        return true;
    }
    case ZB_JNI_HC_GetDirectBufferAddress: {
        const JniBackend::Env env = call.env();
        const JniBackend::Ref buffer = ref(0);
        const auto* host = static_cast<const std::uint8_t*>(backend.get_direct_buffer_address(env, buffer));
        const std::uint8_t* base = runtime.memory().base();
        const bool inside = host != nullptr && host >= base &&
                            static_cast<std::uint64_t>(host - base) < kGuestSpaceSize;
        std::uint32_t answer = 0;
        const char* failure = "";
        bool mirrored = false;
        if (inside) {
            // A buffer the guest itself made with NewDirectByteBuffer: hand over its own address.
            answer = static_cast<std::uint32_t>(host - base);
        } else if (host != nullptr) {
            // A direct buffer Java allocated lives outside the guest's 4 GiB space, so its host
            // address cannot be handed over. Mirror it into guest memory instead; the guest reads
            // a real copy rather than NULL (Flutter copies from this pointer at once).
            // Recorded before mirroring: if the guest allocation below never returns, the report
            // still shows how far this call got.
            runtime_report().note_jni_detail("direct-buffer-last-request",
                                             "host=" + std::to_string(reinterpret_cast<std::uintptr_t>(host)), true);
            answer = mirror_direct_buffer(env, buffer, host,
                                          backend.get_direct_buffer_capacity(env, buffer), failure);
            mirrored = answer != 0;
        }
        call.set(answer);
        // Record every answer: a guest that trusts this pointer crashes far away from here.
        static std::atomic<unsigned> answered{0};
        const unsigned seen = answered.fetch_add(1) + 1;
        if (seen <= 4) {
            char text[200];
            std::snprintf(text, sizeof text, "buffer=0x%08x host=%p %s guest=0x%08x mirrored=%s%s",
                          call.arg(1), static_cast<const void*>(host), inside ? "inside" : "outside",
                          answer, mirrored ? "yes" : "no", mirrored || inside ? "" : failure);
            runtime_report().note_jni_detail("direct-buffer-" + std::to_string(seen), text, false);
        }
        runtime_report().note_jni_detail("direct-buffer-calls", std::to_string(seen), true);
        {
            std::lock_guard<std::mutex> lock(mirror_mutex);
            runtime_report().note_jni_detail("direct-buffer-mirrored-bytes", std::to_string(mirrored_bytes), true);
            if (mirror_failures != 0) {
                runtime_report().note_jni_detail("direct-buffer-mirror-failures",
                                                 std::to_string(mirror_failures), true);
            }
        }
        if (!inside && host != nullptr && !mirrored && !logged_foreign_buffer.exchange(true)) {
            log("GetDirectBufferAddress: the buffer lies outside guest memory and could not be "
                "mirrored (%s); returning NULL (logged once)", failure);
        }
        return true;
    }
    case ZB_JNI_HC_GetDirectBufferCapacity:
        call.set64(static_cast<std::uint64_t>(backend.get_direct_buffer_capacity(call.env(), ref(0))));
        return true;
    default:
        return false;
    }
}

}  // namespace zb
