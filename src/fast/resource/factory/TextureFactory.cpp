#include "fast/resource/factory/TextureFactory.h"
#include "fast/resource/type/Texture.h"
#include "spdlog/spdlog.h"

#ifdef INCLUDE_KTX_SUPPORT
#include <ktx.h>
#include <atomic>
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
// Preferred GPU-native compressed format, set once from Interpreter::Init().
static std::atomic<int> s_ktxPreferredFormat{ static_cast<int>(GfxCompressedTexFormat::None) };

void SetKtxPreferredFormat(GfxCompressedTexFormat fmt) {
    s_ktxPreferredFormat.store(static_cast<int>(fmt), std::memory_order_relaxed);
}

bool TranscodeKtxTexture(Texture* texture, GfxCompressedTexFormat preferred) {
    ktx_transcode_fmt_e target;
    switch (preferred) {
        case GfxCompressedTexFormat::BC3_UNORM:
            target = KTX_TTF_BC3_RGBA;
            break;
        case GfxCompressedTexFormat::BC7_UNORM:
            target = KTX_TTF_BC7_RGBA;
            break;
        case GfxCompressedTexFormat::ETC2_RGBA8:
            target = KTX_TTF_ETC2_RGBA;
            break;
        case GfxCompressedTexFormat::ASTC_4x4:
            target = KTX_TTF_ASTC_4x4_RGBA;
            break;
        default:
            SPDLOG_ERROR("TranscodeKtxTexture: unsupported format for '{}'", texture->GetInitData()->Path);
            return false;
    }

    ktxTexture2* kTex = nullptr;
    KTX_error_code result = ktxTexture2_CreateFromMemory(texture->ImageData, texture->ImageDataSize,
                                                         KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTex);
    if (result != KTX_SUCCESS) {
        SPDLOG_ERROR("TranscodeKtxTexture: failed to open '{}': {}", texture->GetInitData()->Path,
                     ktxErrorString(result));
        return false;
    }

    if (ktxTexture2_NeedsTranscoding(kTex))
        result = ktxTexture2_TranscodeBasis(kTex, target, 0);

    if (result == KTX_SUCCESS) {
        const ktx_uint8_t* baseData = ktxTexture_GetData(ktxTexture(kTex));
        uint32_t totalSize = 0;
        for (uint32_t level = 0; level < kTex->numLevels; ++level)
            totalSize += static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTex), level));
        // Transcoded output can be larger than the raw input (e.g. ETC1S→BC7
        // expands up to 16×), so always allocate a correctly-sized new buffer.
        uint8_t* transcoded = new uint8_t[totalSize];
        uint32_t writeOffset = 0;
        for (uint32_t level = 0; level < kTex->numLevels; ++level) {
            ktx_size_t levelOffset = 0;
            ktxTexture_GetImageOffset(ktxTexture(kTex), level, 0, 0, &levelOffset);
            const ktx_size_t levelSize = ktxTexture_GetImageSize(ktxTexture(kTex), level);
            std::memcpy(transcoded + writeOffset, baseData + levelOffset, levelSize);
            writeOffset += static_cast<uint32_t>(levelSize);
        }
        delete[] texture->ImageData;
        texture->ImageData = transcoded;
        texture->ImageDataSize = totalSize;
        texture->CompressedFormat = preferred;
        texture->CompressedMipCount = kTex->numLevels;
    } else {
        SPDLOG_ERROR("TranscodeKtxTexture: transcode failed for '{}': {}", texture->GetInitData()->Path,
                     ktxErrorString(result));
    }

    ktxTexture_Destroy(ktxTexture(kTex));
    return texture->CompressedFormat != GfxCompressedTexFormat::None;
}

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

    // Transcode inline — this ReadResource call is already on the ResourceManager
    // thread pool, so there is no main-thread stall. If the preferred format has
    // not been registered yet (renderer not initialised) the raw bytes stay in
    // ImageData and ImportTextureImg will transcode synchronously as a fallback.
    const auto preferred = static_cast<GfxCompressedTexFormat>(s_ktxPreferredFormat.load(std::memory_order_relaxed));
    if (preferred != GfxCompressedTexFormat::None)
        TranscodeKtxTexture(texture.get(), preferred);

    return texture;
}
#endif

} // namespace Fast
