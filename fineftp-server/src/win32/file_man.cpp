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
  if (data_ != nullptr)
    ::UnmapViewOfFile(data_);

  if (map_handle_ != INVALID_HANDLE_VALUE)
    ::CloseHandle(map_handle_);

  if (handle_ != INVALID_HANDLE_VALUE)
    ::CloseHandle(handle_);

  const std::lock_guard<std::mutex> lock{guard};
  if (!path_.empty())
  {
    (void)files.erase(path_);
  }
}

std::shared_ptr<ReadableFile> ReadableFile::get(const Str& file_path)
{
  std::basic_ostringstream<Str::value_type> os;
  for (auto c : file_path)
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

  auto&& file_path_fixed_separators = os.str();

  // See if we already have this file mapped
  const std::lock_guard<std::mutex> lock{guard};
  auto existing_files_it = files.find(file_path_fixed_separators);
  if (files.end() != existing_files_it)
  {
    auto readable_file_ptr = existing_files_it->second.lock();
    if (readable_file_ptr)
    {
      return readable_file_ptr;
    }
  }

#if !defined(__GNUG__)
  HANDLE file_handle =
    ::CreateFileW(file_path_fixed_separators.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
  auto file_handle =
    ::CreateFileA(file_path_fixed_separators.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#endif
  if (INVALID_HANDLE_VALUE == file_handle)
  {
    return {};
  }

  // Get the file size by Using GetFileInformationByHandle
  BY_HANDLE_FILE_INFORMATION file_info;
  if (0 == ::GetFileInformationByHandle(file_handle, &file_info))
  {
    ::CloseHandle(file_handle);
    return {};
  }
  LARGE_INTEGER file_size;
  file_size.LowPart = file_info.nFileSizeLow;
  file_size.HighPart = file_info.nFileSizeHigh;

  // Create new ReadableFile ptr
  std::shared_ptr<ReadableFile> readable_file_ptr(new ReadableFile{});

  if (file_size.QuadPart == 0)
  { 
    // Handle zero-size files
    readable_file_ptr->path_        = std::move(file_path_fixed_separators);
    readable_file_ptr->size_        = file_size.QuadPart;
    readable_file_ptr->data_        = static_cast<uint8_t*>(nullptr);
    readable_file_ptr->handle_      = file_handle;
    readable_file_ptr->map_handle_  = INVALID_HANDLE_VALUE;
  }
  else
  {
    // Handle non-zero-size files
    auto* map_handle = ::CreateFileMapping(file_handle, nullptr, PAGE_READONLY, file_size.HighPart, file_size.LowPart, nullptr);
    if ((map_handle == INVALID_HANDLE_VALUE) || (map_handle == nullptr))
    {
      ::CloseHandle(file_handle);
      return {};
    }

    auto* map_start = ::MapViewOfFile(map_handle, FILE_MAP_READ, 0, 0, file_size.QuadPart);
    if (nullptr == map_start)
    {
      ::CloseHandle(map_handle);
      ::CloseHandle(file_handle);
      return {};
    }

    readable_file_ptr->path_        = std::move(file_path_fixed_separators);
    readable_file_ptr->size_        = file_size.QuadPart;
    readable_file_ptr->data_        = static_cast<uint8_t*>(map_start);
    readable_file_ptr->handle_      = file_handle;
    readable_file_ptr->map_handle_  = map_handle;
  }

  // Add readable_file_ptr to the map and return it to the user
  files[readable_file_ptr->path_] = readable_file_ptr;
  return readable_file_ptr;
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

  // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
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
  DWORD bytes_written{}; // Unused, but required according to https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
  (void)::WriteFile(handle_, data, static_cast<DWORD>(sz), &bytes_written, nullptr);
}

}

