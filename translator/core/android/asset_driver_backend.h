#pragma once

#include "zb/asset_backend.h"

namespace zb {

// The real AssetBackend (Phase 5 Task 7): a thin wrapper over the NDK
// <android/asset_manager_jni.h> / <android/asset_manager.h>. Opaque host handles are the
// AAssetManager*/AAsset* pointers themselves, reinterpret_cast to/from std::uint64_t; HostAssets
// never lets them reach the guest, only 32-bit handles into its own tables.
class AndroidAssetBackend final : public AssetBackend {
public:
    std::uint64_t manager_from_java(JniBackend::Env env, JniBackend::Ref java_manager) override;
    std::uint64_t open(std::uint64_t manager, const std::string& filename, std::int32_t mode) override;
    std::int64_t length(std::uint64_t asset) override;
    const void* buffer(std::uint64_t asset) override;
    std::int64_t read(std::uint64_t asset, void* buffer, std::size_t count) override;
    void close(std::uint64_t asset) override;
    FileDescriptor open_file_descriptor(std::uint64_t asset) override;
};

}  // namespace zb
