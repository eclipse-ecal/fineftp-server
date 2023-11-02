#include <chrono>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>

#include <fineftp/server.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef WIN32
#include <win_str_convert.h>
#endif // WIN32

#if 1
TEST(FineFTPTest, SimpleUploadDownload) {
  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";

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
    const std::string curl_command = "curl -S -s -T \"" + local_file.string() + "\" \"ftp://localhost:2121/\"";
    const auto curl_result = std::system(curl_command.c_str());

    // Make sure that the upload was successful
    ASSERT_EQ(curl_result, 0);

    // Make sure that the file exists in the FTP root dir
    auto ftp_file = ftp_root_dir / "hello_world.txt";
    ASSERT_TRUE(std::filesystem::exists(ftp_file));
    ASSERT_TRUE(std::filesystem::is_regular_file(ftp_file));

    // Make sure that the file has the same content
    std::ifstream ifs(ftp_file.string());
    const std::string content((std::istreambuf_iterator<char>(ifs)),
            (std::istreambuf_iterator<char>()));
    ASSERT_EQ(content, "Hello World");
  }

  // Download the file again
  {
    const std::string curl_command_download = "curl -S -s -o \"" + local_root_dir.string() + "/hello_world_download.txt\" \"ftp://localhost:2121/hello_world.txt\"";
    const auto curl_result = std::system(curl_command_download.c_str());
    ASSERT_EQ(curl_result, 0);

    // Make sure that the files are identical
    auto local_file_download = local_root_dir / "hello_world_download.txt";

    ASSERT_TRUE(std::filesystem::exists(local_file_download));
    ASSERT_TRUE(std::filesystem::is_regular_file(local_file_download));

    std::ifstream ifs(local_file_download.string());
    const std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));

    ASSERT_EQ(content, "Hello World");
  }

  // Stop the server
  server.stop();
}
#endif

