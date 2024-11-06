#include <gtest/gtest.h>

#include <fineftp/server.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef WIN32
#include <win_str_convert.h>
#endif // WIN32

namespace
{
  // https://everything.curl.dev/usingcurl/returns
  constexpr int curl_return_code_ftp_access_denied       = 9;  // Curl@Ubuntu: This is returned, when the file exists, but the permissions prevent downloading
  constexpr int curl_return_code_ftp_download_failed     = 19; // Curl@Windows: This is returned, when the file does not exist
  constexpr int curl_return_code_quote_command_error     = 21;
  constexpr int curl_return_code_upload_failed           = 25;
  constexpr int curl_return_code_login_failed            = 67;
  constexpr int curl_return_code_resource_does_not_exist = 78; // This is returned, when the file exists, but the permissions prevent downloading

  // Custom system command that returns the actual return value of the command, even on POSIX Systems.
  int system_execute(const std::string& command)
  {
    const int status = std::system(command.c_str());
#ifdef WIN32
    return status;
#else  // WIN32
    if (WIFEXITED(status))
    {
      // Program has exited normally
      return WEXITSTATUS(status);
    }
    else
    {
      // Program has exited abnormally
      return -1;
    }
#endif // WIN32

  }

  struct DirPreparer
  {
    DirPreparer(unsigned int num_servers, unsigned int num_clients_per_server)
      : num_servers(num_servers)
      , num_clients_per_server(num_clients_per_server)
    {
      // Make sure, the local_root_dir and local_ftp_root_dir don't exist, yet
      if (std::filesystem::exists(local_root_dir))
        std::filesystem::remove_all(local_root_dir);

      if (std::filesystem::exists(local_ftp_root_dir))
        std::filesystem::remove_all(local_ftp_root_dir);

      // Create root directories again
      std::filesystem::create_directories(local_ftp_root_dir);
      std::filesystem::create_directories(local_root_dir);

      // Create server directories
      for (unsigned int server_idx = 0; server_idx < num_servers; ++server_idx)
      {
        std::filesystem::create_directories(local_ftp_root_dir / ("s" + std::to_string(server_idx)));
      }

      // Create client directories
      for (unsigned int server_idx = 0; server_idx < num_servers; ++server_idx)
      {
        for (unsigned int client_idx = 0; client_idx < num_clients_per_server; ++client_idx)
        {
          std::filesystem::create_directories(local_root_dir / ("s" + std::to_string(server_idx) + "_c" + std::to_string(client_idx)));
        }
      }
    }

    // Disable copy and move
    DirPreparer(const DirPreparer&)            = delete;
    DirPreparer& operator=(const DirPreparer&) = delete;
    DirPreparer(DirPreparer&&)                 = delete;
    DirPreparer& operator=(DirPreparer&&)      = delete;

    ~DirPreparer()
    {
      // Remove all directories and files
      std::error_code ec;
      std::filesystem::remove_all(local_root_dir, ec);
      std::filesystem::remove_all(local_ftp_root_dir, ec);
    }

    std::filesystem::path server_local_root_dir(unsigned int server_idx) const
    {
      return local_ftp_root_dir / ("s" + std::to_string(server_idx));
    }

    std::filesystem::path client_local_root_dir(unsigned int server_idx, unsigned int client_idx) const
    {
      return local_root_dir / ("s" + std::to_string(server_idx) + "_c" + std::to_string(client_idx));
    }

    /**
     * @brief Creates a file with a given size in bytes for all servers
     */
    void create_server_files(const std::filesystem::path& relative_path, size_t size_bytes) const
    {
      // Throw exception, if the path is not relative
      if (relative_path.is_absolute())
        throw std::invalid_argument("The path must be relative!");

      // Create the file for all servers
      for (unsigned int server_idx = 0; server_idx < num_servers; ++server_idx)
      {
        std::ofstream file(server_local_root_dir(server_idx) / relative_path, std::ios::binary);
        std::vector<char> data(size_bytes, 'a');
        file.write(data.data(), size_bytes);
      }
    }

    /**
     * @brief Creates a file with a given size in bytes for all clients
     */
    void create_client_files(const std::filesystem::path& relative_path, size_t size_bytes) const
    {
      // Throw exception, if the path is not relative
      if (relative_path.is_absolute())
        throw std::invalid_argument("The path must be relative!");

      // Create the file for all clients
      for (unsigned int server_idx = 0; server_idx < num_servers; ++server_idx)
      {
        for (unsigned int client_idx = 0; client_idx < num_clients_per_server; ++client_idx)
        {
          std::ofstream file(client_local_root_dir(server_idx, client_idx) / relative_path, std::ios::binary);
          std::vector<char> data(size_bytes, 'a');
          file.write(data.data(), size_bytes);
        }
      }
    }

    // Server and client count
    const unsigned int num_servers;
    const unsigned int num_clients_per_server;

