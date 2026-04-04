#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"

#ifdef INCLUDE_KTX_SUPPORT
#include "fast/backends/gfx_rendering_api.h"
#include "fast/resource/type/Texture.h"
#endif

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

bool TranscodeKtxTexture(Texture* texture, GfxCompressedTexFormat preferred);

// Called once from Interpreter::Init() so ReadResource knows the target format.
void SetKtxPreferredFormat(GfxCompressedTexFormat fmt);

class ResourceFactoryKtxTextureV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};
#endif
} // namespace Fast
