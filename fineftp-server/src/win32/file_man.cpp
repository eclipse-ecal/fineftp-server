/// @file

#include "file_man.h"

#include <map>
#include <mutex>
#include <sstream>

#include "win_str_convert.h"


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

  const std::lock_guard<std::mutex> lock{guard};
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
  const std::lock_guard<std::mutex> lock{guard};
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
  HANDLE handle =
    ::CreateFileW(s.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
  auto handle =
    ::CreateFileA(s.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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

  auto* map_handle = ::CreateFileMapping(handle, nullptr, PAGE_READONLY, sz.HighPart, sz.LowPart, nullptr);
  if ((map_handle == INVALID_HANDLE_VALUE) || (map_handle == nullptr))
  {
    ::CloseHandle(handle);
    return {};
  }

  auto* map_start = ::MapViewOfFile(map_handle, FILE_MAP_READ, 0, 0, sz.QuadPart);
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
  
WriteableFile::WriteableFile(const std::string& filename, std::ios::openmode mode)
{
  // std::ios::binary is ignored in mode because, on Windows, even ASCII files have to be stored as
  // binary files as they come in with the right line endings.

  DWORD dwDesiredAccess = GENERIC_WRITE;  // It's always a writeable file
  if (bool(mode & std::ios::app))
  {
    dwDesiredAccess |= FILE_APPEND_DATA;  // Append to the file
  }

  DWORD dwCreationDisposition = 0;
  if (bool(mode & std::ios::app))
  {
    dwCreationDisposition = OPEN_EXISTING;   // Append => Open existing file
  }
  else
  {
    dwCreationDisposition = CREATE_ALWAYS;   // Not Append => Create new file
  }

#if !defined(__GNUG__)
  auto wfilename = StrConvert::Utf8ToWide(filename);
  handle_ = ::CreateFileW(wfilename.c_str(), dwDesiredAccess, FILE_SHARE_DELETE, nullptr, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
  handle_ = ::CreateFileA(filename.c_str(), dwDesiredAccess, FILE_SHARE_DELETE, nullptr, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
#endif

  if (INVALID_HANDLE_VALUE != handle_ && (mode & std::ios::app) == std::ios::app)
  {
    if (INVALID_SET_FILE_POINTER == ::SetFilePointer(handle_, 0, nullptr, FILE_END))
    {
      close();
    }
  }
}

WriteableFile::~WriteableFile()
{
  close();
}
  
void WriteableFile::close()
{
  if (INVALID_HANDLE_VALUE != handle_)
  {
    ::CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
}
  
void WriteableFile::write(const char* data, std::size_t sz)
{
  (void)::WriteFile(handle_, data, static_cast<DWORD>(sz), nullptr, nullptr);
}

}