    // Root dir for the entire test
    const std::filesystem::path test_working_dir   = std::filesystem::current_path();;

    // Directories for the FTP Server and the local root
    const std::filesystem::path local_ftp_root_dir = test_working_dir / "ftp_root";
    const std::filesystem::path local_root_dir     = test_working_dir / "local_root";
  };
}


#if 1
// Create and destroy a server object without doing aynthing with it
TEST(StartStopTests, RAII_destroy_without_connection)
{
  const fineftp::FtpServer server(2121);
}
#endif

#if 1
// Create a server object, start it and destroy it
TEST(StartStopTests, RAII_destroy_started)
{
  fineftp::FtpServer server(2121);
  server.start(4);
}
#endif

#if 1
// Create a server object, start it, stop it and destroy it
TEST(StartStopTests, RAII_destroy_started_stopped)
{
  fineftp::FtpServer server(2121);
  server.start(4);
  server.stop();
}
#endif

#if 1
// Access the server with curl and check the connection count
TEST(StartStopTests, connection_count)
{
  const DirPreparer dir_preparer(1, 1);
  dir_preparer.create_client_files(std::filesystem::path("test.txt"), 16);

  {
    fineftp::FtpServer server(2121);
    server.addUserAnonymous(dir_preparer.server_local_root_dir(0).string(), fineftp::Permission::All);
    server.start(4);

    // Check connection count
    EXPECT_EQ(server.getOpenConnectionCount(), 0);

    // use CURL to upload the file to the server
    const std::string curl_command = "curl -S -s -T " + dir_preparer.client_local_root_dir(0, 0).string() + "/test.txt ftp://localhost:2121/test.txt --user anonymous:anonymous";
    const int curl_return_code = system_execute(curl_command);

    EXPECT_EQ(curl_return_code, 0);

    // The connection count should be 0 again, as CURL has terminated the connection
    EXPECT_EQ(server.getOpenConnectionCount(), 0);

    server.stop();
  }
}
#endif

#if 1
// Create a large amount of servers, upload files to them from threads and stop the servers while the upload may still be in progress
TEST(StartStopTests, multiple_servers_upload_stop)
{
  constexpr unsigned int num_servers = 100; // TODO: increase
  constexpr unsigned int num_clients_per_server = 5; // TODO: increase

  const DirPreparer dir_preparer(num_servers, num_clients_per_server);
  dir_preparer.create_client_files(std::filesystem::path("test.txt"), 10 * 1024 * 1024);

  // Create a large amount of FTP Servers
  std::vector<std::unique_ptr<fineftp::FtpServer>> server_list;
  server_list.reserve(num_servers);
  for (unsigned int i = 0; i < num_servers; ++i)
  {
    server_list.push_back(std::make_unique<fineftp::FtpServer>(0));
    server_list.back()->addUserAnonymous(dir_preparer.server_local_root_dir(i).string(), fineftp::Permission::All);
    server_list.back()->start(4);
  }

  // Check connection count of each server
  for (unsigned int i = 0; i < num_servers; ++i)
  {
    EXPECT_EQ(server_list[i]->getOpenConnectionCount(), 0);
  }

  // Check port of each server to make sure each server has a non-zero port
  for (unsigned int i = 0; i < num_servers; ++i)
  {
    EXPECT_NE(server_list[i]->getPort(), 0);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Create a thread for each client and let it upload a file to the server
  std::vector<std::thread> threads;
  threads.reserve(num_servers * num_clients_per_server);

  // Create Clients for all servers that upload a file to the server.
  // We wait a short time after each server, so we get a better distribution of
  // the clients. We aim for a wild mix of clients that have already finished
  // uploading, that are currently uploading and that haven't successfully
  // established a connection, yet
  for (unsigned int server_idx = 0; server_idx < num_servers; ++server_idx)
  {
    const uint16_t port = server_list[server_idx]->getPort();

    for (unsigned int client_idx = 0; client_idx < num_clients_per_server; ++client_idx)
    {
      threads.emplace_back([&dir_preparer, server_idx, client_idx, port]()
                            {
                              // use CURL to upload the file to the server
                              const std::string curl_command = "curl -S -s --max-time 5 --connect-timeout 1 -T " + dir_preparer.client_local_root_dir(server_idx, client_idx).string() + "/test.txt ftp://localhost:" + std::to_string(port) + "/test" + std::to_string(client_idx) + ".txt --user anonymous:anonymous";
                              const int curl_return_code = system_execute(curl_command);
                            });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Stop the servers while the upload may still be in progress
  for (unsigned int i = 0; i < num_servers; ++i)
  {
    server_list[i]->stop();
  }

  // Wait for all threads to finish
  for (std::thread& thread : threads)
  {
    thread.join();
  }

  // Check if the connection count is now 0
  for (unsigned int i = 0; i < num_servers; ++i)
  {
    EXPECT_EQ(server_list[i]->getOpenConnectionCount(), 0);
  }
}

#endif
