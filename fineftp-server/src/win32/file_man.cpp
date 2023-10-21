/// @file

#include "file_man.h"

#include <map>
#include <mutex>
#include <sstream>

namespace fineftp
{

namespace {

std::mutex                                               guard;
std::map<ReadableFile::Str, std::weak_ptr<ReadableFile>> files;

}  // namespace

ReadableFile::~ReadableFile()
{
  if (INVALID_HANDLE_VALUE != handle_)
  {
    ::UnmapViewOfFile(data_);
    ::CloseHandle(map_handle_);
    ::CloseHandle(handle_);
  }

  std::lock_guard<std::mutex> lock{guard};
  if (!pth_.empty())
  {
    (void)files.erase(pth_);
  }
}

std::shared_ptr<ReadableFile> ReadableFile::get(const Str& pth)
{
  std::basic_ostringstream<Str::value_type> os;
  for (auto c : pth)
  {
    if (c == '/')
    {
      os << '\\';
    }
    else
    {
      os << c;
    }
  }

  auto&& s = os.str();

  // See if we already have this file mapped
  std::lock_guard<std::mutex> lock{guard};
  auto                        fit = files.find(s);
  if (files.end() != fit)
  {
    auto p = fit->second.lock();
    if (p)
    {
      return p;
    }
  }

#if !defined(__GNUG__)
  auto handle =
    ::CreateFileW(s.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
#else
  auto handle =
    ::CreateFileA(s.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
#endif
  if (INVALID_HANDLE_VALUE == handle)
  {
    return {};
  }

  LARGE_INTEGER sz;
  if (0 == ::GetFileSizeEx(handle, &sz))
  {
    ::CloseHandle(handle);
    return {};
  }

  auto map_handle = ::CreateFileMapping(handle, 0, PAGE_READONLY, sz.HighPart, sz.LowPart, 0);
  if (INVALID_HANDLE_VALUE == map_handle)
  {
    ::CloseHandle(handle);
    return {};
  }

  auto map_start = ::MapViewOfFile(map_handle, FILE_MAP_READ, 0, 0, sz.QuadPart);
  if (nullptr == map_start)
  {
    ::CloseHandle(map_handle);
    ::CloseHandle(handle);
    return {};
  }

  std::shared_ptr<ReadableFile> p{new ReadableFile{}};
  p->pth_        = std::move(s);
  p->size_       = sz.QuadPart;
  p->data_       = static_cast<uint8_t*>(map_start);
  p->handle_     = handle;
  p->map_handle_ = map_handle;
  files[p->pth_] = p;
  return p;
}

}

