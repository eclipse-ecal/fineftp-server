#include "ftp_session.h"

#include <asio.hpp>

#include <algorithm>
#include <cassert> // assert
#include <cctype>  // std::iscntrl, toupper
#include <chrono>   // IWYU pragma: keep (it is used for special preprocessor defines)
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <vector>

#include <file_man.h>

#include "filesystem.h"
#include "ftp_message.h"
#include "user_database.h"
#include <fineftp/permissions.h>

#include <sys/stat.h>

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include "win_str_convert.h"
#else
  #include <unistd.h>
#endif // WIN32


namespace fineftp
{

  FtpSession::FtpSession(asio::io_context& io_context, const UserDatabase& user_database, const std::function<void()>& completion_handler, std::ostream& output, std::ostream& error)
    : completion_handler_   (completion_handler)
    , user_database_        (user_database)
    , io_context_           (io_context)
    , command_strand_       (io_context)
    , command_socket_       (io_context)
    , data_type_binary_     (false)
    , shutdown_requested_   (false)
    , ftp_working_directory_("/")
    , data_acceptor_        (io_context)
    , data_socket_strand_   (io_context)
    , timer_                (io_context)
    , output_(output)
    , error_(error)
  {
  }

  FtpSession::~FtpSession()
  {
#ifndef NDEBUG
    output_ << "Ftp Session shutting down" << std::endl;
#endif // !NDEBUG

    {
      // Properly close command socket.
      // When the FtpSession is being destroyed, there are no std::shared_ptr's referring to
      // it and hence no possibility of race conditions on command_socket_.
      asio::error_code ec;
      command_socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
      command_socket_.close(ec);
    }

    // When the FtpSession is being destroyed, there are no std::shared_ptr's referring to
    // it and hence no possibility of race conditions on data_socket_weak_ptr_.
    auto data_socket = data_socket_weakptr_.lock();
    if (data_socket)
    {
      // Properly close data socket
      asio::error_code ec;
      data_socket->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
      data_socket->close(ec);
    }

    completion_handler_();
  }

  void FtpSession::start()
  {
    asio::error_code ec;
    command_socket_.set_option(asio::ip::tcp::no_delay(true), ec);
    if (ec) error_ << "Unable to set socket option tcp::no_delay: " << ec.message() << std::endl;

    asio::post(command_strand_, [me = shared_from_this()]() { me->readFtpCommand(); });
    sendFtpMessage(FtpMessage(FtpReplyCode::SERVICE_READY_FOR_NEW_USER, "Welcome to fineFTP Server"));
  }

  asio::ip::tcp::socket& FtpSession::getSocket()
  {
    return command_socket_;
  }

  void FtpSession::sendFtpMessage(const FtpMessage& message)
  {
    sendRawFtpMessage(message.str());
  }
  void FtpSession::sendFtpMessage(FtpReplyCode code, const std::string& message)
  {
    sendFtpMessage(FtpMessage(code, message));
  }

  void FtpSession::sendRawFtpMessage(const std::string& raw_message)
  {
    asio::post(command_strand_, [me = shared_from_this(), raw_message]()
                         {
                           const bool write_in_progress = !me->command_output_queue_.empty();
                           me->command_output_queue_.push_back(raw_message);
                           if (!write_in_progress)
                           {
                             me->startSendingMessages();
                           }
                         });
  }

