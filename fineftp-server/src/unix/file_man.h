/// @file

#ifndef FINEFTP_SERVER_SRC_UNIX_FILE_MAN_H_
#define FINEFTP_SERVER_SRC_UNIX_FILE_MAN_H_

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <vector>

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

  /// Retrieves the file at the specified path.
  ///
  /// @param pth      The path of the file.
  ///
  /// @param The requested file or nullptr if the file could not be retrieved.
  static std::shared_ptr<ReadableFile> get(const std::string& pth);

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
  const std::string& path() const;

private:
  ReadableFile() = default;

  std::string   pth_    = {};
  std::size_t   size_   = {};
  std::uint8_t* data_   = {};
};


/// @brief A writeable file tailored for the Win32 environment.
class WriteableFile
{
public:
  /// @brief Constructor.
  ///
  /// @param filename  The (UTF-8 encoded) name of the file.
  /// @param mode      The open mode to use for the file (std::ios::out is implied).
  WriteableFile(const std::string& filename, std::ios::openmode mode)
    : file_stream_(filename, std::ios::out | mode)
    , stream_buffer_(1024 * 1024)
  {
    file_stream_.rdbuf()->pubsetbuf(stream_buffer_.data(), static_cast<std::streamsize>(stream_buffer_.size()));
  }

  // Copy disabled
  WriteableFile(const WriteableFile&)            = delete;
  WriteableFile& operator=(const WriteableFile&) = delete;

  // Move disabled (as we are storing the shared_from_this() pointer in lambda captures)
  WriteableFile& operator=(WriteableFile&&)      = delete;
  WriteableFile(WriteableFile&&)                 = delete;

  ~WriteableFile()
  {
    close();
  }

  void write(const char* data, std::size_t sz)
  {
    file_stream_.write(data, static_cast<std::streamsize>(sz));
  }

  void close()
  {
    file_stream_.flush();
    file_stream_.close();
  }

  bool good() const
  {
    return file_stream_.good();
  }

  std::fstream      file_stream_;
  std::vector<char> stream_buffer_;
};


inline std::size_t ReadableFile::size() const
{
  return size_;
}

inline const std::uint8_t* ReadableFile::data() const
{
  return data_;
}
   
inline const std::string& ReadableFile::path() const
{
  return pth_;
}

}

#endif  // FINEFTP_SERVER_SRC_UNIX_FILE_MAN_H_

