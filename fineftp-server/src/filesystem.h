#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <iostream>

#include <sys/stat.h>

////////////////////////////////////////////////////////////////////////////////
/// Filesystem
////////////////////////////////////////////////////////////////////////////////

namespace fineftp
{
  namespace Filesystem
  {
    enum class FileType
    {
      Unknown,
      RegularFile,
      Dir,
      CharacterDevice,
      BlockDevice,
      Fifo,
      SymbolicLink,
      Socket
    };

    class FileStatus
    {
    public:
      FileStatus(const std::string& path);

      bool isOk() const;
      FileType type() const;

      int64_t fileSize() const;

      bool permissionRootRead()     const;
      bool permissionRootWrite()    const;
      bool permissionRootExecute()  const;
      bool permissionGroupRead()    const;
      bool permissionGroupWrite()   const;
      bool permissionGroupExecute() const;
      bool permissionOwnerRead()    const;
      bool permissionOwnerWrite()   const;
      bool permissionOwnerExecute() const;

      std::string permissionString() const;

      std::string ownerString() const;

      std::string groupString() const;

      std::string timeString() const;

      bool canOpenDir() const;



    private:
      std::string path_;
      bool is_ok_;
  #ifdef WIN32
      struct __stat64 file_status_;
  #else // WIN32
      struct stat file_status_;
  #endif 
    };

    std::map<std::string, FileStatus> dirContent(const std::string& path, std::ostream& error);

    std::string cleanPath(const std::string& path, bool path_is_windows_path, char output_separator);

    std::string cleanPathNative(const std::string& path);
  }
}
