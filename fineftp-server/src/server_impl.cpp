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

  FtpServerImpl::FtpServerImpl(const std::string& address, const uint16_t port, std::ostream& output, std::ostream& error)
    : ftp_users_            (output, error)
    , port_                 (port)
    , address_              (address)
    , acceptor_             (io_context_)
    , open_connection_count_(0)
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
    auto ftp_session = std::make_shared<FtpSession>(io_context_, ftp_users_, [this]() { open_connection_count_--; }, output_, error_);

    // set up the acceptor to listen on the tcp port
    asio::error_code make_address_ec;
    const asio::ip::tcp::endpoint endpoint(asio::ip::make_address(address_, make_address_ec), port_);
    if (make_address_ec)
    {
      error_ << "Error creating address from string \"" << address_<< "\": " << make_address_ec.message() << std::endl;
      return false;
    }
    
    {
      asio::error_code ec;
      acceptor_.open(endpoint.protocol(), ec);
      if (ec)
      {
        error_ << "Error opening acceptor: " << ec.message() << std::endl;
        return false;
      }
    }
    
    {
      asio::error_code ec;
      acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
      if (ec)
      {
        error_ << "Error setting reuse_address option: " << ec.message() << std::endl;
        return false;
      }
    }
    
    {
      asio::error_code ec;
      acceptor_.bind(endpoint, ec);
      if (ec)
      {
        error_ << "Error binding acceptor: " << ec.message() << std::endl;
        return false;
      }
    }
    
    {
      asio::error_code ec;
      acceptor_.listen(asio::socket_base::max_listen_connections, ec);
      if (ec)
      {
        error_ << "Error listening on acceptor: " << ec.message() << std::endl;
        return false;
      }
    }
    
#ifndef NDEBUG
    output_ << "FTP Server created." << std::endl << "Listening at address " << acceptor_.local_endpoint().address() << " on port " << acceptor_.local_endpoint().port() << ":" << std::endl;
#endif // NDEBUG

    acceptor_.async_accept(ftp_session->getSocket()
                          , [this, ftp_session](auto ec)
                          {
                            open_connection_count_++;

                            acceptFtpSession(ftp_session, ec);
                          });

    for (size_t i = 0; i < thread_count; i++)
    {
      thread_pool_.emplace_back([this] {io_context_.run(); });
    }
    
    return true;
  }

  void FtpServerImpl::stop()
  {
    io_context_.stop();
    for (std::thread& thread : thread_pool_)
    {
      thread.join();
    }
    thread_pool_.clear();
  }

  void FtpServerImpl::acceptFtpSession(const std::shared_ptr<FtpSession>& ftp_session, asio::error_code const& error)
  {
    if (error)
    {
#ifndef NDEBUG
      error_ << "Error handling connection: " << error.message() << std::endl;
#endif
      return;
    }

#ifndef NDEBUG
    output_ << "FTP Client connected: " << ftp_session->getSocket().remote_endpoint().address().to_string() << ":" << ftp_session->getSocket().remote_endpoint().port() << std::endl;
#endif

    ftp_session->start();

    auto new_session = std::make_shared<FtpSession>(io_context_, ftp_users_, [this]() { open_connection_count_--; }, output_, error_);

    acceptor_.async_accept(new_session->getSocket()
                          , [this, new_session](auto ec)
                          {
                            open_connection_count_++;
                            acceptFtpSession(new_session, ec);
                          });
  }

  int FtpServerImpl::getOpenConnectionCount()
  {
    return open_connection_count_;
  }

  uint16_t FtpServerImpl::getPort()
  {
    return acceptor_.local_endpoint().port();
  }

  std::string FtpServerImpl::getAddress()
  {
    return acceptor_.local_endpoint().address().to_string();
  }
}
