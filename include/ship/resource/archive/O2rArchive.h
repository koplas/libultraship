#pragma once

#undef _DLL

#include <string>
#include <stdint.h>
#include <string>
#include <mutex>
#include <vector>

#include "mz.h"
#include "mz_zip.h"
#include "mz_stream.h"
#include "mz_zip_rw.h"

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

    // Check out a reader from the pool (creating a new one if the pool is empty).
    // Returns nullptr if the archive is not open or the file cannot be opened.
    void* AcquireReader();
    // Return a reader to the pool. If the archive has already been closed the
    // reader is destroyed instead.
    void ReleaseReader(void* reader);
};
} // namespace Ship