  void FtpSession::startSendingMessages()
  {
#ifndef NDEBUG
    output_ << "FTP >> " << command_output_queue_.front() << std::endl;
#endif

    asio::async_write(command_socket_
                    , asio::buffer(command_output_queue_.front())
                    , command_strand_.wrap(
                      [me = shared_from_this()](asio::error_code ec, std::size_t /*bytes_to_transfer*/)
                      {
                        if (!ec)
                        {
                          me->command_output_queue_.pop_front();

                          // Handle the QUIT command
                          if (me->shutdown_requested_)
                          {
                            // Properly close command socket
                            asio::error_code ec_;
                            me->command_socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec_);
                            me->command_socket_.close(ec_);
                            return;
                          }
              
                          if (!me->command_output_queue_.empty())
                          {
                            me->startSendingMessages();
                          }
                        }
                        else
                        {
                          me->error_ << "Command write error for message " << me->command_output_queue_.front() << ec.message() << std::endl;
                        }
                      }
                    ));
  }

  void FtpSession::readFtpCommand()
  {
    asio::async_read_until(command_socket_, command_input_stream_, "\r\n",
                        command_strand_.wrap([me = shared_from_this()](asio::error_code ec, std::size_t length)
                        {
                          if (ec)
                          {
                            if (ec != asio::error::eof)
                            {
                              me->error_ << "read_until error: " << ec.message() << std::endl;
                            }
#ifndef NDEBUG
                            else
                            {
                              me->output_ << "Control connection closed by client." << std::endl;
                            }
#endif // !NDEBUG
                            // Close the data connection, if it is open
                            {
                              asio::error_code ec_;
                              me->data_acceptor_.close(ec_);
                            }

                            asio::post(me->data_socket_strand_, [me]()
                            {
                              auto data_socket = me->data_socket_weakptr_.lock();
                              if (data_socket)
                              { 
                                asio::error_code ec_;
                                data_socket->close(ec_);
                              }
                            });

                            return;
                          }

                          std::istream stream (&(me->command_input_stream_));
                          std::string packet_string(length - 2, ' ');
                          stream.read(&packet_string[0], length - 2);  // NOLINT(readability-container-data-pointer) Reason: I need a non-const pointer here, As I am directly reading into the buffer, but .data() returns a const pointer. I don't consider a const_cast to be better. Since C++11 this is safe, as strings are stored in contiguous memeory.

                          stream.ignore(2); // Remove the "\r\n"
#ifndef NDEBUG
                          me->output_ << "FTP << " << packet_string << std::endl;
#endif

                          me->handleFtpCommand(packet_string);
                        }));
  }

  void FtpSession::handleFtpCommand(const std::string& command)
  {
    std::string ftp_command;
    std::string parameters;

    const size_t space_index = command.find_first_of(' ');

    ftp_command = command.substr(0, space_index);
    std::transform(ftp_command.begin(), ftp_command.end(), ftp_command.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });

    if (space_index != std::string::npos)
    {
      parameters = command.substr(space_index + 1, std::string::npos);
    }


    const std::map<std::string, std::function<void(std::string)>> command_map {
      // Access control commands
      { "USER", std::bind(&FtpSession::handleFtpCommandUSER, this, std::placeholders::_1) },
      { "PASS", std::bind(&FtpSession::handleFtpCommandPASS, this, std::placeholders::_1) },
      { "ACCT", std::bind(&FtpSession::handleFtpCommandACCT, this, std::placeholders::_1) },
      { "CWD",  std::bind(&FtpSession::handleFtpCommandCWD,  this, std::placeholders::_1) },
      { "CDUP", std::bind(&FtpSession::handleFtpCommandCDUP, this, std::placeholders::_1) },
      { "REIN", std::bind(&FtpSession::handleFtpCommandREIN, this, std::placeholders::_1) },
      { "QUIT", std::bind(&FtpSession::handleFtpCommandQUIT, this, std::placeholders::_1) },

      // Transfer parameter commands
      { "PORT", std::bind(&FtpSession::handleFtpCommandPORT, this, std::placeholders::_1) },
      { "PASV", std::bind(&FtpSession::handleFtpCommandPASV, this, std::placeholders::_1) },
      { "TYPE", std::bind(&FtpSession::handleFtpCommandTYPE, this, std::placeholders::_1) },
      { "STRU", std::bind(&FtpSession::handleFtpCommandSTRU, this, std::placeholders::_1) },
      { "MODE", std::bind(&FtpSession::handleFtpCommandMODE, this, std::placeholders::_1) },

      // Ftp service commands
      { "RETR", std::bind(&FtpSession::handleFtpCommandRETR, this, std::placeholders::_1) },
      { "STOR", std::bind(&FtpSession::handleFtpCommandSTOR, this, std::placeholders::_1) },
      { "STOU", std::bind(&FtpSession::handleFtpCommandSTOU, this, std::placeholders::_1) },
      { "APPE", std::bind(&FtpSession::handleFtpCommandAPPE, this, std::placeholders::_1) },
      { "ALLO", std::bind(&FtpSession::handleFtpCommandALLO, this, std::placeholders::_1) },
      { "REST", std::bind(&FtpSession::handleFtpCommandREST, this, std::placeholders::_1) },
      { "RNFR", std::bind(&FtpSession::handleFtpCommandRNFR, this, std::placeholders::_1) },
      { "RNTO", std::bind(&FtpSession::handleFtpCommandRNTO, this, std::placeholders::_1) },
      { "ABOR", std::bind(&FtpSession::handleFtpCommandABOR, this, std::placeholders::_1) },
      { "DELE", std::bind(&FtpSession::handleFtpCommandDELE, this, std::placeholders::_1) },
      { "RMD",  std::bind(&FtpSession::handleFtpCommandRMD,  this, std::placeholders::_1) },
      { "MKD",  std::bind(&FtpSession::handleFtpCommandMKD,  this, std::placeholders::_1) },
      { "PWD",  std::bind(&FtpSession::handleFtpCommandPWD,  this, std::placeholders::_1) },
      { "LIST", std::bind(&FtpSession::handleFtpCommandLIST, this, std::placeholders::_1) },
      { "NLST", std::bind(&FtpSession::handleFtpCommandNLST, this, std::placeholders::_1) },
      { "SITE", std::bind(&FtpSession::handleFtpCommandSITE, this, std::placeholders::_1) },
      { "SYST", std::bind(&FtpSession::handleFtpCommandSYST, this, std::placeholders::_1) },
      { "STAT", std::bind(&FtpSession::handleFtpCommandSTAT, this, std::placeholders::_1) },
      { "HELP", std::bind(&FtpSession::handleFtpCommandHELP, this, std::placeholders::_1) },
      { "NOOP", std::bind(&FtpSession::handleFtpCommandNOOP, this, std::placeholders::_1) },

      // Modern FTP Commands
      { "FEAT", std::bind(&FtpSession::handleFtpCommandFEAT, this, std::placeholders::_1) },
      { "OPTS", std::bind(&FtpSession::handleFtpCommandOPTS, this, std::placeholders::_1) },
      { "SIZE", std::bind(&FtpSession::handleFtpCommandSIZE, this, std::placeholders::_1) },
    };

    auto command_it = command_map.find(ftp_command);
    if (command_it != command_map.end())
    {
      command_it->second(parameters);
      last_command_ = ftp_command;
    }
    else
    {
      sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "Unrecognized command");
    }

    // Wait for next command
    if (!shutdown_requested_)
    {
      readFtpCommand();
    }
  }

  ////////////////////////////////////////////////////////
  // FTP Commands
  ////////////////////////////////////////////////////////


  // Access control commands

  void FtpSession::handleFtpCommandUSER(const std::string& param)
  {
    logged_in_user_        = nullptr;
    username_for_login_    = param;
    ftp_working_directory_ = "/";

    if (param.empty())
    {
      sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_PARAMETERS, "Please provide username");
      return;
    }
    else
    {
      sendFtpMessage(FtpReplyCode::USER_NAME_OK, "Please enter password");
      return;
    }
  }

  void FtpSession::handleFtpCommandPASS(const std::string& param)
  {
    if (last_command_ != "USER")
    {
      sendFtpMessage(FtpReplyCode::COMMANDS_BAD_SEQUENCE, "Please specify username first");
      return;
    }
    else
    {
      auto user = user_database_.getUser(username_for_login_, param);
      if (user)
      {
        logged_in_user_ = user;
        sendFtpMessage(FtpReplyCode::USER_LOGGED_IN, "Login successful");
        return;
      }
      else
      {
        sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN, "Failed to log in");
        return;
      }
    }
  }

  void FtpSession::handleFtpCommandACCT(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "Unsupported command");
  }

  void FtpSession::handleFtpCommandCWD(const std::string& param)
  {
      sendFtpMessage(executeCWD(param));
  }

  void FtpSession::handleFtpCommandCDUP(const std::string& /*param*/)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::DirList) == 0) 
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
      return;
    }

    if (ftp_working_directory_ != "/")
    {
      // Only CDUP when we are not already at the root directory
      auto cwd_reply = executeCWD("..");
      if (cwd_reply.replyCode() == FtpReplyCode::FILE_ACTION_COMPLETED)
      {
        // The CWD returns FILE_ACTION_COMPLETED on success, while CDUP returns COMMAND_OK on success.
        sendFtpMessage(FtpReplyCode::COMMAND_OK, cwd_reply.message());
        return;
      }
      else
      {
        sendFtpMessage(cwd_reply);
        return;
      }
    }
    else
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Already at root directory");
      return;
    }
  }
  
  void FtpSession::handleFtpCommandREIN(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::COMMAND_NOT_IMPLEMENTED, "Unsupported command");
  }

  void FtpSession::handleFtpCommandQUIT(const std::string& /*param*/)
  {
    logged_in_user_ = nullptr;
    shutdown_requested_ = true; // This will cause the control connection to be closed after the next message
    sendFtpMessage(FtpReplyCode::SERVICE_CLOSING_CONTROL_CONNECTION, "Connection shutting down");
  }

  // Transfer parameter commands

  void FtpSession::handleFtpCommandPORT(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "FTP active mode is not supported by this server");
  }

  void FtpSession::handleFtpCommandPASV(const std::string& /*param*/)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }

    if (data_acceptor_.is_open())
    {
      asio::error_code ec;
      data_acceptor_.close(ec);
      if (ec)
      {
        error_ << "Error closing data acceptor: " << ec.message() << std::endl;
      }
    }

    const asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 0);

    {
      asio::error_code ec;
      data_acceptor_.open(endpoint.protocol(), ec);
      if (ec)
      {
        error_ << "Error opening data acceptor: " << ec.message() << std::endl;
        sendFtpMessage(FtpReplyCode::SERVICE_NOT_AVAILABLE, "Failed to enter passive mode.");
        return;
      }
    }
    {
      asio::error_code ec;
      data_acceptor_.bind(endpoint, ec);
      if (ec)
      {
        error_ << "Error binding data acceptor: " << ec.message() << std::endl;
        sendFtpMessage(FtpReplyCode::SERVICE_NOT_AVAILABLE, "Failed to enter passive mode.");
        return;
      }
    }
    {
      asio::error_code ec;
      data_acceptor_.listen(asio::socket_base::max_listen_connections, ec);
      if (ec)
      {
        error_ << "Error listening on data acceptor: " << ec.message() << std::endl;
        sendFtpMessage(FtpReplyCode::SERVICE_NOT_AVAILABLE, "Failed to enter passive mode.");
        return;
      }
    }

    // Split address and port into bytes and get the port the OS chose for us
    auto ip_bytes = command_socket_.local_endpoint().address().to_v4().to_bytes();
    auto port     = data_acceptor_.local_endpoint().port();

    // Form reply string
    std::stringstream stream;
    stream << "(";
    for (const auto byte : ip_bytes)
    {
      stream << static_cast<unsigned int>(byte) << ",";
    }
    stream << ((port >> 8) & 0xff) << "," << (port & 0xff) << ")";

    sendFtpMessage(FtpReplyCode::ENTERING_PASSIVE_MODE, "Entering passive mode " + stream.str());
  }

  void FtpSession::handleFtpCommandTYPE(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }

    if (param == "A")
    {
      data_type_binary_ = false;
      // TODO: The ASCII mode currently does not work as RFC 959 demands it. It
      // should perform line ending conversion, which it doesn't. But as we are
      // living in the 21st centry, nobody should use ASCII mode anyways.
      sendFtpMessage(FtpReplyCode::COMMAND_OK, "Switching to ASCII mode");
      return;
    }
    else if (param == "I")
    {
      data_type_binary_ = true;
      sendFtpMessage(FtpReplyCode::COMMAND_OK, "Switching to binary mode");
      return;
    }
    else
    {
      sendFtpMessage(FtpReplyCode::COMMAND_NOT_IMPLEMENTED_FOR_PARAMETER, "Unknown or unsupported type");
      return;
    }
  }

  void FtpSession::handleFtpCommandSTRU(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "Unsupported command");
  }

  void FtpSession::handleFtpCommandMODE(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "Unsupported command");
  }

  // Ftp service commands
  void FtpSession::handleFtpCommandRETR(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::FileRead) == 0)
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN,              "Permission denied");
      return;
    }
    if (!data_acceptor_.is_open())
    {
      sendFtpMessage(FtpReplyCode::ERROR_OPENING_DATA_CONNECTION, "Error opening data connection");
      return;
    }

    const std::string local_path = toLocalPath(param);
    
