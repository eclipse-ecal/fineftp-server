#pragma once

#include <memory>
#include <string>

#include <fineftp/permissions.h>

namespace fineftp
{

  /** Private implementation */
  class FtpServerImpl;

  /**
   * @brief The fineftp::FtpServer is a simple FTP server library.
   * 
   * Using the FtpServer class is simple:
   *   1. Create an instance
   *   2. Add a user
   *   3. Start the server
   * 
   * Then your server is up and running. In code it will look like this:
   * 
   * @code{.cpp}
   * 
   *   fineftp::FtpServer server(2121);
   *   server.addUserAnonymous("C:\\", fineftp::Permission::All);
   *   server.start();
   * 
   * @endcode
   * 
   */
  class FtpServer
  {
  public:
    /**
     * @brief Creates an FTP Server instance that will listen on the the given control port.
     * 
     * If no port is provided, the default FTP Port 21 is used. If you want to
     * use that port, make sure that your application runs as root.
     * 
     * Instead of using a predefined port, the operating system can choose a
     * free port port. Use port=0, if that behaviour is desired. The chosen port
     * can be determined by with getPort().
     * 
     * @param port: The port to start the FTP server on. Defaults to 21.
     */
    FtpServer(uint16_t port = 21);
    FtpServer(const std::string address, uint16_t port = 21);

    ~FtpServer();

    /**
     * @brief Adds a new user
     * 
     * Adds a new user with a given username, password, local root path and
     * permissions.
     * 
     * Note that the username "anonymous" and "ftp" are reserved, as those are
     * well-known usernames usually used for accessing FTP servers without a
     * password. If adding a user with any of those usernames, the password will
     * be ignored, any user will be able to log in with any password!
     * 
     * The Permissions are flags that are or-ed bit-wise and control what the
     * user will be able to do.
     * 
     * @param username:         The username for login
     * @param password:         The user's password
     * @param local_root_path:  A path to any resource on the local filesystem that will be accessed by the user
     * @param permissions:      A bit-mask of what the user will be able to do.
     * 
     * @return True if adding the user was successful (i.e. it didn't exit already).
     */
    bool addUser(const std::string& username, const std::string& password, const std::string& local_root_path, const Permission permissions);
    
    /**
     * @brief Adds the "anonymous" / "ftp" user that FTP clients use to access FTP servers without password
     * 
     * @param local_root_path:  A path to any resource on the local filesystem that will be accessed by the user
     * @param permissions:      A bit-mask of what the user will be able to do.
     * 
     * @return True if adding the anonymous user was successful (i.e. it didn't exit already).
     */
    bool addUserAnonymous(const std::string& local_root_path, const Permission permissions);

    /**
     * @brief Starts the FTP Server
     * 
     * @param thread_count: The size of the thread pool to use. Must not be 0.
     * 
     * @return True if the Server has been started successfully.
     */
    bool start(size_t thread_count = 1);

    /**
     * @brief Stops the FTP Server
     * 
     * All operations will be cancelled as fast as possible. The clients will
     * not be informed about the shutdown.
     */
    void stop();

    /**
     * @brief Returns the number of currently open connections
     *
     * @return the number of open connections
     */
    int getOpenConnectionCount() const;

    /**
     * @brief Get the control port that the FTP server is listening on
     * 
     * When the server was created with a specific port (not 0), this port will
     * be returned.
     * If the server however was created with port 0, the operating system will
     * choose a free port. This method will return that port.
     * 
     * @return The control port the server is listening on
     */
    uint16_t getPort() const;
    std::string getAddress() const;

    // Non-copyable
    FtpServer(const FtpServer&) = delete;
    FtpServer& operator=(const FtpServer&) = delete;

  private:
    std::unique_ptr<FtpServerImpl> ftp_server_;        /**< Implementation details */
  };

}
