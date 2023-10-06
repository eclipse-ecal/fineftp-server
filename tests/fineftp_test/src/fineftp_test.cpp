#include <gtest/gtest.h>

#include <fineftp/server.h>

#include <filesystem>
#include <string>
#include <fstream>

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