#if defined(WIN32) && !defined(__GNUG__)
    const auto file = ReadableFile::get(StrConvert::Utf8ToWide(local_path));
#else
    const auto file = ReadableFile::get(local_path);
#endif

    if (!file)
    {
      sendFtpMessage(FtpReplyCode::ACTION_ABORTED_LOCAL_ERROR, "Error opening file for transfer");
      return;
    }

    sendFtpMessage(FtpReplyCode::FILE_STATUS_OK_OPENING_DATA_CONNECTION, "Sending file");
    sendFile(file);
  }

  void FtpSession::handleFtpCommandSIZE(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN, "Not logged in");
      return;
    }

    // We allow the SIZE command both for FileRead and DirList permissions.
    if (static_cast<int>(logged_in_user_->permissions_ & (Permission::FileRead | Permission::DirList)) == 0)
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
      return;
    }

    const std::string local_path = toLocalPath(param);

    const std::ios::openmode open_mode =
       std::ios::ate | (data_type_binary_ ? (std::ios::in | std::ios::binary) : (std::ios::in));
    std::fstream::pos_type file_size;
    {
#if defined(WIN32) && !defined(__GNUG__)
      std::ifstream file(StrConvert::Utf8ToWide(local_path), open_mode);
#else
      std::ifstream file(local_path, open_mode);
#endif

      if (!file.good())
      {
        sendFtpMessage(FtpReplyCode::ACTION_ABORTED_LOCAL_ERROR, "Error opening file for size retrieval");
        return;
      }

      // RFC 3659 actually states that the returned size should depend on the STRU, MODE, and TYPE and that
      // the returned size should be exact. We don't comply with this here. The size returned is the
      // size for TYPE=I.
      file_size = file.tellg();
      if (std::fstream::pos_type(-1) == file_size)
      {
        sendFtpMessage(FtpReplyCode::ACTION_ABORTED_LOCAL_ERROR, "Error getting file size");
        return;
      }
    }

    // Form reply string
    std::stringstream rep;
    rep << file_size;

    sendFtpMessage(FtpReplyCode::FILE_STATUS, rep.str());
  }

  void FtpSession::handleFtpCommandSTOR(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }

    // TODO: the ACTION_NOT_TAKEN reply is not RCF 959 conform. Apparently in
    // 1985 nobody anticipated that you might not want anybody uploading files
    // to your server. We use the return code anyways, as the popular FileZilla
    // Server also returns that code as "Permission denied"
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::FileWrite) == 0)
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN,              "Permission denied");
      return;
    }
    if (!data_acceptor_.is_open())
    {
      sendFtpMessage(FtpReplyCode::ERROR_OPENING_DATA_CONNECTION, "Error opening data connection");
      return;
    }

    const std::string local_path = toLocalPath(param);

    auto existing_file_filestatus = Filesystem::FileStatus(local_path);
    if (existing_file_filestatus.isOk())
    {
      if ((existing_file_filestatus.type() == Filesystem::FileType::RegularFile)
        && (static_cast<int>(logged_in_user_->permissions_ & Permission::FileDelete) == 0))
      {
        sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN_FILENAME_NOT_ALLOWED, "File already exists. Permission denied to overwrite file.");
        return;
      }
      else if (existing_file_filestatus.type() == Filesystem::FileType::Dir)
      {
        sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN_FILENAME_NOT_ALLOWED, "Cannot create file. A directory with that name already exists.");
        return;
      }
    }

    const std::ios::openmode open_mode = (data_type_binary_ ? std::ios::binary : std::ios::openmode{});
    const std::shared_ptr<WriteableFile> file = std::make_shared<WriteableFile>(local_path, open_mode);

    if (!file->good())
    {
#ifdef WIN32
      sendFtpMessage(FtpReplyCode::ACTION_ABORTED_LOCAL_ERROR, "Error opening file for transfer: " + GetLastErrorStr());
#else
      sendFtpMessage(FtpReplyCode::ACTION_ABORTED_LOCAL_ERROR, "Error opening file for transfer");
#endif // WIN32

      return;
    }

    sendFtpMessage(FtpReplyCode::FILE_STATUS_OK_OPENING_DATA_CONNECTION, "Receiving file");
    receiveFile(file);
  }

  void FtpSession::handleFtpCommandSTOU(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "Command not implemented");
  }

  void FtpSession::handleFtpCommandAPPE(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }

    // Check whether the file exists. This determines whether we need Append or Write Permissions
    const std::string local_path = toLocalPath(param);
    auto existing_file_filestatus = Filesystem::FileStatus(local_path);

    if (existing_file_filestatus.isOk())
    {
      // The file does exist => we need Append Permissions
      if (static_cast<int>(logged_in_user_->permissions_ & Permission::FileAppend) == 0)
      {
        sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
        return;
      }

      // Return error message for anything that is not a file
      if(existing_file_filestatus.type() != Filesystem::FileType::RegularFile)
      {
        sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Pathname is not a file");
        return;
      }
    }
    else
    {
      // The file does not exist => we need Write Permissions
      if (static_cast<int>(logged_in_user_->permissions_ & Permission::FileWrite) == 0)
      {
        sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
        return;
      }
    }

    if (!data_acceptor_.is_open())
    {
      sendFtpMessage(FtpReplyCode::ERROR_OPENING_DATA_CONNECTION, "Error opening data connection");
      return;
    }

    // If the file did not exist, we create a new one. Otherwise, we open it in append mode.
    std::ios::openmode open_mode{};
    if (existing_file_filestatus.isOk())
      open_mode = (data_type_binary_ ? (std::ios::app | std::ios::binary) : (std::ios::app));
    else
      open_mode = (data_type_binary_ ? (std::ios::binary) : std::ios::openmode{});

    const std::shared_ptr<WriteableFile> file = std::make_shared<WriteableFile>(local_path, open_mode);

    if (!file->good())
    {
#ifdef WIN32
      sendFtpMessage(FtpReplyCode::ACTION_ABORTED_LOCAL_ERROR, "Error opening file for transfer: " + GetLastErrorStr());
#else
      sendFtpMessage(FtpReplyCode::ACTION_ABORTED_LOCAL_ERROR, "Error opening file for transfer");
#endif // WIN32
      return;
    }

    sendFtpMessage(FtpReplyCode::FILE_STATUS_OK_OPENING_DATA_CONNECTION, "Receiving file");
    receiveFile(file);
  }

  void FtpSession::handleFtpCommandALLO(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "Command not implemented");
  }

  void FtpSession::handleFtpCommandREST(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::COMMAND_NOT_IMPLEMENTED, "Command not implemented");
  }

  void FtpSession::handleFtpCommandRNFR(const std::string& param)
  {
    rename_from_path_.clear();

    auto is_renamable_error = checkIfPathIsRenamable(param);

    if (is_renamable_error.replyCode() == FtpReplyCode::COMMAND_OK)
    {
      rename_from_path_ = param;
      sendFtpMessage(FtpReplyCode::FILE_ACTION_NEEDS_FURTHER_INFO, "Enter target name");
      return;
    }
    else
    {
      sendFtpMessage(is_renamable_error);
      return;
    }
  }

  void FtpSession::handleFtpCommandRNTO(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }

    if (last_command_ != "RNFR" || rename_from_path_.empty())
    {
      sendFtpMessage(FtpReplyCode::COMMANDS_BAD_SEQUENCE, "Please specify target file first");
      return;
    }

    if (param.empty())
    {
      sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_PARAMETERS, "No target name given");
      return;
    }

    // TODO: returning neiher FILE_ACTION_NOT_TAKEN nor ACTION_NOT_TAKEN are
    // RFC 959 conform. Aoarently back in 1985 it was assumed that the RNTO
    //command will always succeed, as long as you enter a valid target file
    // name. Thus we use the two return codes anyways, the popular FileZilla
    // FTP Server uses those as well.
    auto is_renamable_error = checkIfPathIsRenamable(rename_from_path_);

    if (is_renamable_error.replyCode() == FtpReplyCode::COMMAND_OK)
    {
      const std::string local_from_path = toLocalPath(rename_from_path_);
      const std::string local_to_path   = toLocalPath(param);

      // Check if the source file exists already. We simple disallow overwriting a
      // file be renaming (the bahavior of the native rename command on Windows
      // and Linux differs; Windows will not overwrite files, Linux will).      
      if (Filesystem::FileStatus(local_to_path).isOk())
      {
        sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Target path exists already.");
        return;
      }

#ifdef WIN32

      if (MoveFileW(StrConvert::Utf8ToWide(local_from_path).c_str(), StrConvert::Utf8ToWide(local_to_path).c_str()) != 0)
      {
        sendFtpMessage(FtpReplyCode::FILE_ACTION_COMPLETED, "OK");
        return;
      }
      else
      {
        sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Error renaming file: " + GetLastErrorStr());
        return;
      }
#else // WIN32
      if (rename(local_from_path.c_str(), local_to_path.c_str()) == 0)
      {
        sendFtpMessage(FtpReplyCode::FILE_ACTION_COMPLETED, "OK");
        return;
      }
      else
      {
        sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Error renaming file");
        return;
      }
#endif // WIN32
    }
    else
    {
      sendFtpMessage(is_renamable_error);
      return;
    }
  }

  void FtpSession::handleFtpCommandABOR(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::COMMAND_NOT_IMPLEMENTED, "Command not implemented");
  }

  void FtpSession::handleFtpCommandDELE(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }
    const std::string local_path = toLocalPath(param);

    auto file_status = Filesystem::FileStatus(local_path);

    if (!file_status.isOk())
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Resource does not exist");
      return;
    }
    else if (file_status.type() != Filesystem::FileType::RegularFile)
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Resource is not a file");
      return;
    }
    else
    {
      if (static_cast<int>(logged_in_user_->permissions_ & Permission::FileDelete) == 0)
      {
        sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
        return;
      }
      else
      {
#ifdef WIN32
        if (DeleteFileW(StrConvert::Utf8ToWide(local_path).c_str()) != 0)
        {
          sendFtpMessage(FtpReplyCode::FILE_ACTION_COMPLETED, "Successfully deleted file");
          return;
        }
        else
        {
          sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Unable to delete file: " + GetLastErrorStr());
          return;
        }
#else
        if (unlink(local_path.c_str()) == 0)
        {
          sendFtpMessage(FtpReplyCode::FILE_ACTION_COMPLETED, "Successfully deleted file");
          return;
        }
        else
        {
          sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Unable to delete file");
          return;
        }
#endif
      }
    }
  }

  void FtpSession::handleFtpCommandRMD(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::DirDelete) == 0)
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
      return;
    }

    const std::string local_path = toLocalPath(param);

