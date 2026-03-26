#pragma once

#undef _DLL

#include <string>
#include <stdint.h>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ship/resource/File.h"
#include "ship/resource/Resource.h"
#include "ship/resource/archive/Archive.h"

namespace Ship {
struct File;

class O2rArchive final : virtual public Archive {
  public:
    O2rArchive(const std::string& archivePath);
    ~O2rArchive();

    bool Open();
    bool Close();
    bool WriteFile(const std::string& filename, const std::vector<uint8_t>& data);

    std::shared_ptr<File> LoadFile(const std::string& filePath);
    std::shared_ptr<File> LoadFile(uint64_t hash);

  private:
    bool mIsOpen;
    mutable std::mutex mReaderPoolMutex;
    std::vector<void*> mReaderPool;
    // Maps entry filename → disk_offset for O(1) entry lookup (populated at Open/WriteFile).
    std::unordered_map<std::string, int64_t> mDiskOffsets;

    void* AcquireReader();
    void ReleaseReader(void* reader);
    // Iterate all entries in reader, populating mDiskOffsets and calling IndexFile.
    void BuildIndex(void* reader);
};
} // namespace Ship
