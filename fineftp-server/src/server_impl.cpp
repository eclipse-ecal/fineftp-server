#include "server_impl.h"

#include "ftp_session.h"

#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <cstdint>
#include <cstddef>

#include <fineftp/permissions.h>

#include <asio.hpp> // IWYU pragma: keep

namespace fineftp
{
  std::shared_ptr<FtpServerImpl> FtpServerImpl::create(const std::string& address, uint16_t port, std::ostream& output, std::ostream& error)
  {
    return std::shared_ptr<FtpServerImpl>(new FtpServerImpl(address, port, output, error));
  }

  FtpServerImpl::FtpServerImpl(const std::string& address, const uint16_t port, std::ostream& output, std::ostream& error)
    : ftp_users_            (output, error)
    , port_                 (port)
    , address_              (address)
    , is_stopped_           (false)
    , acceptor_             (io_context_)
    , output_               (output)
    , error_                (error)
  {}

  FtpServerImpl::~FtpServerImpl()
  {
    stop();
  }

  bool FtpServerImpl::addUser(const std::string& username, const std::string& password, const std::string& local_root_path, const Permission permissions)
  {
    return ftp_users_.addUser(username, password, local_root_path, permissions);
  }

  bool FtpServerImpl::addUserAnonymous(const std::string& local_root_path, const Permission permissions)
  {
    return ftp_users_.addUser("anonymous", "", local_root_path, permissions);
  }

  bool FtpServerImpl::start(size_t thread_count)
  {
    // set up the acceptor to listen on the tcp port
    asio::error_code make_address_ec;
    const asio::ip::tcp::endpoint endpoint(asio::ip::make_address(address_, make_address_ec), port_);
    if (make_address_ec)
    {
      error_ << "Error creating address from string \"" << address_<< "\": " << make_address_ec.message() << std::endl;
      return false;
    }
    
    {
      const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);

      asio::error_code ec;
      acceptor_.open(endpoint.protocol(), ec);
      if (ec)
      {
        error_ << "Error opening acceptor: " << ec.message() << std::endl;
        return false;
      }
    }

    // TODO: Add the code again to use reuse_address option
    {
      const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);

      asio::error_code ec;
      acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
      if (ec)
      {
        error_ << "Error setting reuse_address option: " << ec.message() << std::endl;
        return false;
      }
    }
    
    {
      const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);

      asio::error_code ec;
      acceptor_.bind(endpoint, ec);
      if (ec)
      {
        error_ << "Error binding acceptor: " << ec.message() << std::endl;
        return false;
      }
    }
    
    {
      const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);

      asio::error_code ec;
      acceptor_.listen(asio::socket_base::max_listen_connections, ec);
      if (ec)
      {
        error_ << "Error listening on acceptor: " << ec.message() << std::endl;
        return false;
      }
    }
    
#ifndef NDEBUG
    {
      const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);
      output_ << "FTP Server created." << std::endl << "Listening at address " << acceptor_.local_endpoint().address() << " on port " << acceptor_.local_endpoint().port() << ":" << std::endl;
    }
#endif // NDEBUG

    waitForNextFtpSession();

    for (size_t i = 0; i < thread_count; i++)
    {
      thread_pool_.emplace_back([this] {io_context_.run(); });
    }
    
    return true;
  }

  void FtpServerImpl::stop()
  {
    is_stopped_ = true;

    // Prevent new sessions from being created
    {
      const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);

      // Close acceptor, if necessary
      if (acceptor_.is_open())
      {
        asio::error_code ec;
        acceptor_.close(ec); // NOLINT(bugprone-unused-return-value) -> We already get the return value  rom the ec parameter
      }
    }

    // Stop all sessions
    {
      const std::lock_guard<std::mutex> session_list_lock(session_list_mutex_);
      for(const auto& session_pair : session_list_)
      {
        const auto session_sharedptr = session_pair.second.lock();
        if (session_sharedptr)
          session_sharedptr->stop();
      }
    }

    // Wait for the io_context to run out of work by joining all threads
    {
      for (std::thread& thread : thread_pool_)
      {
        thread.join();
      }
      thread_pool_.clear();
    }
  }

  void FtpServerImpl::waitForNextFtpSession()
  {
    if (is_stopped_)
    {
      return;
    }

    auto shutdown_callback = [weak_me = std::weak_ptr<FtpServerImpl>(shared_from_this())](FtpSession* session_to_delete)
                              {
                                if (auto me = weak_me.lock())
                                {
                                  // Lock session_list
                                  const std::lock_guard<std::mutex> session_list_lock(me->session_list_mutex_);

                                  // Remove session_to_delete from the session_list
                                  me->session_list_.erase(session_to_delete);
                                }
                              };

    auto new_ftp_session = std::make_shared<FtpSession>(io_context_, ftp_users_, shutdown_callback, output_, error_);

    {
      const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);
      acceptor_.async_accept(new_ftp_session->getSocket()
                            , [weak_me = std::weak_ptr<FtpServerImpl>(shared_from_this()), new_ftp_session](asio::error_code ec)
                              {
                                // Lock the shared pointer to this. May be a nullptr, so we need to check!!!
                                auto me = weak_me.lock();

                                // Before even checking the error code, check if the server has been stopped
                                if (!me || (me->is_stopped_))
                                {
                                  return;
                                }

                                // Check error code
                                if (ec)
                                {
                                  if (ec != asio::error::operation_aborted)
                                  {
                                    std::cerr << "Error accepting connection: " << ec.message() << std::endl;
                                  }
                                  return;
                                }

#ifndef NDEBUG
                                me->output_ << "FTP Client connected: " << new_ftp_session->getSocket().remote_endpoint().address().to_string() << ":" << new_ftp_session->getSocket().remote_endpoint().port() << std::endl;
#endif
                                // TODO: review if this is thread safe, if right here the ftp server is shut down and the acceptor is closed. I think, that then the session will still be added to the list of open sessions and kept open.

                                if (me)
                                {
                                  const std::lock_guard<std::mutex> session_list_lock(me->session_list_mutex_);
                                  me->session_list_.emplace(new_ftp_session.get(), std::weak_ptr<FtpSession>(new_ftp_session)); // Add the session to the list of open sessions

                                  new_ftp_session->start();

                                  me->waitForNextFtpSession();
                                }
                              });
    }
  }

  int FtpServerImpl::getOpenConnectionCount()
  {
    const std::lock_guard<std::mutex> session_list_lock(session_list_mutex_);
    return static_cast<int>(session_list_.size());
  }

  uint16_t FtpServerImpl::getPort()
  {
    const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);
    return acceptor_.local_endpoint().port();
  }

  std::string FtpServerImpl::getAddress()
  {
    const std::lock_guard<std::mutex> acceptor_lock(acceptor_mutex_);
    return acceptor_.local_endpoint().address().to_string();
  }
}
