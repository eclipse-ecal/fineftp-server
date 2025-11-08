/// @file

#ifndef FINEFTP_SERVER_SRC_WIN32_FILE_MAN_H_
#define FINEFTP_SERVER_SRC_WIN32_FILE_MAN_H_

#include <windows.h>

#include <cstdint>
#include <ios>
#include <memory>
#include <string>

namespace fineftp
{

/// A memory mapped read-only file.
///
/// @note The implementation is NOT thread safe!
class ReadableFile
{
public:
  ReadableFile(const ReadableFile&)            = delete;
  ReadableFile& operator=(const ReadableFile&) = delete;
  ReadableFile(ReadableFile&&)                 = delete;
  ReadableFile& operator=(ReadableFile&&)      = delete;
  ~ReadableFile();

#if !defined(__GNUG__)
  using Str = std::wstring;
#else
  using Str = std::string;
#endif

  /// Retrieves the file at the specified path.
  ///
  /// @param file_path      The path of the file.
  ///
  /// @param The requested file or nullptr if the file could not be retrieved.
  static std::shared_ptr<ReadableFile> get(const Str& file_path);

  /// Returns the size of the file.
  ///
  /// @return The size of the file.
  std::size_t size() const;

  /// Returns a pointer to the beginning of the file contents.
  ///
  /// @return A pointer to the beginning of the file contents.
  const std::uint8_t* data() const;

  /// Returns the path of the file.
  ///
  /// @return The path of the file.
  const Str& path() const;

private:
  ReadableFile() = default;

  Str           path_       = {};
  std::size_t   size_       = {};
  std::uint8_t* data_       = {};
  HANDLE        handle_     = INVALID_HANDLE_VALUE;
  HANDLE        map_handle_ = INVALID_HANDLE_VALUE;
};


/// @brief A writeable file tailored for the Win32 environment.
class WriteableFile
{
public:
  /// @brief Constructor.
  ///
  /// @param filename  The (UTF-8 encoded) name of the file.
  /// @param mode      The open mode to use for the file (std::ios::out is implied).
  WriteableFile(const std::string& filename, std::ios::openmode mode);

  // Copy disable
  WriteableFile(const WriteableFile&)            = delete;
  WriteableFile& operator=(const WriteableFile&) = delete;

  // Move disabled (as we are storing the shared_from_this() pointer in lambda captures)
  WriteableFile& operator=(WriteableFile&&)      = delete;
  WriteableFile(WriteableFile&&)                 = delete;

  ~WriteableFile();

  void write(const char* data, std::size_t sz);
  void close();
  bool good() const;
  std::string filename() const;

private:
  std::string filename_  = {};
  HANDLE handle_    = INVALID_HANDLE_VALUE;
};


inline std::size_t ReadableFile::size() const
{
  return size_;
}

inline const std::uint8_t* ReadableFile::data() const
{
  return data_;
}

inline const ReadableFile::Str& ReadableFile::path() const
{
  return path_;
}

inline bool WriteableFile::good() const
{
  return INVALID_HANDLE_VALUE != handle_;
}

}

#endif  // FINEFTP_SERVER_SRC_WIN32_FILE_MAN_H_

