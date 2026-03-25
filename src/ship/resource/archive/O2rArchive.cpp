#include "ship/resource/archive/O2rArchive.h"

#include "ship/Context.h"
#include "ship/window/Window.h"
#include "spdlog/spdlog.h"

#include <cstdio>
#include <cstring>

namespace Ship {
O2rArchive::O2rArchive(const std::string& archivePath) : Archive(archivePath), mZipReader(nullptr) {
}

O2rArchive::~O2rArchive() {
    SPDLOG_TRACE("destruct o2rarchive: {}", GetPath());
    Close();
}

std::shared_ptr<File> O2rArchive::LoadFile(uint64_t hash) {
    const std::string& filePath =
        *Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HashToString(hash);
    return LoadFile(filePath);
}

std::shared_ptr<File> O2rArchive::LoadFile(const std::string& filePath) {
    if (mZipReader == nullptr) {
        SPDLOG_TRACE("Failed to open file {} from zip archive {}. Archive not open.", filePath, GetPath());
        return nullptr;
    }

    if (mz_zip_reader_locate_entry(mZipReader, filePath.c_str(), 0) != MZ_OK) {
        SPDLOG_TRACE("Failed to find file {} in zip archive  {}.", filePath, GetPath());
        return nullptr;
    }

    mz_zip_file* zipEntryStat = nullptr;
    if (mz_zip_reader_entry_get_info(mZipReader, &zipEntryStat) != MZ_OK) {
        SPDLOG_TRACE("Failed to get entry information for file {} in zip archive  {}.", filePath, GetPath());
        return nullptr;
    }

    // Filesize 0, no logging needed
    if (zipEntryStat->uncompressed_size == 0) {
        SPDLOG_TRACE("Failed to load file {}; filesize 0", filePath);
        return nullptr;
    }

    if (mz_zip_reader_entry_open(mZipReader) != MZ_OK) {
        SPDLOG_TRACE("Failed to open file {} in zip archive  {}.", filePath, GetPath());
        return nullptr;
    }

    auto fileToLoad = std::make_shared<File>();
    fileToLoad->Buffer = std::make_shared<std::vector<char>>(zipEntryStat->uncompressed_size);

    if (mz_zip_reader_entry_read(mZipReader, fileToLoad->Buffer->data(),
                                 static_cast<int32_t>(zipEntryStat->uncompressed_size)) < 0) {
        SPDLOG_TRACE("Error reading file {} in zip archive  {}.", filePath, GetPath());
    }

    if (mz_zip_reader_entry_close(mZipReader) != MZ_OK) {
        SPDLOG_TRACE("Error closing file {} in zip archive  {}.", filePath, GetPath());
    }

    fileToLoad->IsLoaded = true;

    return fileToLoad;
}

bool O2rArchive::Open() {
    mZipReader = mz_zip_reader_create();

    int32_t err = mz_zip_reader_open_file(mZipReader, GetPath().c_str());
    if (err != MZ_OK) {
        // Check if the file exists; if not, create an empty archive (mirrors libzip ZIP_CREATE behaviour)
        FILE* testFile = fopen(GetPath().c_str(), "r");
        if (testFile) {
            fclose(testFile);
            SPDLOG_ERROR("Failed to load zip file \"{}\"", GetPath());
            mz_zip_reader_delete(&mZipReader);
            mZipReader = nullptr;
            return false;
        }

        void* writer = mz_zip_writer_create();
        if (mz_zip_writer_open_file(writer, GetPath().c_str(), 0, 0) != MZ_OK) {
            SPDLOG_ERROR("Failed to load zip file \"{}\"", GetPath());
            mz_zip_writer_delete(&writer);
            mz_zip_reader_delete(&mZipReader);
            mZipReader = nullptr;
            return false;
        }
        mz_zip_writer_close(writer);
        mz_zip_writer_delete(&writer);

        err = mz_zip_reader_open_file(mZipReader, GetPath().c_str());
        if (err != MZ_OK) {
            SPDLOG_ERROR("Failed to load zip file \"{}\"", GetPath());
            mz_zip_reader_delete(&mZipReader);
            mZipReader = nullptr;
            return false;
        }
    }

    err = mz_zip_reader_goto_first_entry(mZipReader);
    while (err == MZ_OK) {
        // It is possible for directories to have entries in a zip
        // file, we don't want those indexed as files in the archive
        if (mz_zip_reader_entry_is_dir(mZipReader) != MZ_OK) {
            mz_zip_file* fileInfo = nullptr;
            mz_zip_reader_entry_get_info(mZipReader, &fileInfo);
            IndexFile(fileInfo->filename);
        }
        err = mz_zip_reader_goto_next_entry(mZipReader);
    }

    return true;
}

bool O2rArchive::Close() {
    if (mZipReader == nullptr) {
        SPDLOG_ERROR("Cannot close zip file. Zip file not loaded. \"{}\"", GetPath());
        return false;
    }

    if (mz_zip_reader_close(mZipReader) != MZ_OK) {
        SPDLOG_ERROR("Failed to close zip file \"{}\"", GetPath());
        return false;
    }

    mz_zip_reader_delete(&mZipReader);
    mZipReader = nullptr;
    return true;
}

bool O2rArchive::WriteFile(const std::string& filePath, const std::vector<uint8_t>& data) {
    if (!mZipReader) {
        SPDLOG_ERROR("Cannot write to zip: Archive is not open.");
        return false;
    }

    std::string tempPath = GetPath() + ".tmp";

    // Build a new archive in a temp file, copying all existing entries except the one being replaced
    void* writer = mz_zip_writer_create();
    if (mz_zip_writer_open_file(writer, tempPath.c_str(), 0, 0) != MZ_OK) {
        SPDLOG_ERROR("Failed to create temporary zip archive for writing");
        mz_zip_writer_delete(&writer);
        return false;
    }

    int32_t err = mz_zip_reader_goto_first_entry(mZipReader);
    while (err == MZ_OK) {
        mz_zip_file* fileInfo = nullptr;
        mz_zip_reader_entry_get_info(mZipReader, &fileInfo);

        if (strcmp(fileInfo->filename, filePath.c_str()) != 0) {
            mz_zip_writer_copy_from_reader(writer, mZipReader);
        }

        err = mz_zip_reader_goto_next_entry(mZipReader);
    }

    // Add the new/updated entry
    mz_zip_file fileInfo = {};
    fileInfo.filename = filePath.c_str();
    fileInfo.compression_method = MZ_COMPRESS_METHOD_DEFLATE;

    if (mz_zip_writer_add_buffer(writer, (void*)data.data(), static_cast<int32_t>(data.size()), &fileInfo) != MZ_OK) {
        SPDLOG_ERROR("Failed to add file \"{}\" to ZIP", filePath);
        mz_zip_writer_close(writer);
        mz_zip_writer_delete(&writer);
        std::remove(tempPath.c_str());
        return false;
    }

    if (mz_zip_writer_close(writer) != MZ_OK) {
        SPDLOG_ERROR("Failed to save changes to zip archive");
        mz_zip_writer_delete(&writer);
        std::remove(tempPath.c_str());
        return false;
    }
    mz_zip_writer_delete(&writer);

    // Close the reader before replacing the file on disk
    mz_zip_reader_close(mZipReader);
    mz_zip_reader_delete(&mZipReader);
    mZipReader = nullptr;

    std::remove(GetPath().c_str());
    if (std::rename(tempPath.c_str(), GetPath().c_str()) != 0) {
        SPDLOG_ERROR("Failed to replace zip archive with updated version");
        return false;
    }

    SPDLOG_INFO("Successfully wrote file: {}", filePath);

    // Reopen the zip file so that it may continued to be used by libultraship
    mZipReader = mz_zip_reader_create();
    if (mz_zip_reader_open_file(mZipReader, GetPath().c_str()) != MZ_OK) {
        SPDLOG_ERROR("Failed to reopen zip file after writing.");
        mz_zip_reader_delete(&mZipReader);
        mZipReader = nullptr;
        return false;
    }

    IndexFile(filePath);

    // Success
    return true;
}

} // namespace Ship