#if 1
TEST(FineFTPTest, BigFilesMultipleClients)
{
  constexpr int num_clients     = 10;
  constexpr int file_size_bytes = 1024 * 1024 * 20;

  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";
  const auto upload_dir       = local_root_dir   / "upload_dir";
  const auto download_dir     = local_root_dir   / "download_dir";

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
                            const std::string curl_command = "curl -S -s -T \"" + (upload_dir / "big_file").string() + "\" \"ftp://localhost:2121/" + std::to_string(i) + "/\" --ftp-create-dirs";
                            const auto curl_result = std::system(curl_command.c_str());
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
                            const std::string curl_command_download = "curl -S -s -o \"" + (download_dir / ("big_file_download_" + std::to_string(i))).string() + "\" \"ftp://localhost:2121/" + std::to_string(i) + "/big_file\"";
                            const auto curl_result = std::system(curl_command_download.c_str());
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
    const std::string random_data_str(random_data.data(), random_data.size());
    const auto        random_data_hash = std::hash<std::string>{}(random_data_str);

    for (int i = 0; i < num_clients; i++)
    {
      auto local_file_download = download_dir / ("big_file_download_" + std::to_string(i));
      ASSERT_TRUE(std::filesystem::exists(local_file_download));
      ASSERT_TRUE(std::filesystem::is_regular_file(local_file_download));

      // Read the file and compare it to the random_data variable
      std::ifstream ifs(local_file_download.string(), std::ios::binary);
      const std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
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
// Curl will call LIST, RNFR and RNTO to the server with parallel sessions
TEST(FineFTPTest, ListAndRename)
{
  constexpr int num_clients          = 10;
  constexpr int num_files_per_client = 10;

  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";

  // Create ftp root dir
  {
    if (std::filesystem::exists(ftp_root_dir))
          std::filesystem::remove_all(ftp_root_dir);

    // Make sure that we start clean, so no old dir exists
    ASSERT_FALSE(std::filesystem::exists(ftp_root_dir));

    // Create dirs
    std::filesystem::create_directory(ftp_root_dir);

    // Make sure all dirs exist
    ASSERT_TRUE(std::filesystem::is_directory(ftp_root_dir));
  }

  // Fill the ftp root dir with files
  {
    for (int i = 0; i < num_clients; i++)
    {
      for (int j = 0; j < num_files_per_client; j++)
      {
        auto upload_target_filename = std::to_string(i) + "_" + std::to_string(j) + ".txt";
        auto upload_target_file     = ftp_root_dir / upload_target_filename;

        std::ofstream ofs(upload_target_file.string());
        ofs << "Hello World";
        ofs.close();

        // Make sure that the file exists
        ASSERT_TRUE(std::filesystem::exists(upload_target_file));
        ASSERT_TRUE(std::filesystem::is_regular_file(upload_target_file));
      }
    }
  }

  // Start the server
  fineftp::FtpServer server(2121);
  server.start(10);

  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Rename the files with parallel curl sessions
  {
    std::vector<std::thread> threads;
    threads.reserve(num_clients);
    for (int i = 0; i < num_clients; i++)
    {
      threads.emplace_back([&, i]() {
                            for (int j = 0; j < num_files_per_client; j++)
                            {
                              // Create target filename having the client and upload index in the name
                              auto upload_target_filename = std::to_string(i) + "_" + std::to_string(j) + ".txt";
                              auto rename_target_filename = std::to_string(i) + "_" + std::to_string(j) + "_renamed.txt";

#ifdef WIN32
                              const std::string curl_output_file = "NUL";
#else // WIN32
                              const std::string curl_output_file = "/dev/null";
#endif // WIN32

                              const std::string curl_command = "curl -Q \"RNFR " + upload_target_filename + "\" "
                                                              + " -Q \"RNTO " + rename_target_filename + "\" "
                                                              + " -S -s "
                                                              + " -o " + curl_output_file + " "
                                                              +" \"ftp://localhost:2121/\"";

                              const auto curl_result = std::system(curl_command.c_str());
                              //if (curl_result != 0)
                              //{
                              //  std::cerr << "Curl Command LIST + RNFR" << upload_target_filename << " + RNTO " << rename_target_filename << "Failed" << std::endl;
                              //}
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

#if 1
TEST(FineFTPTest, UploadAndRename)
{
  constexpr int num_clients            = 20;
  constexpr int num_uploads_per_client = 20;

  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";
  const auto upload_dir       = local_root_dir   / "upload_dir";
  const auto download_dir     = local_root_dir   / "download_dir";

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

                              const std::string curl_command = "curl -T \"" + (upload_dir / "hello_world.txt").string() + "\" "
                                                                + " \"ftp://localhost:2121/" + upload_target_filename + "\" "
                                                                + " --ftp-create-dirs "
                                                                + " -S -s "
                                                                + " -Q -\"RNFR " + upload_target_filename + "\" "
                                                                + " -Q -\"RNTO " + rename_target_filename + "\" ";

                              const auto curl_result = std::system(curl_command.c_str());

                              auto s = ftp_root_dir / upload_target_filename;
                              if (std::filesystem::exists(s))
                              {
                                auto err = std::error_code{};
                                auto sz  = std::filesystem::file_size(s, err);
                                std::cerr << "error: " << s.string() << " of size " << sz << std::endl;
                              }

                              auto t = ftp_root_dir / rename_target_filename;
                              if (!std::filesystem::exists(t))
                              {
                                std::cerr << "error: " << t.string() << " does not exist" << std::endl;
                              }

                              if (0 != curl_result)
                              {
                                std::cerr
                                  << "error: " << curl_result << " returned by curl when operating on "
                                  << upload_target_filename << std::endl;
                              }

                              // Check the return value of curl 
                              ASSERT_EQ(curl_result, 0);
                              
                              // Make sure that the file exists, but in the renamed version
                              ASSERT_TRUE(std::filesystem::exists(ftp_root_dir / rename_target_filename));
                              ASSERT_TRUE(std::filesystem::is_regular_file(ftp_root_dir / rename_target_filename));
                              ASSERT_FALSE(std::filesystem::exists(ftp_root_dir / upload_target_filename));
                              
                              // Check the stored file size
                              auto err         = std::error_code{};
                              auto stored_size = std::filesystem::file_size(ftp_root_dir / rename_target_filename, err);
                              
                              if (stored_size != 11)
                              {
                                // Wait a little and retry
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                stored_size = std::filesystem::file_size(ftp_root_dir / rename_target_filename, err);
                              }

                              ASSERT_EQ(stored_size, 11);
                              ASSERT_FALSE(bool{err});
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

#if 1
TEST(FineFTPTest, UploadAndRenameOriginal)
{
  constexpr int num_clients            = 20;
  constexpr int num_uploads_per_client = 20;

  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";
  const auto upload_dir       = local_root_dir   / "upload_dir";
  const auto download_dir     = local_root_dir   / "download_dir";

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
  server.start(10);

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

                              const std::string curl_command = "curl -T \"" + (upload_dir / "hello_world.txt").string() + "\" "
                                                                + " \"ftp://localhost:2121/" + upload_target_filename + "\" "
                                                                + " --ftp-create-dirs "
                                                                + " -S -s "
                                                                + " -Q -\"RNFR " + upload_target_filename + "\" "
                                                                + " -Q -\"RNTO " + rename_target_filename + "\" ";

                              const auto curl_result = std::system(curl_command.c_str());
                              ASSERT_EQ(curl_result, 0);

                              // Make sure that the file exists, but in the renamed version
                              ASSERT_FALSE(std::filesystem::exists(ftp_root_dir / upload_target_filename));
                              ASSERT_TRUE(std::filesystem::exists(ftp_root_dir / rename_target_filename));
                              ASSERT_TRUE(std::filesystem::is_regular_file(ftp_root_dir / rename_target_filename));

                              // Check size of the file
                              auto err         = std::error_code{};
                              auto stored_size = std::filesystem::file_size(ftp_root_dir / rename_target_filename, err);
                              ASSERT_EQ(stored_size, 11);
                              ASSERT_FALSE(bool{err});
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

#if 1
TEST(FineFTPTest, UploadAndRenameDifferentDirs)
{
  constexpr int num_clients            = 5;
  constexpr int num_uploads_per_client = 5;

  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";
  const auto upload_dir       = local_root_dir   / "upload_dir";
  const auto download_dir     = local_root_dir   / "download_dir";

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

  // Pre-populate the ftp_root dir with subdirs, one for each client
  {
    for (int i = 0; i < num_clients; i++)
    {
      std::filesystem::create_directory(ftp_root_dir / std::to_string(i));
      ASSERT_TRUE(std::filesystem::is_directory(ftp_root_dir / std::to_string(i)));
    }
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
  server.start(1);

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
                              // target_dir
                              auto upload_target_dir = std::to_string(i);

                              // Create target filename having the client and upload index in the name
                              auto upload_target_filename = std::to_string(i) + "_" + std::to_string(j) + ".txt";
                              auto rename_target_filename = std::to_string(i) + "_" + std::to_string(j) + "_renamed.txt";

                              const std::string curl_command = "curl -T \"" + (upload_dir / "hello_world.txt").string() + "\" "
                                                                + " \"ftp://localhost:2121/" + upload_target_dir + "/" + upload_target_filename + "\" "
                                                                + " -S -s "
                                                                + " -Q -\"RNFR /" + upload_target_dir + "/" + upload_target_filename + "\" "
                                                                + " -Q -\"RNTO /" + upload_target_dir + "/" + rename_target_filename + "\" ";

                              const auto curl_result = std::system(curl_command.c_str());
                              ASSERT_EQ(curl_result, 0);

                              // Make sure that the file exists, but in the renamed version
                              ASSERT_FALSE(std::filesystem::exists(ftp_root_dir / upload_target_dir / upload_target_filename));
                              ASSERT_TRUE(std::filesystem::exists(ftp_root_dir / upload_target_dir / rename_target_filename));
                              ASSERT_TRUE(std::filesystem::is_regular_file(ftp_root_dir / upload_target_dir / rename_target_filename));
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

#if 1
TEST(FineFTPTest, UploadAndRenameAnotherFile)
{
  constexpr int num_clients            = 10;
  constexpr int num_uploads_per_client = 10;

  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";
  const auto upload_dir       = local_root_dir   / "upload_dir";
  const auto download_dir     = local_root_dir   / "download_dir";

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

  // Create a hello world file for each client and each uploaded file in the ftp root dir
  {
    for (int i = 0; i < num_clients; i++)
    {
      for (int j = 0; j < num_uploads_per_client; j++)
      {
          auto file_for_renaming_filename = std::to_string(i) + "_" + std::to_string(j) + "_for_renaming.txt";
          auto file_for_renaming          = ftp_root_dir / file_for_renaming_filename;
    
          std::ofstream ofs(file_for_renaming.string());
          ofs << "Hello World";
          ofs.close();
    
          // Make sure that the file exists
          ASSERT_TRUE(std::filesystem::exists(file_for_renaming));
          ASSERT_TRUE(std::filesystem::is_regular_file(file_for_renaming));
          
          // Check if the files are the right size
          ASSERT_EQ(std::filesystem::file_size(file_for_renaming), 11);
      }
    }
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
  server.start(10);

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

                              auto filename_for_renaming  = std::to_string(i) + "_" + std::to_string(j) + "_for_renaming.txt";
                              auto filename_renamed       = filename_for_renaming + "_renamed.txt";

                              const std::string curl_command = "curl -T \"" + (upload_dir / "hello_world.txt").string() + "\" "
                                                                + " \"ftp://localhost:2121/" + "/" + upload_target_filename + "\" "
                                                                + " -S -s "
                                                                + " -Q -\"RNFR /" + filename_for_renaming + "\" "
                                                                + " -Q -\"RNTO /" + filename_renamed + "\" ";

                              const auto curl_result = std::system(curl_command.c_str());
                              ASSERT_EQ(curl_result, 0);

                              // Make sure that the uploaded file exists
                              ASSERT_TRUE(std::filesystem::exists(ftp_root_dir / upload_target_filename));
                              ASSERT_TRUE(std::filesystem::is_regular_file(ftp_root_dir / upload_target_filename));

                              // Make sure that the renamed file exists, but only in the renamed form
                              ASSERT_FALSE(std::filesystem::exists(ftp_root_dir / filename_for_renaming));
                              ASSERT_TRUE(std::filesystem::exists(ftp_root_dir / filename_renamed));
                              ASSERT_TRUE(std::filesystem::is_regular_file(ftp_root_dir / filename_renamed));
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

#if 1
TEST(FineFTPTest, UTF8Paths)
{
  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";
  const auto upload_dir       = local_root_dir   / "upload_dir";
  const auto download_dir     = local_root_dir   / "download_dir";

  const std::string utf8_laughing_emoji = "\xF0\x9F\x98\x82";
  const std::string utf8_beermug_emoji  = "\xF0\x9F\x8D\xBA";
  const std::string utf8_german_letter_UE = "\xC3\x9C";
  const std::string utf8_greek_letter_OMEGA = "\xCE\xA9";

  const std::string upload_subdir_utf8 = "dir_" + utf8_laughing_emoji + utf8_german_letter_UE;
  const std::string filename_utf8      = "file_" + utf8_beermug_emoji + utf8_greek_letter_OMEGA + ".txt";
#ifdef WIN32
  const std::wstring upload_subdir_wstr = fineftp::StrConvert::Utf8ToWide(upload_subdir_utf8);
  const std::wstring filename_wstr      = fineftp::StrConvert::Utf8ToWide(filename_utf8);
#endif // WIN32

#ifdef WIN32
  // For Windows we need to use the wstring / UTF-16 functions to be independent from the system configuration
  const auto upload_subdir   = upload_dir / upload_subdir_wstr;
  const auto local_file_path = upload_subdir / filename_wstr;
  const std::string local_file_path_utf8 = fineftp::StrConvert::WideToUtf8(local_file_path.wstring());
#else // WIN32
  // For all other operating systems we assume that they use UTF-8 out of the box
  const auto upload_subdir   = upload_dir / upload_subdir_utf8;
  const auto local_file_path = upload_subdir / filename_utf8;
  const std::string local_file_path_utf8 = local_file_path.string();
#endif // WIN32

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
    std::filesystem::create_directory(upload_subdir);
    std::filesystem::create_directory(download_dir);

    // Make sure all dirs exist
    ASSERT_TRUE(std::filesystem::is_directory(ftp_root_dir));
    ASSERT_TRUE(std::filesystem::is_directory(local_root_dir));
    ASSERT_TRUE(std::filesystem::is_directory(upload_dir));
    ASSERT_TRUE(std::filesystem::is_directory(upload_subdir));
    ASSERT_TRUE(std::filesystem::is_directory(download_dir));
  }

  // Start the server
  fineftp::FtpServer server(2121);
  server.start(4);

  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Create a small hello world file in the upload dir
  {
#ifdef WIN32
    std::ofstream ofs(local_file_path.wstring());
#else // WIN32
    std::ofstream ofs(local_file_path.string());
#endif // WIN32
    ofs << "Hello World";
    ofs.close();
    
    // Make sure that the file exists
    ASSERT_TRUE(std::filesystem::exists(local_file_path));
    ASSERT_TRUE(std::filesystem::is_regular_file(local_file_path));
  }

  // Upload the upload dir to the server with curl. Make sure to let curl create subdirs automatically
  {
    const std::string curl_command_utf8 = "curl  -S -s -T \"" + local_file_path_utf8 + "\" \"ftp://localhost:2121/" + utf8_laughing_emoji + "/\" --ftp-create-dirs";
#ifdef WIN32
    const auto curl_result = _wsystem(fineftp::StrConvert::Utf8ToWide(curl_command_utf8).c_str());
    const auto target_file_path_in_ftp_root = ftp_root_dir / fineftp::StrConvert::Utf8ToWide(utf8_laughing_emoji) / filename_wstr;
#else
    const auto curl_result = std::system(curl_command_utf8.c_str());
    const auto target_file_path_in_ftp_root = ftp_root_dir / utf8_laughing_emoji / filename_utf8;
#endif // WIN32

    ASSERT_EQ(curl_result, 0);

    // Make sure that the file exists
    ASSERT_TRUE(std::filesystem::exists(target_file_path_in_ftp_root));
    ASSERT_TRUE(std::filesystem::is_regular_file(target_file_path_in_ftp_root));
  }

  // Download the file again to the download dir.
  {
    const std::string curl_command_download_utf8 = "curl  -S -s -o \"" + (download_dir / filename_utf8).string() + "\" \"ftp://localhost:2121/" + utf8_laughing_emoji + "/" + filename_utf8 + "\"";
#ifdef WIN32
    const auto curl_result = _wsystem(fineftp::StrConvert::Utf8ToWide(curl_command_download_utf8).c_str());
    const auto target_file_path_in_download_dir = download_dir / filename_wstr;
#else
    const auto curl_result = std::system(curl_command_download_utf8.c_str());
    const auto target_file_path_in_download_dir = download_dir / filename_utf8;
#endif // WIN32

    ASSERT_EQ(curl_result, 0);
    
    // Make sure that the file exists
    ASSERT_TRUE(std::filesystem::exists(target_file_path_in_download_dir));
    ASSERT_TRUE(std::filesystem::is_regular_file(target_file_path_in_download_dir));
  }
}
#endif

#if 1
TEST(FineFTPTest, AppendToFile) {
  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";

  {
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
  }

  fineftp::FtpServer server(2121);
  server.start(1);

  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Create a hello world.txt file in the local root dir and write "Hello World" into it
  auto local_file = local_root_dir / "hello_world.txt";
  {
    std::ofstream ofs(local_file.string());
    ofs << "Hello World";
    ofs.close();
  }

  // Make sure that the file exists
  ASSERT_TRUE(std::filesystem::exists(local_file));
  ASSERT_TRUE(std::filesystem::is_regular_file(local_file));

  // create a hello world.txt file in the ftp root dir and write "HELLO WORLD" into it
  auto ftp_file = ftp_root_dir / "hello_world.txt";
  {
    std::ofstream ofs(ftp_file.string());
    ofs << "HELLO WORLD";
    ofs.close();
  }

  // Make sure that the file exists
  ASSERT_TRUE(std::filesystem::exists(ftp_file));
  ASSERT_TRUE(std::filesystem::is_regular_file(ftp_file));

  // Append the local file to the ftp file
  {
    const std::string curl_command = "curl -S -s -T \"" + local_file.string() + "\" \"ftp://localhost:2121/hello_world.txt\" --append";
    const auto curl_result = std::system(curl_command.c_str());

    // Make sure that the upload was successful
    ASSERT_EQ(curl_result, 0);

    // Make sure that the file exists in the FTP root dir
    ASSERT_TRUE(std::filesystem::exists(ftp_file));
    ASSERT_TRUE(std::filesystem::is_regular_file(ftp_file));

    // Make sure that the file has the same content
    std::ifstream ifs(ftp_file.string());
    const std::string content((std::istreambuf_iterator<char>(ifs)),
                 (std::istreambuf_iterator<char>()));

    ASSERT_EQ(content, "HELLO WORLDHello World");
  }

  // Stop the server
  server.stop();
}
#endif

#if 1
// According to the RFC 959, a normal upload (STOR) must replace an already existing file
TEST(FineFTPTest, ReplaceFile) {
  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_root_dir     = test_working_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";

  {
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
  }

  fineftp::FtpServer server(2121);
  server.start(1);

  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Create a hello world.txt file in the local root dir and write "Hello World" into it
  auto local_file = local_root_dir / "hello_world.txt";
  {
    std::ofstream ofs(local_file.string());
    ofs << "Hello World";
    ofs.close();
  }

  // Make sure that the file exists
  ASSERT_TRUE(std::filesystem::exists(local_file));
  ASSERT_TRUE(std::filesystem::is_regular_file(local_file));

  // create a hello world.txt file in the ftp root dir and write "HELLO WORLD" into it
  auto ftp_file = ftp_root_dir / "hello_world.txt";
  {
    std::ofstream ofs(ftp_file.string());
    ofs << "HELLO WORLD";
    ofs.close();
  }

  // Make sure that the file exists
  ASSERT_TRUE(std::filesystem::exists(ftp_file));
  ASSERT_TRUE(std::filesystem::is_regular_file(ftp_file));

  // Replace the FTP file with the local file
  {
    const std::string curl_command = "curl -S -s -T \"" + local_file.string() + "\" \"ftp://localhost:2121/hello_world.txt\"";
    const auto curl_result = std::system(curl_command.c_str());

    // Make sure that the upload was successful
    ASSERT_EQ(curl_result, 0);

    // Make sure that the file exists in the FTP root dir
    ASSERT_TRUE(std::filesystem::exists(ftp_file));
    ASSERT_TRUE(std::filesystem::is_regular_file(ftp_file));

    // Make sure that the file has the same content
    std::ifstream ifs(ftp_file.string());
    const std::string content((std::istreambuf_iterator<char>(ifs)),
                 (std::istreambuf_iterator<char>()));

    ASSERT_EQ(content, "Hello World");
  }

  // Stop the server
  server.stop();
}
#endif

#if 1
// This test checks whether the user can access a path above the root dir. This
// had been the case in the past, so now there is a special test for it to make
// sure this never happens again.
TEST(FineFTPTest, PathVulnerability)
{
  const auto test_working_dir = std::filesystem::current_path();
  const auto ftp_toplevel_dir = test_working_dir / "ftp_toplevel_dir";
  const auto ftp_root_dir     = ftp_toplevel_dir / "ftp_root";
  const auto local_root_dir   = test_working_dir / "local_root";

  // Create local root and ftp dir
  {
    if (std::filesystem::exists(ftp_toplevel_dir))
      std::filesystem::remove_all(ftp_toplevel_dir);
    
    if (std::filesystem::exists(local_root_dir))
      std::filesystem::remove_all(local_root_dir);
    
    // Make sure that we start clean, so no old dir exists
    ASSERT_FALSE(std::filesystem::exists(ftp_toplevel_dir));
    ASSERT_FALSE(std::filesystem::exists(local_root_dir));
    
    // Create dirs
    std::filesystem::create_directory(ftp_toplevel_dir);
    std::filesystem::create_directory(ftp_root_dir);
    std::filesystem::create_directory(local_root_dir);
    
    // Make sure all dirs exist
    ASSERT_TRUE(std::filesystem::is_directory(ftp_toplevel_dir));
    ASSERT_TRUE(std::filesystem::is_directory(ftp_root_dir));
    ASSERT_TRUE(std::filesystem::is_directory(local_root_dir));
  }

  // Start the server
  fineftp::FtpServer server(2121);
  server.start(4);

  server.addUserAnonymous(ftp_root_dir.string(), fineftp::Permission::All);

  // Create a small hello world file in the ftp_toplevel_dir
  {
    auto local_file_path = ftp_toplevel_dir / "hello_world.txt";
    std::ofstream ofs(local_file_path.string());
    ofs << "Hello World";
    ofs.close();
        
    // Make sure that the file exists
    ASSERT_TRUE(std::filesystem::exists(local_file_path));
    ASSERT_TRUE(std::filesystem::is_regular_file(local_file_path));
  }

  // Retrieve size of the file with curl (Absolute root, relative path). The file should not be accessible
  {
    const std::string curl_command = "curl \"ftp://localhost:2121/\" -Q \"SIZE /../hello_world.txt\"";
    const auto curl_result = std::system(curl_command.c_str());
    ASSERT_NE(curl_result, 0);
  }

  // Retrieve size of the file with curl (Pure relative path). The file should not be accessible
  {
    const std::string curl_command = "curl \"ftp://localhost:2121/\" -Q \"SIZE ../hello_world.txt\"";
    const auto curl_result = std::system(curl_command.c_str());
    ASSERT_NE(curl_result, 0);
  }
}
#endif
