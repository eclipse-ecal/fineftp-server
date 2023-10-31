#include <gtest/gtest.h>

#include <fineftp/server.h>

#include <filesystem>
#include <string>
#include <fstream>
#include <thread>
#include <algorithm>

#ifdef WIN32
#include <win_str_convert.h>
#endif // WIN32

namespace
{
  struct DirPreparer
  {
    DirPreparer()
    {
      // Make sure, the local_root_dir and local_ftp_root_dir don't exist, yet
      if (std::filesystem::exists(local_root_dir))
        std::filesystem::remove_all(local_root_dir);

      if (std::filesystem::exists(local_ftp_root_dir))
        std::filesystem::remove_all(local_ftp_root_dir);

      // Create root directories again
      std::filesystem::create_directories(local_ftp_root_dir);
      std::filesystem::create_directories(local_root_dir);

      // Create local dirs and files
      std::filesystem::create_directories(local_upload_dir);
      std::filesystem::create_directories(local_download_dir);
      std::ofstream(local_file_1) << local_file_1_content;
      std::ofstream(local_file_2) << local_file_2_content;

      // Create FTP dirs and files
      std::filesystem::create_directories(local_ftp_root_dir / ftp_subdir_a_empty);
      std::filesystem::create_directories(local_ftp_root_dir / ftp_subdir_b_full);
      std::ofstream(local_ftp_root_dir / ftp_file_b1) << ftp_file_b1_content;
      std::ofstream(local_ftp_root_dir / ftp_file_b2) << ftp_file_b2_content;
    }

    ~DirPreparer()
    {
      // Remove all directories and files
      std::filesystem::remove_all(local_root_dir);
      std::filesystem::remove_all(local_ftp_root_dir);
    }

    // Root dir for the entire test
    const std::filesystem::path test_working_dir   = std::filesystem::current_path();;

    // Directories for the FTP Server and the local root
    const std::filesystem::path local_ftp_root_dir = test_working_dir / "ftp_root";
    const std::filesystem::path local_root_dir     = test_working_dir / "local_root";

    // FTP dirs and files
    const std::filesystem::path ftp_subdir_a_empty = "a";
    const std::filesystem::path ftp_subdir_b_full  = "b";
    const std::filesystem::path ftp_file_b1        = ftp_subdir_b_full / "b1.txt";
    const std::filesystem::path ftp_file_b2        = ftp_subdir_b_full / "b2.txt";

    const std::string ftp_file_b1_content = "Hello World";
    const std::string ftp_file_b2_content = "HELLO WORLD!!!";

    // Local dirs and files
    const std::filesystem::path local_upload_dir   = local_root_dir   / "upload_dir";
    const std::filesystem::path local_download_dir = local_root_dir   / "download_dir";

    const std::filesystem::path local_file_1 = local_upload_dir / "1.txt";
    const std::filesystem::path local_file_2 = local_upload_dir / "2.txt";

    const std::string local_file_1_content = "Foo";
    const std::string local_file_2_content = "Bar";
  };
}

#if 1
TEST(PermissionTest, UploadNewFile)
{
  // Uploading a new file only needs Write permission

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        { fineftp::Permission::All, true},
        { fineftp::Permission::None, false},
        { fineftp::Permission::DirList | fineftp::Permission::FileWrite, true},
        { fineftp::Permission::All & (~fineftp::Permission::FileWrite), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    // Create curl string to upload a file to a new location
    std::string curl_command = std::string("curl -T ")
                            + " \"" + dir_preparer.local_file_1.string() + "\" "
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "/test.txt\""
                            + " -s -S ";

    auto curl_result = std::system(curl_command.c_str());

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file exists and has the correct content
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "test.txt"));

      std::ifstream ifs((dir_preparer.local_ftp_root_dir / "test.txt").string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
              (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.local_file_1_content);
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the file does not exist
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "test.txt"));
    }
  }
}
#endif

