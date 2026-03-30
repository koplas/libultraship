#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

namespace Fast {
class ResourceFactoryBinaryTextureV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

class ResourceFactoryBinaryTextureV1 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

#ifdef INCLUDE_KTX_SUPPORT
#include "fast/backends/gfx_rendering_api.h"
#include "fast/resource/type/Texture.h"

// Transcodes a KTX2 texture in-place to the given GPU-native compressed format.
// Updates texture->ImageData, CompressedFormat, and CompressedMipCount.
// Returns true on success.
bool TranscodeKtxTexture(Texture* texture, GfxCompressedTexFormat preferred);

// Called once from Interpreter::Init() so ReadResource knows the target format.
void SetKtxPreferredFormat(GfxCompressedTexFormat fmt);

// Reads a KTX2 file and transcodes it immediately on the calling thread (the
// ResourceManager thread pool) to the GPU-native format registered via
// SetKtxPreferredFormat. If the format has not been set yet, stores raw KTX2
// bytes and sets CompressedFormat = None as a fallback.
class ResourceFactoryKtxTextureV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};
#endif
} // namespace Fast
