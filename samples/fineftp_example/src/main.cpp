#include <chrono>
#include <fineftp/server.h>

#include <string>
#include <thread>

int main() {

#ifdef _WIN32
  const std::string local_root =  "C:\\"; // The backslash at the end is necessary!
#else // _WIN32
  const std::string local_root =  "/";
#endif // _WIN32

  // Create an FTP Server on port 2121. We use 2121 instead of the default port
  // 21, as your application would need root privileges to open port 21.
  fineftp::FtpServer server(2121);

  // Add the well known anonymous user and some normal users. The anonymous user
  // can log in with username "anonyous" or "ftp" and any password. The normal
  // users have to provide their username and password. 
  server.addUserAnonymous(local_root, fineftp::Permission::All);
  server.addUser         ("MyUser",    "MyPassword", local_root, fineftp::Permission::ReadOnly);
  server.addUser         ("Uploader",  "123456",     local_root, fineftp::Permission::DirList | fineftp::Permission::DirCreate | fineftp::Permission::FileWrite | fineftp::Permission::FileAppend);
  server.addUser         ("PrivilegeUser", "SuperSecret", local_root, fineftp::Permission::All);

  /**
   * @brief Example FTP Command Callback
   * This callback will be called on every FTP command for every successful 
   * file operation received by the server.
   * The callback should be light-weight and thread-safe, as it will be called 
   * synchronously in the FTP command handling code.
   * Must not capture objects that may be destroyed while sessions still invoke the callback — that is,
   * caller must ensure lifetimes or use weak_ptr inside the callback.
   */
  auto callbackHandler = [](const fineftp::command_type cmd, const std::string& args) {

    switch (cmd) {
        case fineftp::command_type::FTP_CMD_CWD:
            // Handle change working directory command
            std::cout << "Callback: Change working directory command received for path: " << args << std::endl;
            break;
        case fineftp::command_type::FTP_CMD_STOR:
            // Handle store file command
            std::cout << "Callback: Store file command received for file: " << args << std::endl;
            break;
        case fineftp::command_type::FTP_CMD_RNFR:
            // Handle rename from command
            std::cout << "Callback: Rename from command received for file: " << args << std::endl;
            break;
        case fineftp::command_type::FTP_CMD_RNTO:
            // Handle rename to command
            std::cout << "Callback: Rename to command received for file: " << args << std::endl;
            break;
        case fineftp::command_type::FTP_CMD_DELE:
            // Handle delete command
            std::cout << "Callback: Delete command received for file: " << args << std::endl;
            break;
        case fineftp::command_type::FTP_CMD_MKD:
            // Handle make directory command
            std::cout << "Callback: Make directory command received for directory: " << args << std::endl;
            break;
        case fineftp::command_type::FTP_CMD_RMD:
            // Handle remove directory command
            std::cout << "Callback: Remove directory command received for directory: " << args << std::endl;
            break;
        case fineftp::command_type::FTP_CMD_RETR:
            // Handle retrieve file command
            std::cout << "Callback: Retrieve file command received for file: " << args << std::endl;
            break;
        default:
            break;
      }

  };

  /**
   * Setting the FTP command callback function is optional.
   * The calback can only be set before starting the server.
   */
  server.setFtpCommandCallback(callbackHandler);

  // Start the FTP server with 4 threads. More threads will increase the
  // performance with multiple clients, but don't over-do it.
  server.start(4);

  // Prevent the application from exiting immediatelly
  for (;;)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}
