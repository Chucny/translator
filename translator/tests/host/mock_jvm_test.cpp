// The mock JVM behind the JNI host tests: model setup, calls with virtual dispatch, fields,
// strings in modified UTF-8, arrays, frames, references, exceptions, monitors, natives, direct
// buffers, reflection, threads, and discipline errors.
#include <cstring>
#include <string>
#include <thread>

#include "check.h"
#include "mock_jvm.h"

using zb::JniCallKind;
using zb::JValue;
using zb::mock::MockCall;
using zb::mock::MockJvm;

namespace {

JValue int_value(std::int32_t value) {
    JValue v{};
    v.i = value;
    return v;
}

}  // namespace

int main() {
    MockJvm vm;
    vm.define_class("zb/Base");
    vm.define_class("zb/Child", "zb/Base");
    vm.add_method("zb/Base", "value", "(I)I", false, [](MockCall& call) { return int_value(call.args[0].i + 1); });
    vm.add_method("zb/Child", "value", "(I)I", false, [](MockCall& call) { return int_value(call.args[0].i + 2); });
    vm.add_method("zb/Base", "<init>", "(Ljava/lang/String;)V", false, [](MockCall& call) {
        JValue text{};
        text.l = call.args[0].l;
        call.vm.set_field_value(call.self, "name", text);
        return JValue{};
    });
    vm.add_method("zb/Base", "fail", "()V", true, [](MockCall& call) {
        call.vm.throw_in(call.env, "java/lang/IllegalArgumentException", "boom");
        return JValue{};
    });
    vm.add_field("zb/Base", "name", "Ljava/lang/String;", false);
    vm.add_field("zb/Base", "count", "J", true);
    vm.add_field("zb/Base", "zeroStatic", "I", true);
    vm.add_field("zb/Base", "zeroInstance", "I", false);
    vm.add_native("zb/Base", "run", "(IFFIFF)I", true);

    const auto env = vm.thread_env();
    CHECK(env != 0 && vm.thread_env() == env && vm.attached_threads() == 1);
    MockJvm::NativeFrame frame(vm, env);

    // Classes, subtyping, virtual and nonvirtual dispatch.
    const auto base = vm.find_class(env, "zb/Base");
    const auto child = vm.find_class(env, "zb/Child");
    CHECK(base != 0 && child != 0 && vm.find_class(env, "zb/Missing") == 0);
    CHECK(vm.exception_check(env));
    vm.exception_clear(env);
    CHECK(vm.is_assignable_from(env, child, base) && !vm.is_assignable_from(env, base, child));
    CHECK(vm.is_same_object(env, vm.get_superclass(env, child), base));
    const auto value = vm.get_method_id(env, base, "value", "(I)I", false);
    CHECK(value != 0 && vm.get_method_id(env, base, "value", "(I)I", true) == 0);
    vm.exception_clear(env);
    const auto object = vm.alloc_object(env, child);
    JValue arg = int_value(40);
    CHECK(vm.call_method(env, JniCallKind::Virtual, 'I', object, 0, value, &arg).i == 42);
    CHECK(vm.call_method(env, JniCallKind::Nonvirtual, 'I', object, base, value, &arg).i == 41);
    CHECK(vm.is_instance_of(env, object, base) && vm.is_instance_of(env, 0, child));

    // Constructor, instance and static fields.
    const auto init = vm.get_method_id(env, base, "<init>", "(Ljava/lang/String;)V", false);
    JValue name{};
    name.l = vm.new_string_utf(env, "n\xc3\xa9");
    const auto made = vm.call_method(env, JniCallKind::NewObject, 'L', 0, base, init, &name).l;
    const auto name_field = vm.get_field_id(env, base, "name", "Ljava/lang/String;", false);
    const auto got = vm.get_field(env, false, 'L', made, name_field).l;
    CHECK(vm.get_string_length(env, got) == 2 && vm.get_string_utf_length(env, got) == 3);
    const auto count = vm.get_field_id(env, base, "count", "J", true);
    const auto zero_static = vm.get_field_id(env, base, "zeroStatic", "I", true);
    const auto zero_instance = vm.get_field_id(env, base, "zeroInstance", "I", false);
    CHECK(vm.get_field(env, true, 'I', base, zero_static).i == 0);
    CHECK(vm.get_field(env, false, 'I', made, zero_instance).i == 0);
    JValue wide{};
    wide.j = -7;
    vm.set_field(env, true, 'J', base, count, wide);
    CHECK(vm.get_field(env, true, 'J', base, count).j == -7);

    // Strings: surrogates and NUL in modified UTF-8.
    const std::uint16_t units[] = {'a', 0, 0xD83D, 0xDE00};
    const auto str = vm.new_string(env, units, 4);
    std::string utf;
    CHECK(vm.get_string_utf_region(env, str, 0, 4, utf) && utf == std::string("a\xc0\x80\xed\xa0\xbd\xed\xb8\x80", 9));
    CHECK(!vm.get_string_utf_region(env, str, 3, 2, utf));
    vm.exception_clear(env);
    std::uint16_t region[2] = {};
    vm.get_string_region(env, str, 2, 2, region);
    CHECK(region[0] == 0xD83D && region[1] == 0xDE00);
    CHECK(vm.string_value(vm.new_string_object(u"x")) == u"x");

    // Arrays.
    const auto ints = vm.new_primitive_array(env, 'I', 3);
    const std::int32_t in[3] = {1, -2, 3};
    vm.set_primitive_array_region(env, 'I', ints, 0, 3, in);
    std::int32_t out[2] = {};
    vm.get_primitive_array_region(env, 'I', ints, 1, 2, out);
    CHECK(out[0] == -2 && out[1] == 3 && vm.get_array_length(env, ints) == 3);
    CHECK(vm.get_array_element_type(env, ints) == 'I');
    vm.get_primitive_array_region(env, 'I', ints, 2, 2, out);
    CHECK(vm.exception_check(env));
    vm.exception_clear(env);
    const auto objects = vm.new_object_array(env, 2, base, made);
    CHECK(vm.get_array_element_type(env, objects) == 'L');
    CHECK(vm.is_same_object(env, vm.get_object_array_element(env, objects, 1), made));

    // References and frames.
    const auto global = vm.new_global_ref(env, made);
    const auto weak = vm.new_weak_global_ref(env, made);
    CHECK(vm.live_global_refs() == 1 && vm.live_weak_refs() == 1);
    CHECK(vm.push_local_frame(env, 4) == 0);
    const auto inner = vm.new_local_ref(env, global);
    const auto kept = vm.pop_local_frame(env, inner);
    CHECK(kept != 0 && vm.is_same_object(env, kept, made));
    CHECK(vm.errors().empty());
    vm.delete_local_ref(env, inner);
    CHECK(vm.errors().size() == 1);  // deleted when its frame was popped
    const std::string deleted = vm.errors()[0];
    CHECK(deleted.find("not a live local reference") != std::string::npos);
    vm.collect(vm.pending_exception(env));  // no-op for null
    const auto weak_object = vm.new_local_ref(env, weak);
    CHECK(vm.is_same_object(env, weak_object, made));
    vm.delete_global_ref(env, global);
    // Collecting the object clears weak references to it.
    const auto made_id = vm.field_value(vm.class_object("zb/Base"), "count").j == -7 ? 0 : 1;
    CHECK(made_id == 0);
    vm.delete_weak_global_ref(env, weak);
    CHECK(vm.live_global_refs() == 0 && vm.live_weak_refs() == 0);

    // Exceptions.
    const auto fail = vm.get_method_id(env, base, "fail", "()V", true);
    vm.call_method(env, JniCallKind::Static, 'V', 0, base, fail, nullptr);
    CHECK(vm.exception_check(env));
    const auto thrown = vm.exception_occurred(env);
    vm.exception_clear(env);
    CHECK(vm.is_instance_of(env, thrown, vm.find_class(env, "java/lang/RuntimeException")));
    CHECK(vm.throw_exception(env, thrown) == 0 && vm.exception_check(env));
    vm.call_method(env, JniCallKind::Static, 'V', 0, base, fail, nullptr);
    CHECK(vm.errors().size() == 2);  // call with a pending exception
    vm.exception_describe(env);
    CHECK(!vm.exception_check(env));

    // Reflection round trip.
    const auto reflected = vm.to_reflected_method(env, base, value, false);
    std::string signature;
    CHECK(vm.from_reflected_method(env, reflected, signature) == value && signature == "(I)I");
    const auto reflected_field = vm.to_reflected_field(env, base, count, true);
    CHECK(vm.from_reflected_field(env, reflected_field) == count);

    // Monitors are recursive and owned.
    CHECK(vm.monitor_enter(env, made) == 0 && vm.monitor_enter(env, made) == 0);
    CHECK(vm.monitor_exit(env, made) == 0 && vm.monitor_exit(env, made) == 0);

    // Native registration.
    int marker = 0;
    CHECK(vm.register_native(env, base, "run", "(IFFIFF)I", &marker) == 0);
    CHECK(vm.native_function("zb/Base", "run", "(IFFIFF)I") == &marker);
    CHECK(vm.register_native(env, base, "run", "(I)I", &marker) == -1);
    vm.exception_clear(env);
    CHECK(vm.unregister_natives(env, base) == 0 && vm.native_function("zb/Base", "run", "(IFFIFF)I") == nullptr);

    // Direct buffers.
    char storage[8];
    const auto buffer = vm.new_direct_byte_buffer(env, storage, 8);
    CHECK(vm.get_direct_buffer_address(env, buffer) == storage && vm.get_direct_buffer_capacity(env, buffer) == 8);
    CHECK(vm.get_direct_buffer_capacity(env, made) == -1 && vm.new_direct_byte_buffer(env, nullptr, 1) == 0);
    vm.exception_clear(env);

    // A foreign thread must attach, and cannot use this thread's env or locals.
    std::thread([&] {
        CHECK(vm.find_class(env, "zb/Base") == 0);
        const auto attached = vm.attach_current_thread(true, "worker", 0);
        CHECK(attached != 0 && attached != env && vm.thread_daemon(attached) && vm.thread_name(attached) == "worker");
        CHECK(vm.get_array_length(attached, ints) == 0);  // another thread's local reference
        CHECK(vm.detach_current_thread() == 0 && vm.detach_current_thread() == -1);
    }).join();
    CHECK(vm.errors().size() == 5);

    // A completed Java thread must not make a later thread with the same recycled std::thread::id
    // look attached under the old environment.
    std::thread::id retired_id;
    std::thread([&] {
        retired_id = std::this_thread::get_id();
        CHECK(vm.thread_env() != 0);
    }).join();
    bool reused_id = false;
    bool fresh_attachment = false;
    for (int attempt = 0; attempt < 64 && !reused_id; ++attempt) {
        std::thread([&] {
            if (std::this_thread::get_id() != retired_id) return;
            reused_id = true;
            const auto attached = vm.attach_current_thread(true, "reused-worker", 0);
            fresh_attachment = vm.thread_name(attached) == "reused-worker";
            CHECK(vm.detach_current_thread() == 0);
        }).join();
    }
    CHECK(reused_id && fresh_attachment);

    // Closing a native frame frees what it created and reports it.
    CHECK(frame.close() > 0 && vm.live_local_refs(env) == 0);
    MockJvm::NativeFrame call(vm, env);
    const auto argument = call.local(vm.new_object("zb/Base"));
    CHECK(call.result(vm.new_local_ref(env, argument)) != 0);
    const std::size_t before = vm.errors().size();
    vm.delete_local_ref(env, argument);  // argument references are not deletable, as in ART
    CHECK(vm.errors().size() == before + 1 && call.close() == 1);

    std::puts("mock_jvm_test PASS");
    return 0;
}
