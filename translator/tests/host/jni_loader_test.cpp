#include <cstdlib>
#include <filesystem>
#include <string>

#include "check.h"
#include "mock_jvm.h"
#include "zb/host_jni.h"
#include "zb/jni_loader.h"
#include "zb/library_protocol.h"
#include "zb/library_runtime.h"
#include "zb/runtime_report.h"

namespace {

using zb::mock::MockJvm;
using NoArgsFn = std::int32_t (*)(std::uint64_t, std::uint64_t);
using IntFn = std::int32_t (*)(std::uint64_t, std::uint64_t, std::int32_t);
using ObjectFn = std::int32_t (*)(std::uint64_t, std::uint64_t, std::uint64_t);

std::string library(const char* directory, const char* name) {
    return (std::filesystem::path(directory) / name).string();
}

}  // namespace

int main(int argc, char** argv) {
    CHECK(argc == 4);
    auto* vm = new MockJvm();
    vm->define_class("zb/Load");
    vm->add_native("zb/Load", "shortExport", "()I", true);
    vm->add_native("zb/Load", "shortExport", "(I)I", true);
    vm->add_native("zb/Load", "over", "(I)I", true);
    vm->add_native("zb/Load", "over", "(Ljava/lang/String;)I", false);
    // libzbloadskip.so: one export names a type the plugin cannot resolve, one declares its natives
    // in a class whose enumeration throws, one is ordinary and must still bind.
    vm->define_class("zb/Skip");
    vm->add_native("zb/Skip", "fine", "()I", true);
    vm->add_native("zb/Skip", "ads", "(Lcom/google/ads/Ad;)I", true);
    vm->define_class("zb/Broken");
    vm->add_native("zb/Broken", "enumerate", "()I", true);
    vm->fail_type_resolution("Lcom/google/ads/Ad;");
    vm->fail_declared_enumeration("zb/Broken", zb::NativeLookupStatus::Unresolvable);

    auto* runtime = new zb::LibraryRuntime();
    auto* host_jni = new zb::HostJni(*runtime, *vm, 64);
    runtime->set_host_call_handler(
        [host_jni](std::uint32_t index, zb::GuestThread& thread) {
            return host_jni->handle_host_call(index, thread);
        });
    zb::LibraryRuntimeOptions options;
    options.sysroot = argv[1];
    options.zbhost = argv[2];
    options.target_sdk = 16;
    options.guest_environment = {"LD_LIBRARY_PATH=" + std::string(argv[3])};
    options.preload = "libzbjni.so";
    std::string error;
    CHECK(runtime->start(options, error));

    const auto env = vm->thread_env();
    MockJvm::NativeFrame frame(*vm, env);
    CHECK(host_jni->load_library_on_current(
              env, library(argv[3], "lib-does-not-exist.so"), ZB_GUEST_RTLD_NOW, error) == 0);
    CHECK(!error.empty());
    zb::JniLoader loader(*host_jni, *vm);
    zb::RuntimeReport& report = zb::runtime_report();
    report.clear();
    const auto loaded = loader.load(env, library(argv[3], "libzbloadprobe.so"), ZB_GUEST_RTLD_NOW);
    CHECK(loaded.ok);
    // Every bound native and every guest JNI_OnLoad reaches the runtime report.
    CHECK(report.registered_natives() == loaded.bound_methods);
    CHECK(report.jni_onload_calls() == 1);
    CHECK(loaded.bound_methods == 4 && loaded.skipped_classes == 1);
    CHECK(loaded.jni_version == 0x00010006);
    CHECK(vm->native_function("zb/Load", "shortExport", "()I") != nullptr);
    CHECK(vm->native_function("zb/Load", "shortExport", "(I)I") != nullptr);
    CHECK(vm->native_function("zb/Load", "over", "(I)I") != nullptr);
    CHECK(vm->native_function("zb/Load", "over", "(Ljava/lang/String;)I") != nullptr);
    CHECK(!vm->exception_check(env));
    {
        MockJvm::NativeFrame calls(*vm, env);
        const auto cls = calls.local(vm->class_object("zb/Load"));
        const auto self = calls.local(vm->new_object("zb/Load"));
        const auto text = calls.local(vm->new_string_object(u"text"));
        const auto no_args = reinterpret_cast<NoArgsFn>(
            vm->native_function("zb/Load", "shortExport", "()I"));
        const auto ignored_arg = reinterpret_cast<IntFn>(
            vm->native_function("zb/Load", "shortExport", "(I)I"));
        const auto int_overload = reinterpret_cast<IntFn>(
            vm->native_function("zb/Load", "over", "(I)I"));
        const auto object_overload = reinterpret_cast<ObjectFn>(
            vm->native_function("zb/Load", "over", "(Ljava/lang/String;)I"));
        CHECK(no_args(env, cls) == 17);
        CHECK(ignored_arg(env, cls, 99) == 17);
        CHECK(int_overload(env, cls, 41) == 42);
        CHECK(object_overload(env, self, text) == 23);
        CHECK(calls.close() == 3);
    }

    const auto no_onload = loader.load(env, library(argv[3], "libzbloadnoonload.so"), ZB_GUEST_RTLD_NOW);
    CHECK(no_onload.ok && no_onload.jni_version == 0x00010006);
    // A library without JNI_OnLoad is not counted as one that ran.
    CHECK(report.jni_onload_calls() == 1);

    const auto bad_version = loader.load(env, library(argv[3], "libzbloadbad.so"), ZB_GUEST_RTLD_NOW);
    CHECK(!bad_version.ok && bad_version.error.find("unsupported JNI version") != std::string::npos);
    CHECK(report.jni_onload_calls() == 2);

    const auto unknown = loader.load(env, library(argv[3], "libzbloadunknown.so"), ZB_GUEST_RTLD_NOW);
    // An export with no matching declared native is skipped, as ART ignores unused exports.
    CHECK(unknown.ok && unknown.skipped_exports == 1);

    vm->fail_native_registration("()I");
    const auto rejected = loader.load(env, library(argv[3], "libzbloadprobe.so"), ZB_GUEST_RTLD_NOW);
    CHECK(!rejected.ok && rejected.error.find("RegisterNatives") != std::string::npos);
    CHECK(vm->exception_check(env));
    vm->exception_clear(env);

    // A lookup that cannot be resolved skips only its own export; the library still loads.
    const auto skipped = loader.load(env, library(argv[3], "libzbloadskip.so"), ZB_GUEST_RTLD_NOW);
    CHECK(skipped.ok);
    CHECK(skipped.bound_methods == 1 && skipped.skipped_exports == 2 && skipped.skipped_classes == 0);
    CHECK(vm->native_function("zb/Skip", "fine", "()I") != nullptr);
    CHECK(vm->native_function("zb/Skip", "ads", "(Lcom/google/ads/Ad;)I") == nullptr);
    CHECK(vm->native_function("zb/Broken", "enumerate", "()I") == nullptr);
    CHECK(!vm->exception_check(env));

    // A genuine lookup error still fails the whole library.
    vm->fail_declared_enumeration("zb/Broken", zb::NativeLookupStatus::Error);
    const auto broken = loader.load(env, library(argv[3], "libzbloadskip.so"), ZB_GUEST_RTLD_NOW);
    CHECK(!broken.ok && broken.error.find("failed to inspect declared natives") != std::string::npos);

    const std::string report_text = report.text();
    CHECK(report_text.find("jni-onload: libzbloadprobe.so ok jni=0x00010006") != std::string::npos);
    CHECK(report_text.find("jni-onload: libzbloadbad.so failed") != std::string::npos);
    CHECK(report_text.find("registered-natives: " + std::to_string(report.registered_natives())) !=
          std::string::npos);

    CHECK(frame.close() == 0);
    CHECK(vm->errors().empty());
    std::puts("jni_loader_test PASS");
    std::fflush(stdout);
    std::_Exit(0);
}
