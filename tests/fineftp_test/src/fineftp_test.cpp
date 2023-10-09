#include <gtest/gtest.h>

#include <fineftp/server.h>

#include <filesystem>
#include <string>
#include <fstream>
#include <thread>
#include <algorithm>

#if 1
TEST(FineFTPTest, SimpleUploadDownload) {
  auto test_working_dir = std::filesystem::current_path();
  auto ftp_root_dir     = test_working_dir / "ftp_root";
  auto local_root_dir   = test_working_dir / "local_root";

  if (std::filesystem::exists(ftp_root_dir))
    std::filesystem::remove_all(ftp_root_dir);

  if (std::filesystem::exists(local_root_dir))
    std::filesystem::remove_all(local_root_dir);

  // Make sure that we start clean, so no old dir exists
  ASSERT_FALSE(std::filesystem::exists(ftp_root_dir));
  ASSERT_FALSE(std::filesystem::exists(local_root_dir));

  std::filesystem::create_directory(ftp_root_dir);
  std::filesystem::create_directory(local_root_dir);

  // Make sure that we were able to create the dir
  ASSERT_TRUE(std::filesystem::is_directory(ftp_root_dir));
  ASSERT_TRUE(std::filesystem::is_directory(local_root_dir));

  fineftp::FtpServer server(2121);
  server.start(1);

  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Create a hello world.txt file in the local root dir and write "Hello World" into it
  auto local_file = local_root_dir / "hello_world.txt";
  std::ofstream ofs(local_file.string());
  ofs << "Hello World";
  ofs.close();

  // Make sure that the file exists
  ASSERT_TRUE(std::filesystem::exists(local_file));
  ASSERT_TRUE(std::filesystem::is_regular_file(local_file));

  // Upload the file to the FTP server using curl
  {
    std::string curl_command = "curl -T \"" + local_file.string() + "\" \"ftp://localhost:2121/\"";
    auto curl_result = std::system(curl_command.c_str());

    // Make sure that the upload was successful
    ASSERT_EQ(curl_result, 0);

    // Make sure that the file exists in the FTP root dir
    auto ftp_file = ftp_root_dir / "hello_world.txt";
    ASSERT_TRUE(std::filesystem::exists(ftp_file));
    ASSERT_TRUE(std::filesystem::is_regular_file(ftp_file));

    // Make sure that the file has the same content
    std::ifstream ifs(ftp_file.string());
    std::string content((std::istreambuf_iterator<char>(ifs)),
            (std::istreambuf_iterator<char>()));
    ASSERT_EQ(content, "Hello World");
  }

  // Download the file again
  {
    std::string curl_command_download = "curl -o \"" + local_root_dir.string() + "/hello_world_download.txt\" \"ftp://localhost:2121/hello_world.txt\"";
    std::system(curl_command_download.c_str());

    // Make sure that the files are identical
    auto local_file_download = local_root_dir / "hello_world_download.txt";

    ASSERT_TRUE(std::filesystem::exists(local_file_download));
    ASSERT_TRUE(std::filesystem::is_regular_file(local_file_download));

    std::ifstream ifs(local_file_download.string());
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));

    ASSERT_EQ(content, "Hello World");
  }

  // Stop the server
  server.stop();
}
#endif