#if 1
TEST(PermissionTest, OverwriteFile)
{
  // Overwriting a file needs Write and Delete permissions

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        {fineftp::Permission::All, true},
        {fineftp::Permission::None, false},
        {fineftp::Permission::DirList | fineftp::Permission::FileWrite, false},
        {fineftp::Permission::DirList | fineftp::Permission::FileDelete, false},
        {fineftp::Permission::DirList | fineftp::Permission::FileWrite | fineftp::Permission::FileDelete, true},
        {fineftp::Permission::All & (~fineftp::Permission::FileWrite), false},
        {fineftp::Permission::All & (~fineftp::Permission::FileDelete), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    std::string ftp_target_path = "/" + dir_preparer.ftp_file_b1.string();
    // Create curl string to upload a file to a new location
    std::string curl_command = std::string("curl -T ")
                            + " \"" + dir_preparer.local_file_1.string() + "\" "
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + ftp_target_path + "\""
                            + " -s -S ";

    auto curl_result = std::system(curl_command.c_str());

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file exists and has the new content
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));

      std::ifstream ifs((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1).string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
              (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.local_file_1_content);
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the file exists and has the old conent
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));

      std::ifstream ifs((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1).string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.ftp_file_b1_content);
    }
  }
}
#endif

#if 1
TEST(PermissionTest, AppendToFile)
{
  // Appendign to a file needs Append Permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        {fineftp::Permission::All, true},
        {fineftp::Permission::None, false},
        {fineftp::Permission::DirList | fineftp::Permission::FileAppend, true},
        {fineftp::Permission::All & (~fineftp::Permission::FileAppend), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    std::string ftp_target_path = "/" + dir_preparer.ftp_file_b1.string();
    // Create curl string to upload a file to a new location
    std::string curl_command = std::string("curl -T ")
                            + " \"" + dir_preparer.local_file_1.string() + "\" "
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + ftp_target_path + "\""
                            + " -s -S --append";

    auto curl_result = std::system(curl_command.c_str());

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file exists and has the new content
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));

      std::ifstream ifs((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1).string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
              (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.ftp_file_b1_content + dir_preparer.local_file_1_content);
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the file exists and has the old conent
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));

      std::ifstream ifs((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1).string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.ftp_file_b1_content);
    }
  }
}
#endif

#if 1
TEST(PermissionTest, RenameFile)
{
  // Renaming a file needs FileRename Permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        {fineftp::Permission::All, true},
        {fineftp::Permission::None, false},
        {fineftp::Permission::DirList | fineftp::Permission::FileRename, true},
        {fineftp::Permission::All & (~fineftp::Permission::FileRename), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    std::string ftp_source_path = "/" + dir_preparer.ftp_file_b1.string();
    std::string ftp_target_path = "/" + dir_preparer.ftp_file_b1.string() + "_renamed.txt";

#ifdef WIN32
                              std::string curl_output_file = "NUL";
#else // WIN32
                              std::string curl_output_file = "/dev/null";
#endif // WIN32

    std::string curl_command = "curl -Q \"RNFR " + ftp_source_path + "\" "
                                    + " -Q \"RNTO " + ftp_target_path + "\" "
                                    + " -S -s "
                                    + " -o " + curl_output_file + " "
                                    + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

    auto curl_result = std::system(curl_command.c_str());

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file exists at the new location only
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / (dir_preparer.ftp_file_b1.string() + "_renamed.txt")));

      // Make sure that the file has the correct content
      std::ifstream ifs((dir_preparer.local_ftp_root_dir / (dir_preparer.ftp_file_b1.string() + "_renamed.txt")).string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.ftp_file_b1_content);
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the file exists at the old location only
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / (dir_preparer.ftp_file_b1.string() + "_renamed.txt")));

      // Make sure that the file has the correct content
      std::ifstream ifs((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1).string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
                                (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.ftp_file_b1_content);
    }
  }
}
#endif

#if 1
TEST(PermissionTest, DeleteFile)
{
  // Deleting a file needs FileDelete Permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        {fineftp::Permission::All, true},
        {fineftp::Permission::None, false},
        {fineftp::Permission::DirList | fineftp::Permission::FileDelete, true},
        {fineftp::Permission::All & (~fineftp::Permission::FileDelete), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    std::string ftp_source_path = "/" + dir_preparer.ftp_file_b1.string();

#ifdef WIN32
                              std::string curl_output_file = "NUL";
#else // WIN32
                              std::string curl_output_file = "/dev/null";
#endif // WIN32

    std::string curl_command = "curl -Q \"DELE " + ftp_source_path + "\" "
                                    + " -S -s "
                                    + " -o " + curl_output_file + " "
                                    + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

    auto curl_result = std::system(curl_command.c_str());

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file does not exist anymore
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the file still exists exists
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));

      // Make sure that the file has the correct content
      std::ifstream ifs((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1).string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
                                (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.ftp_file_b1_content);
    }
  }
}
#endif
