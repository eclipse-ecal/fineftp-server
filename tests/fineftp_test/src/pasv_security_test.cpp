#include <asio.hpp>
#include <gtest/gtest.h>

#include <fineftp/server.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>

namespace
{
  struct TestRoot
  {
    TestRoot()
      : path(std::filesystem::current_path() / "pasv_security_ftp_root")
    {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
      std::filesystem::create_directories(path);
    }

    ~TestRoot()
    {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }

    const std::filesystem::path path;
  };

  struct Reply
  {
    int code;
    std::string line;
  };

  class RawFtpClient
  {
  public:
    explicit RawFtpClient(uint16_t port)
      : socket_(io_context_)
    {
      socket_.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
      readReply();
    }

    void loginAnonymous()
    {
      EXPECT_EQ(command("USER anonymous").code, 331);
      EXPECT_EQ(command("PASS anonymous").code, 230);
    }

    asio::ip::tcp::endpoint enterPassive()
    {
      const Reply reply = command("PASV");
      EXPECT_EQ(reply.code, 227);

      std::smatch match;
      const std::regex pasv_regex("\\((\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)\\)");
      EXPECT_TRUE(std::regex_search(reply.line, match, pasv_regex)) << reply.line;

      const std::string address = match[1].str() + "." + match[2].str() + "." + match[3].str() + "." + match[4].str();
      const uint16_t port = static_cast<uint16_t>((std::stoi(match[5].str()) << 8) + std::stoi(match[6].str()));
      return asio::ip::tcp::endpoint(asio::ip::make_address(address), port);
    }

    Reply command(const std::string& command_line)
    {
      asio::write(socket_, asio::buffer(command_line + "\r\n"));
      return readReply();
    }

    Reply readReply()
    {
      asio::read_until(socket_, input_, "\r\n");

      std::istream stream(&input_);
      std::string line;
      std::getline(stream, line);
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }

      return {std::stoi(line.substr(0, 3)), line};
    }

    asio::io_context& ioContext()
    {
      return io_context_;
    }

  private:
    asio::io_context io_context_;
    asio::ip::tcp::socket socket_;
    asio::streambuf input_;
  };

  std::string readAll(asio::ip::tcp::socket& socket)
  {
    std::string result;
    std::array<char, 4096> buffer{};
    asio::error_code ec;

    for (;;)
    {
      const std::size_t length = socket.read_some(asio::buffer(buffer), ec);
      if (length > 0)
      {
        result.append(buffer.data(), length);
      }

      if (ec == asio::error::eof)
      {
        return result;
      }
      if (ec)
      {
        ADD_FAILURE() << ec.message();
        return result;
      }
    }
  }

  std::string readFile(const std::filesystem::path& path)
  {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  }
}

TEST(PasvSecurityTest, RetrStillWorksForMatchingControlPeer)
{
  const TestRoot root;
  const std::filesystem::path file_path = root.path / "secret.txt";
  const std::string file_content = "classified\n";
  std::ofstream(file_path, std::ios::binary) << file_content;

  fineftp::FtpServer server(0);
  server.start(1);
  server.addUserAnonymous(root.path.string(), fineftp::Permission::All);

  RawFtpClient client(server.getPort());
  client.loginAnonymous();

  const asio::ip::tcp::endpoint data_endpoint = client.enterPassive();
  asio::ip::tcp::socket data_socket(client.ioContext());
  data_socket.connect(data_endpoint);

  EXPECT_EQ(client.command("RETR secret.txt").code, 150);
  EXPECT_EQ(readAll(data_socket), file_content);
  EXPECT_EQ(client.readReply().code, 226);

  server.stop();
}

TEST(PasvSecurityTest, StorStillWorksForMatchingControlPeer)
{
  const TestRoot root;
  const std::string upload_content = "uploaded\n";

  fineftp::FtpServer server(0);
  server.start(1);
  server.addUserAnonymous(root.path.string(), fineftp::Permission::All);

  RawFtpClient client(server.getPort());
  client.loginAnonymous();

  const asio::ip::tcp::endpoint data_endpoint = client.enterPassive();
  asio::ip::tcp::socket data_socket(client.ioContext());
  data_socket.connect(data_endpoint);

  EXPECT_EQ(client.command("STOR upload.txt").code, 150);
  asio::write(data_socket, asio::buffer(upload_content));
  asio::error_code ec;
  data_socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
  EXPECT_EQ(client.readReply().code, 226);

  EXPECT_EQ(readFile(root.path / "upload.txt"), upload_content);

  server.stop();
}

TEST(PasvSecurityTest, PasvPortIsClosedAfterTransfer)
{
  const TestRoot root;
  const std::filesystem::path file_path = root.path / "secret.txt";
  std::ofstream(file_path, std::ios::binary) << "closed after first transfer\n";

  fineftp::FtpServer server(0);
  server.start(1);
  server.addUserAnonymous(root.path.string(), fineftp::Permission::All);

  RawFtpClient client(server.getPort());
  client.loginAnonymous();

  const asio::ip::tcp::endpoint data_endpoint = client.enterPassive();
  asio::ip::tcp::socket first_data_socket(client.ioContext());
  first_data_socket.connect(data_endpoint);

  EXPECT_EQ(client.command("RETR secret.txt").code, 150);
  static_cast<void>(readAll(first_data_socket));
  EXPECT_EQ(client.readReply().code, 226);

  asio::ip::tcp::socket second_data_socket(client.ioContext());
  asio::error_code connect_ec;
  second_data_socket.connect(data_endpoint, connect_ec);
  EXPECT_TRUE(connect_ec) << "Second connection to the same PASV port unexpectedly succeeded";

  server.stop();
}

TEST(PasvSecurityTest, StorRejectsDifferentDataConnectionSourceAddress)
{
  const TestRoot root;
  const std::string attacker_content = "attacker\n";

  fineftp::FtpServer server(0);
  server.start(1);
  server.addUserAnonymous(root.path.string(), fineftp::Permission::All);

  RawFtpClient client(server.getPort());
  client.loginAnonymous();

  const asio::ip::tcp::endpoint data_endpoint = client.enterPassive();
  asio::ip::tcp::socket data_socket(client.ioContext());
  data_socket.open(asio::ip::tcp::v4());

  asio::error_code bind_ec;
  data_socket.bind(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.2"), 0), bind_ec);
  if (bind_ec)
  {
    server.stop();
    GTEST_SKIP() << "Alternate loopback source address is not available: " << bind_ec.message();
  }

  asio::error_code connect_ec;
  data_socket.connect(data_endpoint, connect_ec);
  if (connect_ec)
  {
    server.stop();
    GTEST_SKIP() << "Alternate loopback source address cannot reach PASV endpoint: " << connect_ec.message();
  }

  EXPECT_EQ(client.command("STOR rejected.txt").code, 150);

  asio::error_code write_ec;
  asio::write(data_socket, asio::buffer(attacker_content), write_ec);
  data_socket.shutdown(asio::ip::tcp::socket::shutdown_send, write_ec);

  EXPECT_EQ(client.readReply().code, 425);
  const std::filesystem::path rejected_file = root.path / "rejected.txt";
  if (std::filesystem::exists(rejected_file))
  {
    EXPECT_NE(readFile(rejected_file), attacker_content);
  }

  server.stop();
}