#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "zb/jni_backend.h"

namespace zb {

// Portable seam behind AAssetManager*/AAsset* (Phase 5 Task 7). HostAssets marshals guest
// arguments and 32-bit handles; this interface deals only in opaque host handles (0 is invalid/
// null) and host JNI values. Implementations: the real Android NDK asset manager
// (core/android/asset_driver_backend.*) and an in-memory mock for host tests
// (tests/host/mock_assets.*).
class AssetBackend {
public:
    virtual ~AssetBackend() = default;

    // AAssetManager_fromJava. env/java_manager are host values (HostJni has already resolved the
    // guest JNIEnv/jobject handles on the calling thread). Returns an opaque host manager handle,
    // or 0 on failure.
    virtual std::uint64_t manager_from_java(JniBackend::Env env, JniBackend::Ref java_manager) = 0;

    // AAssetManager_open. Returns an opaque host asset handle, or 0 if the file does not exist or
    // the manager handle is invalid.
    virtual std::uint64_t open(std::uint64_t manager, const std::string& filename, std::int32_t mode) = 0;

    // AAsset_getLength. -1 for an invalid asset handle.
    virtual std::int64_t length(std::uint64_t asset) = 0;

    // AAsset_read. Returns the number of bytes read (0 at EOF), or -1 on error / invalid handle.
    virtual std::int64_t read(std::uint64_t asset, void* buffer, std::size_t count) = 0;

    // AAsset_close. A no-op for an invalid handle.
    virtual void close(std::uint64_t asset) = 0;

    struct FileDescriptor {
        int fd = -1;  // -1: invalid handle, or the asset is not backed by a plain fd (compressed).
        std::int64_t start = 0;
        std::int64_t length = 0;
    };
    // AAsset_openFileDescriptor. The fd is valid in the guest process (same process).
    virtual FileDescriptor open_file_descriptor(std::uint64_t asset) = 0;

    // AAsset_getBuffer: the whole asset in host memory, or nullptr. HostAssets copies it into
    // guest memory; the pointer itself can never cross, being outside the guest's 4 GiB space.
    virtual const void* buffer(std::uint64_t asset) = 0;
};

}  // namespace zb
