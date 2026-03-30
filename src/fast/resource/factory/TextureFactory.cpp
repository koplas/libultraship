#include "fast/resource/factory/TextureFactory.h"
#include "fast/resource/type/Texture.h"
#include "spdlog/spdlog.h"

#ifdef INCLUDE_KTX_SUPPORT
#include <cstring>
#endif

namespace Fast {

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryTextureV0::ReadResource(std::shared_ptr<Ship::File> file,
                                             std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto texture = std::make_shared<Texture>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    texture->Type = (TextureType)reader->ReadUInt32();
    texture->Width = reader->ReadUInt32();
    texture->Height = reader->ReadUInt32();
    texture->ImageDataSize = reader->ReadUInt32();
    texture->ImageData = new uint8_t[texture->ImageDataSize];

    reader->Read((char*)texture->ImageData, texture->ImageDataSize);

    return texture;
}

std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryTextureV1::ReadResource(std::shared_ptr<Ship::File> file,
                                             std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto texture = std::make_shared<Texture>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    texture->Type = (TextureType)reader->ReadUInt32();
    texture->Width = reader->ReadUInt32();
    texture->Height = reader->ReadUInt32();
    texture->Flags = reader->ReadUInt32();
    texture->HByteScale = reader->ReadFloat();
    texture->VPixelScale = reader->ReadFloat();
    texture->ImageDataSize = reader->ReadUInt32();
    texture->ImageData = new uint8_t[texture->ImageDataSize];

    reader->Read((char*)texture->ImageData, texture->ImageDataSize);

    return texture;
}

#ifdef INCLUDE_KTX_SUPPORT
// KTX2 fixed-header layout (bytes 12-27):
//   [12] vkFormat     (4 bytes)
//   [16] typeSize     (4 bytes)
//   [20] pixelWidth   (4 bytes)
//   [24] pixelHeight  (4 bytes)
static constexpr size_t KTX2_HEADER_PIXELWIDTH_OFFSET = 20;
static constexpr size_t KTX2_HEADER_PIXELHEIGHT_OFFSET = 24;
static constexpr size_t KTX2_HEADER_MIN_SIZE = 28;

std::shared_ptr<Ship::IResource>
ResourceFactoryKtxTextureV0::ReadResource(std::shared_ptr<Ship::File> file,
                                          std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    if (file->Buffer->size() < KTX2_HEADER_MIN_SIZE) {
        SPDLOG_ERROR("ResourceFactoryKtxTextureV0: '{}' is too small to be a valid KTX2 file", initData->Path);
        return nullptr;
    }

    // Read base dimensions directly from the fixed KTX2 header — no libktx needed here.
    // Transcoding to a GPU-native compressed format happens later in RegisterBlendedTexture
    // once the active rendering backend is known.
    const auto* header = reinterpret_cast<const uint8_t*>(file->Buffer->data());
    const uint32_t width = *reinterpret_cast<const uint32_t*>(header + KTX2_HEADER_PIXELWIDTH_OFFSET);
    const uint32_t height = *reinterpret_cast<const uint32_t*>(header + KTX2_HEADER_PIXELHEIGHT_OFFSET);

    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        SPDLOG_ERROR("ResourceFactoryKtxTextureV0: Invalid dimensions {}x{} in '{}'", width, height, initData->Path);
        return nullptr;
    }

    auto texture = std::make_shared<Texture>(initData);
    texture->Type = TextureType::KtxRaw;
    texture->Flags = TEX_FLAG_LOAD_AS_IMG;
    texture->Width = static_cast<uint16_t>(width);
    texture->Height = static_cast<uint16_t>(height);
    texture->ImageDataSize = static_cast<uint32_t>(file->Buffer->size());
    texture->ImageData = new uint8_t[texture->ImageDataSize];
    std::memcpy(texture->ImageData, file->Buffer->data(), texture->ImageDataSize);

    return texture;
}
#endif

} // namespace Fast