#if 1
TEST(FineFTPTest, BigFilesMultipleClients)
{
  constexpr int num_clients     = 5;
  constexpr int file_size_bytes = 1024 * 1024 * 20;

  auto test_working_dir = std::filesystem::current_path();
  auto ftp_root_dir     = test_working_dir / "ftp_root";
  auto local_root_dir   = test_working_dir / "local_root";
  auto upload_dir       = local_root_dir   / "upload_dir";
  auto download_dir     = local_root_dir   / "download_dir";

  if (std::filesystem::exists(ftp_root_dir))
    std::filesystem::remove_all(ftp_root_dir);

  if (std::filesystem::exists(local_root_dir))
    std::filesystem::remove_all(local_root_dir);

  // Make sure that we start clean, so no old dir exists
  ASSERT_FALSE(std::filesystem::exists(ftp_root_dir));
  ASSERT_FALSE(std::filesystem::exists(local_root_dir));

  // Create all directories
  {
    std::filesystem::create_directory(ftp_root_dir);
    std::filesystem::create_directory(local_root_dir);
    std::filesystem::create_directory(upload_dir);
    std::filesystem::create_directory(download_dir);
  
    // Make sure that we were able to create the dir
    ASSERT_TRUE(std::filesystem::is_directory(ftp_root_dir));
    ASSERT_TRUE(std::filesystem::is_directory(local_root_dir));
    ASSERT_TRUE(std::filesystem::is_directory(upload_dir));
    ASSERT_TRUE(std::filesystem::is_directory(download_dir));
  }


  // Start FTP Server
  fineftp::FtpServer server(2121);
  server.start(4);
  
  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Create a file for uploading with the size file_size_bytes and random data
  std::vector<char> random_data(file_size_bytes);
  std::generate(random_data.begin(), random_data.end(), []() { return static_cast<char>(std::rand()); });
  {
    auto local_file = upload_dir / "big_file";    

    std::ofstream ofs(local_file.string(), std::ios::binary | std::ios::out);
    ofs.write(random_data.data(), file_size_bytes);
    ofs.close();

    // Make sure that the file exists
    ASSERT_TRUE(std::filesystem::exists(local_file));
    ASSERT_TRUE(std::filesystem::is_regular_file(local_file));
  }

  // Upload the file to the FTP Server with parallel curl sessions
  {
    std::vector<std::thread> threads;
    threads.reserve(num_clients);
    for (int i = 0; i < num_clients; i++)
    {
      threads.emplace_back([&, i]() {
                            std::string curl_command = "curl -T \"" + (upload_dir / "big_file").string() + "\" \"ftp://localhost:2121/" + std::to_string(i) + "/\" --ftp-create-dirs";
                            auto curl_result = std::system(curl_command.c_str());
                            ASSERT_EQ(curl_result, 0);
                          });
    }
    for (auto& thread : threads)
    {
      thread.join();
    }

    // Check that all files exist in the ftp dir
    for (int i = 0; i < num_clients; i++)
    {
      ASSERT_TRUE(std::filesystem::exists(ftp_root_dir / (std::to_string(i) + "/big_file")));
      ASSERT_TRUE(std::filesystem::is_regular_file(ftp_root_dir / (std::to_string(i) + "/big_file")));
    }
  }

  // Download the files again, with num_client curl calls
  {
    std::vector<std::thread> threads;
    threads.reserve(num_clients);
    for (int i = 0; i < num_clients; i++)
    {
      threads.emplace_back([&, i]() {
                            std::string curl_command_download = "curl -o \"" + (download_dir / ("big_file_download_" + std::to_string(i))).string() + "\" \"ftp://localhost:2121/" + std::to_string(i) + "/big_file\"";
                            auto curl_result = std::system(curl_command_download.c_str());
                            ASSERT_EQ(curl_result, 0);
                          });
    }
    for (auto& thread : threads)
    {
      thread.join();
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Make sure that the files are identical to the random_data
  {
    std::string random_data_str(random_data.data(), random_data.size());
    auto        random_data_hash = std::hash<std::string>{}(random_data_str);

    for (int i = 0; i < num_clients; i++)
    {
      auto local_file_download = download_dir / ("big_file_download_" + std::to_string(i));
      ASSERT_TRUE(std::filesystem::exists(local_file_download));
      ASSERT_TRUE(std::filesystem::is_regular_file(local_file_download));

      // Read the file and compare it to the random_data variable
      std::ifstream ifs(local_file_download.string(), std::ios::binary);
      std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
      auto content_hash = std::hash<std::string>{}(content);

      ASSERT_EQ(content.size(), random_data_str.size());
      ASSERT_EQ(content_hash, random_data_hash);
    }
  }

  // Stop the server
  server.stop();
}
#endif

#if 1
TEST(FineFTPTest, UploadAndRename)
{
  constexpr int num_clients            = 10;
  constexpr int num_uploads_per_client = 10;

  auto test_working_dir = std::filesystem::current_path();
  auto ftp_root_dir     = test_working_dir / "ftp_root";
  auto local_root_dir   = test_working_dir / "local_root";
  auto upload_dir       = local_root_dir   / "upload_dir";
  auto download_dir     = local_root_dir   / "download_dir";

  // Create local root and ftp dir
  {
    if (std::filesystem::exists(ftp_root_dir))
          std::filesystem::remove_all(ftp_root_dir);

    if (std::filesystem::exists(local_root_dir))
      std::filesystem::remove_all(local_root_dir);

    // Make sure that we start clean, so no old dir exists
    ASSERT_FALSE(std::filesystem::exists(ftp_root_dir));
    ASSERT_FALSE(std::filesystem::exists(local_root_dir));

    // Create dirs
    std::filesystem::create_directory(ftp_root_dir);
    std::filesystem::create_directory(local_root_dir);
    std::filesystem::create_directory(upload_dir);
    std::filesystem::create_directory(download_dir);

    // Make sure all dirs exist
    ASSERT_TRUE(std::filesystem::is_directory(ftp_root_dir));
    ASSERT_TRUE(std::filesystem::is_directory(local_root_dir));
    ASSERT_TRUE(std::filesystem::is_directory(upload_dir));
    ASSERT_TRUE(std::filesystem::is_directory(download_dir));
  }

  // Create a small hello world file in the upload dir
  {
    auto local_file = upload_dir / "hello_world.txt";
    std::ofstream ofs(local_file.string());
    ofs << "Hello World";
    ofs.close();
    
    // Make sure that the file exists
    ASSERT_TRUE(std::filesystem::exists(local_file));
    ASSERT_TRUE(std::filesystem::is_regular_file(local_file));
  }

  // Start the server
  fineftp::FtpServer server(2121);
  server.start(4);

  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Upload the file to the FTP Server with parallel curl sessions
  {
    std::vector<std::thread> threads;
    threads.reserve(num_clients);
    for (int i = 0; i < num_clients; i++)
    {
      threads.emplace_back([&, i]() {
                            for (int j = 0; j < num_uploads_per_client; j++)
                            {
                              // Create target filename having the client and upload index in the name
                              auto upload_target_filename = std::to_string(i) + "_" + std::to_string(j) + ".txt";
                              auto rename_target_filename = std::to_string(i) + "_" + std::to_string(j) + "_renamed.txt";

                              std::string curl_command = "curl -T \"" + (upload_dir / "hello_world.txt").string() + "\" "
                                                                + " \"ftp://localhost:2121/" + upload_target_filename + "\" "
                                                                + " --ftp-create-dirs "
                                                                + " -Q -\"RNFR " + upload_target_filename + "\" "
                                                                + " -Q -\"RNTO " + rename_target_filename + "\" ";

                              auto curl_result = std::system(curl_command.c_str());
                              ASSERT_EQ(curl_result, 0);

                              // Make sure that the file exists, but in the renamed version
                              ASSERT_FALSE(std::filesystem::exists(ftp_root_dir / upload_target_filename));
                              ASSERT_TRUE(std::filesystem::exists(ftp_root_dir / rename_target_filename));
                              ASSERT_TRUE(std::filesystem::is_regular_file(ftp_root_dir / rename_target_filename));
                            }
                          });
    }

    // Wait for all curl upload commands to finish
    for (auto& thread : threads)
    {
      thread.join();
    }
  }
}
#endif