#ifdef WIN32
    if (RemoveDirectoryW(StrConvert::Utf8ToWide(local_path).c_str()) != 0)
    {
      sendFtpMessage(FtpReplyCode::FILE_ACTION_COMPLETED, "Successfully removed directory");
      return;
    }
    else
    {
      // If would be a good idea to return a 4xx error code here (-> temp error)
      // (e.g. FILE_ACTION_NOT_TAKEN), but RFC 959 assumes that all directory
      // errors are permanent.
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Unable to remove directory: " + GetLastErrorStr());
      return;
    }
#else
    if (rmdir(local_path.c_str()) == 0)
    {
      sendFtpMessage(FtpReplyCode::FILE_ACTION_COMPLETED, "Successfully removed directory");
      return;
    }
    else
    {
      // If would be a good idea to return a 4xx error code here (-> temp error)
      // (e.g. FILE_ACTION_NOT_TAKEN), but RFC 959 assumes that all directory
      // errors are permanent.
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Unable to remove directory");
      return;
    }
#endif

  }

  void FtpSession::handleFtpCommandMKD(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::DirCreate) == 0)
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
      return;
    }

    auto local_path = toLocalPath(param);

#ifdef WIN32
    LPSECURITY_ATTRIBUTES security_attributes = nullptr; // => Default security attributes
    if (CreateDirectoryW(StrConvert::Utf8ToWide(local_path).c_str(), security_attributes) != 0)
    {
      sendFtpMessage(FtpReplyCode::PATHNAME_CREATED, createQuotedFtpPath(toAbsoluteFtpPath(param)) + " Successfully created");
      return;
    }
    else
    {
      // If would be a good idea to return a 4xx error code here (-> temp error)
      // (e.g. FILE_ACTION_NOT_TAKEN), but RFC 959 assumes that all directory
      // errors are permanent.
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Unable to create directory: " + GetLastErrorStr());
      return;
    }
