#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp> // IWYU pragma: keep

#include <fineftp/permissions.h>
#include <ftp_session.h>

#include <user_database.h>

namespace fineftp
{
  class FtpServerImpl
  {
  public:
    FtpServerImpl(const std::string& address, uint16_t port, std::ostream& output, std::ostream& error);

    // Copy (disabled)
    FtpServerImpl(const FtpServerImpl&)            = delete;
    FtpServerImpl& operator=(const FtpServerImpl&) = delete;

    // Move (disabled, as we are storing the this pointer in lambda captures)
    FtpServerImpl& operator=(FtpServerImpl&&)      = delete;
    FtpServerImpl(FtpServerImpl&&)                 = delete;

    ~FtpServerImpl();

    bool addUser(const std::string& username, const std::string& password, const std::string& local_root_path, Permission permissions);
    bool addUserAnonymous(const std::string& local_root_path, Permission permissions);

    bool start(size_t thread_count = 1);

    void stop();

    int getOpenConnectionCount();

    uint16_t getPort(); 

    std::string getAddress();

  private:
    void acceptFtpSession(const std::shared_ptr<FtpSession>& ftp_session, asio::error_code const& error);

  private:
    UserDatabase   ftp_users_;

    const uint16_t port_;
    const std::string address_;

    std::vector<std::thread> thread_pool_;
    asio::io_context         io_context_;
    asio::ip::tcp::acceptor  acceptor_;

    std::atomic<int> open_connection_count_;

    std::ostream& output_;  /* Normal output log */
    std::ostream& error_;   /* Error output log */
  };
}
