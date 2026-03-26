#include "ship/resource/archive/O2rArchive.h"

#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"

#include "ship/Context.h"
#include "ship/window/Window.h"
#include "spdlog/spdlog.h"

#include <cstdio>
#include <cstring>

namespace Ship {
O2rArchive::O2rArchive(const std::string& archivePath) : Archive(archivePath), mIsOpen(false) {
}

O2rArchive::~O2rArchive() {
    SPDLOG_TRACE("destruct o2rarchive: {}", GetPath());
    Close();
}

// ---------------------------------------------------------------------------
// Reader pool helpers
// ---------------------------------------------------------------------------

void* O2rArchive::AcquireReader() {
    {
        std::lock_guard<std::mutex> lock(mReaderPoolMutex);
        if (!mReaderPool.empty()) {
            void* reader = mReaderPool.back();
            mReaderPool.pop_back();
            return reader;
        }
        if (!mIsOpen) {
            return nullptr;
        }
    }
    // Pool is empty but the archive is open: create an additional reader.
    // The mutex is NOT held here so that file I/O doesn't block other threads
    // that are returning readers to the pool.
    void* reader = mz_zip_reader_create();
    if (mz_zip_reader_open_file(reader, GetPath().c_str()) != MZ_OK) {
        mz_zip_reader_delete(&reader);
        return nullptr;
    }
    return reader;
}

void O2rArchive::ReleaseReader(void* reader) {
    if (reader == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mReaderPoolMutex);
    if (mIsOpen) {
        mReaderPool.push_back(reader);
    } else {
        // The archive was closed while this reader was checked out; discard it.
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::shared_ptr<File> O2rArchive::LoadFile(uint64_t hash) {
    const std::string& filePath =
        *Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HashToString(hash);
    return LoadFile(filePath);
}

std::shared_ptr<File> O2rArchive::LoadFile(const std::string& filePath) {
    void* reader = AcquireReader();
    if (reader == nullptr) {
        SPDLOG_TRACE("Failed to open file {} from zip archive {}. Archive not open.", filePath, GetPath());
        return nullptr;
    }

    if (mz_zip_reader_locate_entry(reader, filePath.c_str(), 0) != MZ_OK) {
        SPDLOG_TRACE("Failed to find file {} in zip archive  {}.", filePath, GetPath());
        ReleaseReader(reader);
        return nullptr;
    }

    mz_zip_file* zipEntryStat = nullptr;
    if (mz_zip_reader_entry_get_info(reader, &zipEntryStat) != MZ_OK) {
        SPDLOG_TRACE("Failed to get entry information for file {} in zip archive  {}.", filePath, GetPath());
        ReleaseReader(reader);
        return nullptr;
    }

    // Filesize 0, no logging needed
    if (zipEntryStat->uncompressed_size == 0) {
        SPDLOG_TRACE("Failed to load file {}; filesize 0", filePath);
        ReleaseReader(reader);
        return nullptr;
    }

    if (mz_zip_reader_entry_open(reader) != MZ_OK) {
        SPDLOG_TRACE("Failed to open file {} in zip archive  {}.", filePath, GetPath());
        ReleaseReader(reader);
        return nullptr;
    }

    auto fileToLoad = std::make_shared<File>();
    fileToLoad->Buffer = std::make_shared<std::vector<char>>(zipEntryStat->uncompressed_size);

    if (mz_zip_reader_entry_read(reader, fileToLoad->Buffer->data(),
                                 static_cast<int32_t>(zipEntryStat->uncompressed_size)) < 0) {
        SPDLOG_TRACE("Error reading file {} in zip archive  {}.", filePath, GetPath());
    }

    if (mz_zip_reader_entry_close(reader) != MZ_OK) {
        SPDLOG_TRACE("Error closing file {} in zip archive  {}.", filePath, GetPath());
    }

    fileToLoad->IsLoaded = true;
    ReleaseReader(reader);
    return fileToLoad;
}

bool O2rArchive::Open() {
    void* reader = mz_zip_reader_create();

    int32_t err = mz_zip_reader_open_file(reader, GetPath().c_str());
    if (err != MZ_OK) {
        // Check if the file exists; if not, create an empty archive (mirrors libzip ZIP_CREATE behaviour).
        FILE* testFile = fopen(GetPath().c_str(), "r");
        if (testFile) {
            fclose(testFile);
            SPDLOG_ERROR("Failed to load zip file \"{}\"", GetPath());
            mz_zip_reader_delete(&reader);
            return false;
        }

        void* writer = mz_zip_writer_create();
        if (mz_zip_writer_open_file(writer, GetPath().c_str(), 0, 0) != MZ_OK) {
            SPDLOG_ERROR("Failed to load zip file \"{}\"", GetPath());
            mz_zip_writer_delete(&writer);
            mz_zip_reader_delete(&reader);
            return false;
        }
        mz_zip_writer_close(writer);
        mz_zip_writer_delete(&writer);

        err = mz_zip_reader_open_file(reader, GetPath().c_str());
        if (err != MZ_OK) {
            SPDLOG_ERROR("Failed to load zip file \"{}\"", GetPath());
            mz_zip_reader_delete(&reader);
            return false;
        }
    }

    err = mz_zip_reader_goto_first_entry(reader);
    while (err == MZ_OK) {
        // It is possible for directories to have entries in a zip
        // file, we don't want those indexed as files in the archive
        if (mz_zip_reader_entry_is_dir(reader) != MZ_OK) {
            mz_zip_file* fileInfo = nullptr;
            mz_zip_reader_entry_get_info(reader, &fileInfo);
            IndexFile(fileInfo->filename);
        }
        err = mz_zip_reader_goto_next_entry(reader);
    }

    std::lock_guard<std::mutex> lock(mReaderPoolMutex);
    mReaderPool.push_back(reader);
    mIsOpen = true;
    return true;
}

bool O2rArchive::Close() {
    std::vector<void*> toClose;
    {
        std::lock_guard<std::mutex> lock(mReaderPoolMutex);
        if (!mIsOpen) {
            SPDLOG_ERROR("Cannot close zip file. Zip file not loaded. \"{}\"", GetPath());
            return false;
        }
        mIsOpen = false;
        toClose = std::move(mReaderPool);
    }

    for (void* reader : toClose) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
    }
    return true;
}

bool O2rArchive::WriteFile(const std::string& filePath, const std::vector<uint8_t>& data) {
    // Acquire a reader for iterating the existing entries.
    void* iterReader = AcquireReader();
    if (!iterReader) {
        SPDLOG_ERROR("Cannot write to zip: Archive is not open.");
        return false;
    }

    std::string tempPath = GetPath() + ".tmp";

    // Build a new archive in a temp file, copying all existing entries except the one being replaced.
    void* writer = mz_zip_writer_create();
    if (mz_zip_writer_open_file(writer, tempPath.c_str(), 0, 0) != MZ_OK) {
        SPDLOG_ERROR("Failed to create temporary zip archive for writing");
        mz_zip_writer_delete(&writer);
        ReleaseReader(iterReader);
        return false;
    }

    int32_t err = mz_zip_reader_goto_first_entry(iterReader);
    while (err == MZ_OK) {
        mz_zip_file* fileInfo = nullptr;
        mz_zip_reader_entry_get_info(iterReader, &fileInfo);

        if (strcmp(fileInfo->filename, filePath.c_str()) != 0) {
            mz_zip_writer_copy_from_reader(writer, iterReader);
        }

        err = mz_zip_reader_goto_next_entry(iterReader);
    }

    // Add the new/updated entry.
    mz_zip_file fileInfo = {};
    fileInfo.filename = filePath.c_str();
    fileInfo.compression_method = MZ_COMPRESS_METHOD_DEFLATE;

    if (mz_zip_writer_add_buffer(writer, (void*)data.data(), static_cast<int32_t>(data.size()), &fileInfo) != MZ_OK) {
        SPDLOG_ERROR("Failed to add file \"{}\" to ZIP", filePath);
        mz_zip_writer_close(writer);
        mz_zip_writer_delete(&writer);
        ReleaseReader(iterReader);
        std::remove(tempPath.c_str());
        return false;
    }

    if (mz_zip_writer_close(writer) != MZ_OK) {
        SPDLOG_ERROR("Failed to save changes to zip archive");
        mz_zip_writer_delete(&writer);
        ReleaseReader(iterReader);
        std::remove(tempPath.c_str());
        return false;
    }
    mz_zip_writer_delete(&writer);
    ReleaseReader(iterReader);

    // Drain all pooled readers before replacing the file on disk.
    std::vector<void*> toClose;
    {
        std::lock_guard<std::mutex> lock(mReaderPoolMutex);
        mIsOpen = false;
        toClose = std::move(mReaderPool);
    }
    for (void* reader : toClose) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
    }

    std::remove(GetPath().c_str());
    if (std::rename(tempPath.c_str(), GetPath().c_str()) != 0) {
        SPDLOG_ERROR("Failed to replace zip archive with updated version");
        return false;
    }

    SPDLOG_INFO("Successfully wrote file: {}", filePath);

    // Reopen the zip file so that it may continued to be used by libultraship.
    void* newReader = mz_zip_reader_create();
    if (mz_zip_reader_open_file(newReader, GetPath().c_str()) != MZ_OK) {
        SPDLOG_ERROR("Failed to reopen zip file after writing.");
        mz_zip_reader_delete(&newReader);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mReaderPoolMutex);
        mReaderPool.push_back(newReader);
        mIsOpen = true;
    }

    IndexFile(filePath);
    return true;
}

} // namespace Ship
