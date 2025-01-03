#include <fineftp/server.h>

#include "server_impl.h"

#include <memory>
#include <string>
#include <cstdint> // uint16_t
#include <cstddef> // size_t
#include <cassert> // assert

#include <fineftp/permissions.h>

namespace fineftp
{
  FtpServer::FtpServer(const std::string& address, const uint16_t port)
    : ftp_server_(std::make_unique<FtpServerImpl>(address, port, std::cout, std::cerr))
  {}

  FtpServer::FtpServer(const std::string& address, const uint16_t port, std::ostream& output, std::ostream& error)
    : ftp_server_(std::make_unique<FtpServerImpl>(address, port, output, error))
  {}

  FtpServer::FtpServer(const std::string& address, std::ostream& output, std::ostream& error)
    : ftp_server_(std::make_unique<FtpServerImpl>(address, 21, output, error))
  {}

  FtpServer::FtpServer(const uint16_t port)
    : FtpServer(std::string("0.0.0.0"), port, std::cout, std::cerr)
  {}

  FtpServer::FtpServer(const uint16_t port, std::ostream& output, std::ostream& error)
    : FtpServer(std::string("0.0.0.0"), port, output, error)
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
}
