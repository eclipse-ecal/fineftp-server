/// @file

#include "file_man.h"

#include <cstdint>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fineftp
{

  namespace
  {
    std::mutex                                         guard;
    std::map<std::string, std::weak_ptr<ReadableFile>> files;
  }  // namespace

  ReadableFile::~ReadableFile()
  {
    if (nullptr != data_)
    {
      ::munmap(data_, size_);
    }

    const std::lock_guard<std::mutex> lock{guard};
    if (!path_.empty())
    {
      (void)files.erase(path_);
    }
  }

  std::shared_ptr<ReadableFile> ReadableFile::get(const std::string& file_path)
  {
    // See if we already have this file mapped
    const std::lock_guard<std::mutex> lock{guard};
    auto existing_files_it = files.find(file_path);
    if (files.end() != existing_files_it)
    {
      auto readable_file_ptr = existing_files_it->second.lock();
      if (readable_file_ptr)
      {
        return readable_file_ptr;
      }
    }

    auto handle = ::open(file_path.c_str(), O_RDONLY);
    if (-1 == handle)
    {
      return {};
    }

    struct stat file_status {};
    if (-1 == ::fstat(handle, &file_status))
    {
      ::close(handle);
      return {};
    }

    void* map_start = nullptr;

    if (file_status.st_size > 0)
    {
      // Only mmap file with a size > 0
      map_start = ::mmap(nullptr, file_status.st_size, PROT_READ, MAP_SHARED, handle, 0);
      if (MAP_FAILED == map_start)
      {
        ::close(handle);
        return {};
      }
    }

    ::close(handle);

    std::shared_ptr<ReadableFile> readable_file_ptr{new ReadableFile{}};
    readable_file_ptr->path_        = file_path;
    readable_file_ptr->size_        = file_status.st_size;
    readable_file_ptr->data_        = static_cast<uint8_t*>(map_start);
    files[readable_file_ptr->path_] = readable_file_ptr;
    return readable_file_ptr;
  }
}
