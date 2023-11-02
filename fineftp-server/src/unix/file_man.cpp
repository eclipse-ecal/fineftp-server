/// @file

#include "file_man.h"

#include <fcntl.h>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sstream>
#include <unistd.h>

namespace fineftp
{

namespace {

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
  if (!pth_.empty())
  {
    (void)files.erase(pth_);
  }
}

std::shared_ptr<ReadableFile> ReadableFile::get(const std::string& pth)
{
  // See if we already have this file mapped
  const std::lock_guard<std::mutex> lock{guard};
  auto                        fit = files.find(pth);
  if (files.end() != fit)
  {
    auto p = fit->second.lock();
    if (p)
    {
      return p;
    }
  }
  
  auto handle = ::open(pth.c_str(), O_RDONLY);
  if (-1 == handle)
  {
    return {};
  }

  struct stat st {};
  if (-1 == ::fstat(handle, &st))
  {
    ::close(handle);
    return {};
  }

  auto* map_start = ::mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, handle, 0);
  if (MAP_FAILED == map_start)
  {
    ::close(handle);
    return {};
  }

  ::close(handle);

  std::shared_ptr<ReadableFile> p{new ReadableFile{}};
  p->pth_        = pth;
  p->size_       = st.st_size;
  p->data_       = static_cast<uint8_t*>(map_start);
  files[p->pth_] = p;
  return p;
}

}