#else
    const mode_t mode = 0755;
    if (mkdir(local_path.c_str(), mode) == 0)
    {
      sendFtpMessage(FtpReplyCode::PATHNAME_CREATED, createQuotedFtpPath(toAbsoluteFtpPath(param)) + " Successfully created");
      return;
    }
    else
    {
      // If would be a good idea to return a 4xx error code here (-> temp error)
      // (e.g. FILE_ACTION_NOT_TAKEN), but RFC 959 assumes that all directory
      // errors are permanent.
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Unable to create directory");
      return;
    }
#endif
  }

  void FtpSession::handleFtpCommandPWD(const std::string& /*param*/)
  {
    // RFC 959 does not allow returning NOT_LOGGED_IN here, so we abuse ACTION_NOT_TAKEN for that.
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Not logged in");
      return;
    }

    sendFtpMessage(FtpReplyCode::PATHNAME_CREATED, createQuotedFtpPath(ftp_working_directory_));
  }

  void FtpSession::handleFtpCommandLIST(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }

    // RFC 959 does not allow ACTION_NOT_TAKEN (-> permanent error), so we return a temporary error (FILE_ACTION_NOT_TAKEN).
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::DirList) == 0)
    {
      sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Permission denied");
      return;
    }

    // Deal with some unusual commands like "LIST -a", "LIST -a dirname".
    // Some FTP clients send those commands, as if they would call ls on unix.
    // 
    // We try to support those parameters (or rather ignore them), even though
    // this techniqually breaks listing directories that actually use "-a" etc.
    // as directory name. As most clients however first CWD into a directory and
    // call LIST without parameter afterwards and starting a directory name with
    // "-a " / "-l " / "-al " / "-la " is not that common, the compatibility
    // benefit should outperform te potential problems by a lot.
    // 
    // The RFC-violating LIST command now shall be:
    // 
    //   LIST [<SP> <arg>] [<SP> <pathname>] <CRLF>
    // 
    //   with:
    // 
    //     <arg> ::= -a
    //             | -l
    //             | -al
    //             | -la
    //

    std::string path2dst;
    if ((param == "-a") || (param == "-l") || (param == "-al") || (param == "-la"))
    {
      path2dst  = "";
    }
    else if ((param.substr(0, 3)=="-a "|| param.substr(0, 3)=="-l ") && (param.size() > 3))
    {
      path2dst = param.substr(3);
    }
    else if ((param.substr(0, 4) == "-al " || param.substr(0, 4) == "-la ") && (param.size() > 4))
    {
      path2dst = param.substr(4);
    }
    else
    {
      path2dst = param;
    }

    const std::string local_path = toLocalPath(path2dst);
    auto dir_status = Filesystem::FileStatus(local_path);

    if (dir_status.isOk())
    {
      if (dir_status.type() == Filesystem::FileType::Dir)
      {
        if (dir_status.canOpenDir())
        {
          sendFtpMessage(FtpReplyCode::FILE_STATUS_OK_OPENING_DATA_CONNECTION, "Sending directory listing");
          sendDirectoryListing(Filesystem::dirContent(local_path, error_));
          return;
        }
        else
        {
          sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Permission denied");
          return;
        }
      }
      else
      {
        // TODO: RFC959: If the pathname specifies a file then the server should send current information on the file.
        sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Path is not a directory");
        return;
      }
    }
    else
    {
      sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Path does not exist");
      return;
    }
  }

  void FtpSession::handleFtpCommandNLST(const std::string& param)
  {
    if (!logged_in_user_)
    {
      sendFtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
      return;
    }

    // RFC 959 does not allow ACTION_NOT_TAKEN (-> permanent error), so we return a temporary error (FILE_ACTION_NOT_TAKEN).
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::DirList) == 0)
    {
      sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Permission denied");
      return;
    }

    const std::string local_path = toLocalPath(param);
    auto dir_status = Filesystem::FileStatus(local_path);

    if (dir_status.isOk())
    {
      if (dir_status.type() == Filesystem::FileType::Dir)
      {
        if (dir_status.canOpenDir())
        {
          sendFtpMessage(FtpReplyCode::FILE_STATUS_OK_OPENING_DATA_CONNECTION, "Sending name list");
          sendNameList(Filesystem::dirContent(local_path, error_));
          return;
        }
        else
        {
          sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Permission denied");
          return;
        }
      }
      else
      {
        // TODO: RFC959: If the pathname specifies a file then the server should send current information on the file.
        sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Path is not a directory");
        return;
      }
    }
    else
    {
      sendFtpMessage(FtpReplyCode::FILE_ACTION_NOT_TAKEN, "Path does not exist");
      return;
    }
  }

  void FtpSession::handleFtpCommandSITE(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::SYNTAX_ERROR_UNRECOGNIZED_COMMAND, "Command not implemented");
  }

  void FtpSession::handleFtpCommandSYST(const std::string& /*param*/)
  {
    // Always returning "UNIX" when being asked for the operating system.
    // Some clients (Mozilla Firefox for example) may disconnect, when we
    // return an unknown operating system here. As depending on the Server's
    // operating system is a horrible feature anyways, we simply fake it.
    //
    // Unix should be the best compatible value here, as we emulate Unix-like
    // outputs for other commands (-> LIST) on all operating systems.
    sendFtpMessage(FtpReplyCode::NAME_SYSTEM_TYPE, "UNIX");
  }

  void FtpSession::handleFtpCommandSTAT(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::COMMAND_NOT_IMPLEMENTED, "Command not implemented");
  }

  void FtpSession::handleFtpCommandHELP(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::COMMAND_NOT_IMPLEMENTED, "Command not implemented");
  }

  void FtpSession::handleFtpCommandNOOP(const std::string& /*param*/)
  {
    sendFtpMessage(FtpReplyCode::COMMAND_OK, "OK");
  }

  // Modern FTP Commands
  void FtpSession::handleFtpCommandFEAT(const std::string& /*param*/)
  {
    std::stringstream ss;
    ss << "211- Feature List:\r\n";
    ss << " UTF8\r\n";
    ss << " SIZE\r\n";
    ss << " LANG EN\r\n";
    ss << "211 END\r\n";

    sendRawFtpMessage(ss.str());
  }

  void FtpSession::handleFtpCommandOPTS(const std::string& param)
  {
    std::string param_upper = param;
    std::transform(param_upper.begin(), param_upper.end(), param_upper.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });

    if (param_upper == "UTF8 ON")
    {
      sendFtpMessage(FtpReplyCode::COMMAND_OK, "OK");
      return;
    }

    sendFtpMessage(FtpReplyCode::COMMAND_NOT_IMPLEMENTED_FOR_PARAMETER, "Unrecognized parameter");
  }

  ////////////////////////////////////////////////////////
  // FTP data-socket send
  ////////////////////////////////////////////////////////

  void FtpSession::sendDirectoryListing(const std::map<std::string, Filesystem::FileStatus>& directory_content)
  {
    auto data_socket = std::make_shared<asio::ip::tcp::socket>(io_context_);

    data_acceptor_.async_accept(*data_socket
                              , data_socket_strand_.wrap([data_socket, directory_content, me = shared_from_this()](auto ec)
                                {
                                  if (ec)
                                  {
                                    me->sendFtpMessage(FtpReplyCode::TRANSFER_ABORTED, "Data transfer aborted: " + ec.message());
                                    return;
                                  }
   
                                  me->data_socket_weakptr_ = data_socket;

                                  // TODO: close acceptor after connect?
                                  // Create a Unix-like file list
                                  std::stringstream stream; // NOLINT(misc-const-correctness) Reason: False detection, this cannot be made const
                                  for (const auto& entry : directory_content)
                                  {
                                    const std::string& filename(entry.first);
                                    const fineftp::Filesystem::FileStatus& file_status(entry.second);

                                    stream << ((file_status.type() == fineftp::Filesystem::FileType::Dir) ? 'd' : '-') << file_status.permissionString() << "   1 ";
                                    stream << std::setw(10) << file_status.ownerString() << " " << std::setw(10) << file_status.groupString() << " ";
                                    stream << std::setw(10) << file_status.fileSize() << " ";
                                    stream << file_status.timeString() << " ";
                                    stream << filename;
                                    stream << "\r\n";
                                  }

                                  // Copy the file list into a raw char vector
                                  const std::string dir_listing_string = stream.str();
                                  const std::shared_ptr<std::vector<char>> dir_listing_rawdata = std::make_shared<std::vector<char>>();
                                  dir_listing_rawdata->reserve(dir_listing_string.size());
                                  std::copy(dir_listing_string.begin(), dir_listing_string.end(), std::back_inserter(*dir_listing_rawdata));

                                  // Send the string out
                                  me->addDataToBufferAndSend(dir_listing_rawdata, data_socket);
                                  me->addDataToBufferAndSend(std::shared_ptr<std::vector<char>>(), data_socket);// Nullpointer indicates end of transmission
                                }));
  }

  void FtpSession::sendNameList(const std::map<std::string, Filesystem::FileStatus>& directory_content)
  {
    auto data_socket = std::make_shared<asio::ip::tcp::socket>(io_context_);

    data_acceptor_.async_accept(*data_socket
                              , data_socket_strand_.wrap([data_socket, directory_content, me = shared_from_this()](auto ec)
                                {
                                  if (ec)
                                  {
                                    me->sendFtpMessage(FtpReplyCode::TRANSFER_ABORTED, "Data transfer aborted: " + ec.message());
                                    return;
                                  }
    
                                  me->data_socket_weakptr_ = data_socket;

                                  // Create a file list
                                  std::stringstream stream; // NOLINT(misc-const-correctness) Reason: False detection, this cannot be made const
                                  for (const auto& entry : directory_content)
                                  {
                                    stream << entry.first;
                                    stream << "\r\n";
                                  }

                                  // Copy the file list into a raw char vector
                                  const std::string dir_listing_string = stream.str();
                                  const std::shared_ptr<std::vector<char>> dir_listing_rawdata = std::make_shared<std::vector<char>>();
                                  dir_listing_rawdata->reserve(dir_listing_string.size());
                                  std::copy(dir_listing_string.begin(), dir_listing_string.end(), std::back_inserter(*dir_listing_rawdata));

                                  // Send the string out
                                  me->addDataToBufferAndSend(dir_listing_rawdata, data_socket);
                                  me->addDataToBufferAndSend(std::shared_ptr<std::vector<char>>(), data_socket);// Nullpointer indicates end of transmission
                                }));
  }

  void FtpSession::sendFile(const std::shared_ptr<ReadableFile>& file)
  {
    auto data_socket = std::make_shared<asio::ip::tcp::socket>(io_context_);

    data_acceptor_.async_accept(*data_socket
                              , data_socket_strand_.wrap([data_socket, file, me = shared_from_this()](auto ec)
                                {
                                  if (ec)
                                  {
                                    me->sendFtpMessage(FtpReplyCode::TRANSFER_ABORTED, "Data transfer aborted: " + ec.message());
                                    return;
                                  }

                                  if (file->size() == 0U)
                                  {
                                    me->sendFtpMessage(FtpReplyCode::CLOSING_DATA_CONNECTION, "Done");
                                  }
                                  else if (file->data() == nullptr)
                                  {
                                    // Error that should never happen. If it does, it's a bug in the server.
                                    // Usually, if the data is null, the file size should be 0.
                                    me->sendFtpMessage(FtpReplyCode::TRANSFER_ABORTED, "Data transfer aborted: File data is null");
                                  }
                                  else
                                  {
                                    me->data_socket_weakptr_ = data_socket;

                                    // Send the file
                                    asio::async_write(*data_socket
                                                    , asio::buffer(file->data(), file->size())
                                                    , [me, file, data_socket](asio::error_code ec, std::size_t /*bytes_to_transfer*/)
                                                      {
                                                        if (ec)
                                                        {
                                                          me->sendFtpMessage(FtpReplyCode::TRANSFER_ABORTED, "Data transfer aborted: " + ec.message());
                                                        }
                                                        else
                                                        {
                                                          // Close Data Socket properly
                                                          {
                                                            asio::error_code errc;
                                                            data_socket->shutdown(asio::socket_base::shutdown_both, errc);
                                                            data_socket->close(errc);
                                                          }

                                                          // Ugly work-around:
                                                          // An FTP client implementation has been observed to close the data connection
                                                          // as soon as it receives the 226 status code - even though it hasn't received
                                                          // all data, yet. To improve interoperability with such buggy clients, sending
                                                          // of the 226 status code can be delayed a bit. The delay is defined through a
                                                          // preprocessor definition. If the delay is 0, no delay is introduced at all.
                                                          #if (0 == DELAY_226_RESP_MS)
                                                            me->sendFtpMessage(FtpReplyCode::CLOSING_DATA_CONNECTION, "Done");
                                                          #else
                                                            me->timer_.expires_after(std::chrono::milliseconds{DELAY_226_RESP_MS});
                                                            me->timer_.async_wait(me->data_socket_strand_.wrap([me](const asio::error_code& ec)
                                                                                  {
                                                                                    if (ec != asio::error::operation_aborted)
                                                                                    {
                                                                                      me->sendFtpMessage(FtpReplyCode::CLOSING_DATA_CONNECTION, "Done");
                                                                                    }
                                                                                  }));
                                                          #endif
                                                        }
                                                      });
                                  }
                                }));
  }

  void FtpSession::addDataToBufferAndSend(const std::shared_ptr<std::vector<char>>& data, const std::shared_ptr<asio::ip::tcp::socket>& data_socket)
  {
    asio::post(data_socket_strand_, [me = shared_from_this(), data, data_socket]()
                            {
                              const bool write_in_progress = (!me->data_buffer_.empty());

                              me->data_buffer_.push_back(data);

                              if (!write_in_progress)
                              {
                                me->writeDataToSocket(data_socket);
                              }
                            });
  }

  void FtpSession::writeDataToSocket(const std::shared_ptr<asio::ip::tcp::socket>& data_socket)
  {
    asio::post(data_socket_strand_,
      [me = shared_from_this(), data_socket]()
      {
        auto data = me->data_buffer_.front();

        if (data)
        {
          // Send out the buffer
          asio::async_write(*data_socket
                            , asio::buffer(*data)
                            , me->data_socket_strand_.wrap([me, data, data_socket](asio::error_code ec, std::size_t /*bytes_to_transfer*/)
                              {
                                me->data_buffer_.pop_front();

                                if (ec)
                                {
                                  me->error_ << "Data write error: " << ec.message() << std::endl;
                                  return;
                                }

                                if (!me->data_buffer_.empty())
                                {
                                  me->writeDataToSocket(data_socket);
                                }
                              }
                              ));
        }
        else
        {
          // we got to the end of transmission
          me->data_buffer_.pop_front();

          // Close Data Socket properly
          {
            asio::error_code ec;
            data_socket->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            data_socket->close(ec);
          }

          me->sendFtpMessage(FtpReplyCode::CLOSING_DATA_CONNECTION, "Done");
        }
      });
  }

  ////////////////////////////////////////////////////////
  // FTP data-socket receive
  ////////////////////////////////////////////////////////

  void FtpSession::receiveFile(const std::shared_ptr<WriteableFile>& file)
  {
    auto data_socket = std::make_shared<asio::ip::tcp::socket>(io_context_);

    data_acceptor_.async_accept(*data_socket
                              , data_socket_strand_.wrap([data_socket, file, me = shared_from_this()](auto ec)
                                {
                                  if (ec)
                                  {
                                    me->error_ << "Data transfer aborted: " << ec.message() << std::endl;
                                    me->sendFtpMessage(FtpReplyCode::TRANSFER_ABORTED, "Data transfer aborted");
                                    return;
                                  }

                                  me->data_socket_weakptr_ = data_socket;
                                  me->receiveDataFromSocketAndWriteToFile(file, data_socket);
                                }));
  }

  void FtpSession::receiveDataFromSocketAndWriteToFile(const std::shared_ptr<WriteableFile>& file, const std::shared_ptr<asio::ip::tcp::socket>& data_socket)
  {
    const std::shared_ptr<std::vector<char>> buffer = std::make_shared<std::vector<char>>(1024 * 1024 * 1);
      
    asio::async_read(*data_socket
                    , asio::buffer(*buffer)
                    , asio::transfer_at_least(buffer->size())
                    , data_socket_strand_.wrap([me = shared_from_this(), file, data_socket, buffer](asio::error_code ec, std::size_t length)
                      {
                        buffer->resize(length);
                        if (ec)
                        {
                          if (length > 0)
                          {
                            me->writeDataToFile(buffer, file);
                          }
                          me->endDataReceiving(file, data_socket);
                          return;
                        }
                        else if (length > 0)
                        {
                          me->writeDataToFile(buffer, file, [me, file, data_socket]() { me->receiveDataFromSocketAndWriteToFile(file, data_socket); });
                        }
                      }));
  }


  void FtpSession::writeDataToFile(const std::shared_ptr<std::vector<char>>& data, const std::shared_ptr<WriteableFile>& file, const std::function<void(void)>& fetch_more)
  {
    fetch_more();
    file->write(data->data(), data->size());
  }

  void FtpSession::endDataReceiving(const std::shared_ptr<WriteableFile>& file, const std::shared_ptr<asio::ip::tcp::socket>& data_socket)
  {
    asio::post(data_socket_strand_, [me = shared_from_this(), file, data_socket]()
                             {
                               file->close();
                               me->sendFtpMessage(FtpReplyCode::CLOSING_DATA_CONNECTION, "Done");
                               asio::error_code ec;
                               data_socket->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                               data_socket->close();
                             });
  }

  ////////////////////////////////////////////////////////
  // Helpers
  ////////////////////////////////////////////////////////

  std::string FtpSession::toAbsoluteFtpPath(const std::string& rel_or_abs_ftp_path) const
  {
    std::string absolute_ftp_path;

    if (!rel_or_abs_ftp_path.empty() && (rel_or_abs_ftp_path[0] == '/'))
    {
      // Absolut path is given. We still clean it to make sure it doesn't contain any ".." that go above the root directory.
      absolute_ftp_path = fineftp::Filesystem::cleanPath(rel_or_abs_ftp_path, false, '/');
    }
    else
    {
      absolute_ftp_path = fineftp::Filesystem::cleanPath(ftp_working_directory_ + "/" + rel_or_abs_ftp_path, false, '/');
    }

    return absolute_ftp_path;
  }

  std::string FtpSession::toLocalPath(const std::string& ftp_path) const
  {
    assert(logged_in_user_);

    // First make the ftp path absolute if it isn't already. This also cleans
    // the path and makes sure that it doesn't contain any ".." that go above
    // the root directory.
    const std::string absolute_ftp_path = toAbsoluteFtpPath(ftp_path);

    // Now map it to the local filesystem
    return fineftp::Filesystem::cleanPathNative(logged_in_user_->local_root_path_ + "/" + absolute_ftp_path);
  }

  std::string FtpSession::createQuotedFtpPath(const std::string& unquoted_ftp_path)
  {
    std::string output;
    output.reserve(unquoted_ftp_path.size() * 2 + 2);
    output.push_back('\"');

    for (const char c : unquoted_ftp_path)
    {
      output.push_back(c);
      if (c == '\"')            // Escape quote by double-quote
        output.push_back(c);
    }

    output.push_back('\"');

    return output;
  }

  FtpMessage FtpSession::checkIfPathIsRenamable(const std::string& ftp_path) const
  {
    if (!logged_in_user_) return FtpMessage(FtpReplyCode::NOT_LOGGED_IN, "Not logged in");

    if (!ftp_path.empty())
    {
      // Is the given path a file or a directory?
      auto file_status = Filesystem::FileStatus(toLocalPath(ftp_path));

      if (file_status.isOk())
      {
        // Calculate required permissions to rename the given resource
        Permission required_permissions = Permission::None;
        if (file_status.type() == Filesystem::FileType::Dir)
        {
          required_permissions = Permission::DirRename;
        }
        else
        {
          required_permissions = Permission::FileRename;
        }

        // Send error if the permisions are insufficient
        if ((logged_in_user_->permissions_ & required_permissions) != required_permissions)
        {
          return FtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
        }

        return FtpMessage(FtpReplyCode::COMMAND_OK, "");
      }
      else
      {
        return FtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "File does not exist");
      }
    }
    else
    {
      return FtpMessage(FtpReplyCode::SYNTAX_ERROR_PARAMETERS, "Empty path");
    }
  }

  FtpMessage FtpSession::executeCWD(const std::string& param)
  {
    if (!logged_in_user_)
    {
      return FtpMessage(FtpReplyCode::NOT_LOGGED_IN,    "Not logged in");
    }
    if (static_cast<int>(logged_in_user_->permissions_ & Permission::DirList) == 0)
    {
      return FtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Permission denied");
    }


    if (param.empty())
    {
      return FtpMessage(FtpReplyCode::SYNTAX_ERROR_PARAMETERS, "No path given");
    }

    std::string absolute_new_working_dir;

    if (param[0] == '/')
    {
      // Absolute path given
      absolute_new_working_dir = fineftp::Filesystem::cleanPath(param, false, '/');
    }
    else
    {
      // Make the path abolute
      absolute_new_working_dir = fineftp::Filesystem::cleanPath(ftp_working_directory_ + "/" + param, false, '/');
    }

    auto local_path = toLocalPath(absolute_new_working_dir);
    const Filesystem::FileStatus file_status(local_path);

    if (!file_status.isOk())
    {
      return FtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Failed ot change directory: The given resource does not exist or permission denied.");
    }
    if (file_status.type() != Filesystem::FileType::Dir)
    {
      return FtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Failed ot change directory: The given resource is not a directory.");
    }
    if (!file_status.canOpenDir())
    {
      return FtpMessage(FtpReplyCode::ACTION_NOT_TAKEN, "Failed ot change directory: Permission denied.");
    }
    ftp_working_directory_ = absolute_new_working_dir;
    return FtpMessage(FtpReplyCode::FILE_ACTION_COMPLETED, "Working directory changed to " + ftp_working_directory_);
  }

#ifdef WIN32
  std::string FtpSession::GetLastErrorStr()
  {
    const DWORD error = GetLastError();
    if (error != 0)
    {
      LPVOID lp_msg_buf = nullptr;
      const DWORD buf_len = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                                          FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                          nullptr,
                                          error,
                                          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                          reinterpret_cast<LPTSTR>(&lp_msg_buf),
                                          0, nullptr );
      if (buf_len != 0)
      {
        LPCSTR lp_msg_str = reinterpret_cast<LPCSTR>(lp_msg_buf);
        std::string result(lp_msg_str, lp_msg_str + buf_len);
        result.erase(std::remove_if(result.begin(),
                                    result.end(),
                                    [](unsigned char x) {return std::iscntrl(x); }),
                     result.end()); //remove CRLF
        LocalFree(lp_msg_buf);

        return result;
      }
    }

    return ""; 
  }
#endif //WIN32
}
