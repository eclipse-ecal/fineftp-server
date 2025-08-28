#include "win_str_convert.h" // IWYU pragma: associated

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h> // IWYU pragma: keep
#endif // _WIN32

#include <string>

namespace fineftp
{
  namespace StrConvert
  {
#ifdef _WIN32
    std::string WideToAnsi(const std::wstring& wstr)
    {
      const int count = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
      std::string str(count, 0);
      WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &str[0], count, nullptr, nullptr); // NOLINT(readability-container-data-pointer) Reason: I need a non-const pointer here, due to the Raw C API, but .data() returns a const pointer. I don't consider a const_cast to be better.
      return str;
    }

    std::wstring AnsiToWide(const std::string& str)
    {
      const int count = MultiByteToWideChar(CP_ACP, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
      std::wstring wstr(count, 0);
      MultiByteToWideChar(CP_ACP, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], count); // NOLINT(readability-container-data-pointer) Reason: I need a non-const pointer here, due to the Raw C API, but .data() returns a const pointer. I don't consider a const_cast to be better.
      return wstr;
    }

    std::string WideToUtf8(const std::wstring& wstr)
    {
      const int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
      std::string str(count, 0);
      WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, nullptr, nullptr); // NOLINT(readability-container-data-pointer) Reason: I need a non-const pointer here, due to the Raw C API, but .data() returns a const pointer. I don't consider a const_cast to be better.
      return str;
    }

    std::wstring Utf8ToWide(const std::string& str)
    {
      const int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
      std::wstring wstr(count, 0);
      MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], count); // NOLINT(readability-container-data-pointer) Reason: I need a non-const pointer here, due to the Raw C API, but .data() returns a const pointer. I don't consider a const_cast to be better.
      return wstr;
    }

    std::string AnsiToUtf8(const std::string& str)
    {
      return WideToUtf8(AnsiToWide(str));
    }

    std::string Utf8ToAnsi(const std::string& str)
    {
      return WideToAnsi(Utf8ToWide(str));
    }
#endif
  }
}
