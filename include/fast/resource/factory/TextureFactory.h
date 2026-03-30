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
// Stores raw KTX2 file bytes as a texture resource. Transcoding to a GPU-native
// compressed format (BC7/BC3/ETC2/ASTC) happens later in RegisterBlendedTexture
// or ImportTextureImg, once the active rendering backend is known.
class ResourceFactoryKtxTextureV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};
#endif
} // namespace Fast
