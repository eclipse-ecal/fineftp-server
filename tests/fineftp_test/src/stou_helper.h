#include <cstdlib>
#include <iostream>
#include <string>
#include <array>
#include <stdexcept>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

// Run a shell command and return its stdout + exit code.
struct CmdResult {
  std::string output;
  int exitCode;
};

CmdResult runCommand(const std::string& cmd) {
  std::array<char, 256> buffer;
  std::string result;

#ifdef _WIN32
  FILE* pipe = _popen(cmd.c_str(), "r");
#else
  FILE* pipe = popen(cmd.c_str(), "r");
#endif

  if (!pipe) {
    throw std::runtime_error("Failed to open pipe for command: " + cmd);
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    result += buffer.data();
  }

#ifdef _WIN32
  int status = _pclose(pipe);
#else
  int status = pclose(pipe);
#endif

  return { result, status };
}

void uploadWithStou(const std::string& filePath,
  const std::string& host,
  int port) {
#ifdef _WIN32
  // ----- Windows: use PowerShell with FtpWebRequest -----
  // Write the script to a temp .ps1 file and run it with -File.
  // Passing multi-line scripts via -Command "..." through cmd.exe
  // is unreliable — newlines and special chars get silently mangled.
  std::string portStr = std::to_string(port);

  std::string ps;
  ps += "$ErrorActionPreference = 'Stop'\n";
  ps += "try {\n";
  ps += "  $file      = '" + filePath + "'\n";
  ps += "  $uri       = 'ftp://" + host + ":" + portStr + "/'\n";
  ps += "  $request   = [System.Net.FtpWebRequest]::Create($uri)\n";
  ps += "  $request.Method      = [System.Net.WebRequestMethods+Ftp]::UploadFileWithUniqueName\n";
  ps += "  $request.Credentials = New-Object System.Net.NetworkCredential('anonymous','anonymous@')\n";
  ps += "  $request.UseBinary   = $true\n";
  ps += "\n";
  ps += "  $content = [System.IO.File]::ReadAllBytes($file)\n";
  ps += "  $stream  = $request.GetRequestStream()\n";
  ps += "  $stream.Write($content, 0, $content.Length)\n";
  ps += "  $stream.Close()\n";
  ps += "\n";
  ps += "  $response = [System.Net.FtpWebResponse]$request.GetResponse()\n";
  ps += "  Write-Host ('Status : ' + $response.StatusDescription)\n";
  ps += "  $response.Close()\n";
  ps += "} catch {\n";
  ps += "  Write-Error $_.Exception.Message\n";
  ps += "  exit 1\n";
  ps += "}\n";

  // Write script to a temp file
  std::string scriptPath;
  char tmpPath[MAX_PATH];
  if (GetTempPathA(MAX_PATH, tmpPath)) {
    scriptPath = std::string(tmpPath) + "ftp_stou_upload.ps1";
  }
  else {
    scriptPath = "ftp_stou_upload.ps1";
  }

  {
    std::ofstream scriptFile(scriptPath);
    if (!scriptFile.is_open()) {
      std::cerr << "Failed to create temp script: " << scriptPath << "\n";
      return;
    }
    scriptFile << ps;
  }

  std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -File \""
    + scriptPath + "\"";

#else
  // ----- Linux / macOS: use the standard ftp client -----
  // The `sunique` command tells the ftp client to use STOU instead of
  // STOR when executing `put`, so the server picks a unique filename.
  // We write the commands to a temp script file and pipe it via `-n`
  // (no auto-login) to avoid interactive prompts.
  std::string portStr = std::to_string(port);

  std::string ftpScript;
  ftpScript += "open " + host + " " + portStr + "\n";
  ftpScript += "user anonymous anonymous@\n";
  ftpScript += "binary\n";
  ftpScript += "sunique\n";
  ftpScript += "put " + filePath + "\n";
  ftpScript += "bye\n";

  // Write script to a temp file
  std::string scriptPath = "/tmp/ftp_stou_upload.ftp";
  {
    std::ofstream scriptFile(scriptPath);
    if (!scriptFile.is_open()) {
      std::cerr << "Failed to create temp script: " << scriptPath << "\n";
      return;
    }
    scriptFile << ftpScript;
  }

  // -n = no auto-login (we login manually via the "user" command)
  std::string cmd = "ftp -n < \"" + scriptPath + "\""; 
#endif

  std::cout << "Running command:\n" << cmd << "\n\n";

  try {
    CmdResult res = runCommand(cmd);
    std::cout << "Output:\n" << res.output << "\n";
    if (res.exitCode != 0) {
      std::cerr << "Command exited with code " << res.exitCode << "\n";
    }
    else {
      std::cout << "STOU Upload completed successfully.\n";
    }
  }
  catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
  }
}