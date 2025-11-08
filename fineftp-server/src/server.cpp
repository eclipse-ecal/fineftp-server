#include <fineftp/server.h>

#include "server_impl.h"

#include <cassert> // assert
#include <cstddef> // size_t
#include <cstdint> // uint16_t
#include <memory>
#include <ostream>
#include <string>

#include <fineftp/permissions.h>

namespace fineftp
{
  FtpServer::FtpServer(const std::string& address, const uint16_t port, std::ostream& output, std::ostream& error, FtpCommandCallback ftp_command_callback)
    : ftp_server_(std::make_unique<FtpServerImpl>(address, port, output, error, ftp_command_callback))
  {}

  FtpServer::FtpServer(const std::string& address, const uint16_t port, FtpCommandCallback ftp_command_callback)
    : FtpServer(address, port, std::cout, std::cerr, ftp_command_callback)
  {}

  FtpServer::FtpServer(const uint16_t port, FtpCommandCallback ftp_command_callback)
    : FtpServer(std::string("0.0.0.0"), port, std::cout, std::cerr, ftp_command_callback)
  {}

  // Move
  FtpServer::FtpServer(FtpServer&&) noexcept                = default;
  FtpServer& FtpServer::operator=(FtpServer&&) noexcept     = default;

  FtpServer::~FtpServer() = default;

  bool FtpServer::addUser(const std::string& username, const std::string& password, const std::string& local_root_path, const Permission permissions)
  {
    return ftp_server_->addUser(username, password, local_root_path, permissions);
  }

  bool FtpServer::addUserAnonymous(const std::string& local_root_path, const Permission permissions)
  {
    return ftp_server_->addUserAnonymous(local_root_path, permissions);
  }

  bool FtpServer::start(size_t thread_count)
  {
    assert(thread_count > 0);
    return ftp_server_->start(thread_count);
  }

  void FtpServer::stop()
  {
    ftp_server_->stop();
  }

  int FtpServer::getOpenConnectionCount() const
  {
    return ftp_server_->getOpenConnectionCount();
  }

  uint16_t FtpServer::getPort() const
  {
    return ftp_server_->getPort();
  }

  std::string FtpServer::getAddress() const
  {
    return ftp_server_->getAddress();
  }

  void FtpServer::setFtpCommandCallback(FtpCommandCallback callback)
  {
    // Forward to implementation
    ftp_server_->setFtpCommandCallback(std::move(callback));
  }
}
