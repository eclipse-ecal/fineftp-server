#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp> // IWYU pragma: keep

#include <fineftp/permissions.h>
#include <ftp_session.h>

#include <user_database.h>

namespace fineftp
{
  class FtpServerImpl : public std::enable_shared_from_this<FtpServerImpl>
  {
  public:
    FtpServerImpl(const std::string& address, uint16_t port, std::ostream& output, std::ostream& error);

  private:
    FtpServerImpl(const std::string& address, uint16_t port);

  public:
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
    void waitForNextFtpSession();

  private:
    UserDatabase      ftp_users_;

    const uint16_t    port_;                                                    //! Port used on creation. May be 0. In that case, the OS will choose a port. Thus, this variable should not be used for getting the actual port.
    const std::string address_;                                                 //! Address used on creation. The acceptor is bound to that address.
    std::atomic<bool> is_stopped_;                                              //! Tells whether the server has been stopped. When stopped, it will refuse to accept new connections.

    std::vector<std::thread> thread_pool_;
    asio::io_service         io_service_;

    mutable std::mutex       acceptor_mutex_;                                   //!< Mutex protecting the acceptor. That is necessary, as the user may stop the server (and therefore close the acceptor) from another thread.
    asio::ip::tcp::acceptor  acceptor_;                                         //!< The acceptor waiting for new sessions

    mutable std::mutex                                session_list_mutex_;      //!< Mutex protecting the list of current sessions
    std::map<FtpSession*, std::weak_ptr<FtpSession>>  session_list_;            //!< List of sessions. Only store weak_ptr, so the sessions can delete themselves. This list is used to stop sessions and count connections. The raw pointers are used to identify the entry even while a session is currently in the destructor, as there is no cross-plattform way of obtaining the raw pointer from the weak pointer in that case.

    std::ostream& output_;  /* Normal output log */
    std::ostream& error_;   /* Error output log */
  };
}
