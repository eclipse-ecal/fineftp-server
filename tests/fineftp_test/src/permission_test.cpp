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
    int status = std::system(command.c_str());
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
      std::filesystem::create_directories(local_curl_output_dir);
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
    const std::filesystem::path ftp_subdir_a_empty = "dir_a";
    const std::filesystem::path ftp_subdir_b_full  = "dir_b";
    const std::filesystem::path ftp_file_b1        = ftp_subdir_b_full / "b1.txt";
    const std::filesystem::path ftp_file_b2        = ftp_subdir_b_full / "b2.txt";

    const std::string ftp_file_b1_content = "Hello World";
    const std::string ftp_file_b2_content = "HELLO WORLD!!!";

    // Local dirs and files
    const std::filesystem::path local_upload_dir      = local_root_dir   / "upload_dir";
    const std::filesystem::path local_download_dir    = local_root_dir   / "download_dir";
    const std::filesystem::path local_curl_output_dir = local_root_dir   / "curl_out";

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

    auto curl_result = system_execute(curl_command);

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
TEST(PermissionTest, UploadNewFileToNewDir)
{
  // Uploading a new file only needs Write permission

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        { fineftp::Permission::All, true},
        { fineftp::Permission::None, false},
        { fineftp::Permission::DirList | fineftp::Permission::FileWrite, false},
        { fineftp::Permission::DirList |  fineftp::Permission::DirCreate, false},
        { fineftp::Permission::DirList | fineftp::Permission::FileWrite | fineftp::Permission::DirCreate, true},
        { fineftp::Permission::All & (~fineftp::Permission::FileWrite), false},
        { fineftp::Permission::All & (~fineftp::Permission::DirCreate), false},
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
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "/newdir/test.txt\""
                            + " -s -S --ftp-create-dirs";

    auto curl_result = system_execute(curl_command);

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file exists and has the correct content
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "newdir" / "test.txt"));

      std::ifstream ifs((dir_preparer.local_ftp_root_dir / "newdir" / "test.txt").string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
              (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.local_file_1_content);
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the new file does not exist
      // Attention: The dir may already exist, if the user has DirCreate permission.
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "newdir" / "test.txt"));
    }
  }
}
#endif

#if 1
TEST(PermissionTest, UploadAndOverwriteFile)
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

    auto curl_result = system_execute(curl_command);

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
TEST(PermissionTest, AppendToExistingFile)
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

    auto curl_result = system_execute(curl_command);

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
TEST(PermissionTest, AppendToNewFile)
{
  // Appendign to a new file effectively means that we write a new file. Thus, it needs Write Permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        {fineftp::Permission::All, true},
        {fineftp::Permission::None, false},
        {fineftp::Permission::DirList | fineftp::Permission::FileWrite, true},
        {fineftp::Permission::All & (~fineftp::Permission::FileWrite), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    std::string ftp_target_path = "/newfile.txt";
    // Create curl string to upload a file to a new location
    std::string curl_command = std::string("curl -T ")
                            + " \"" + dir_preparer.local_file_1.string() + "\" "
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + ftp_target_path + "\""
                            + " -s -S --append";

    auto curl_result = system_execute(curl_command);

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file exists and has the new content
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "newfile.txt"));

      std::ifstream ifs((dir_preparer.local_ftp_root_dir / "newfile.txt").string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.local_file_1_content);
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the file does not exist
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "newfile.txt"));
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

    auto curl_result = system_execute(curl_command);

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
TEST(PermissionTest, RenameDir)
{
  // Renaming a dir needs DirRename Permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        {fineftp::Permission::All, true},
        {fineftp::Permission::None, false},
        {fineftp::Permission::DirList | fineftp::Permission::DirRename, true},
        {fineftp::Permission::All & (~fineftp::Permission::DirRename), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_b_full.string();
    std::string ftp_target_path = "/" + dir_preparer.ftp_subdir_b_full.string() + "_renamed";

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

    auto curl_result = system_execute(curl_command);

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the dir exists at the new location only
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_b_full));
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / (dir_preparer.ftp_subdir_b_full.string() + "_renamed")));
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the dir exists at the old location only
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_b_full));
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / (dir_preparer.ftp_subdir_b_full.string() + "_renamed")));
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

    auto curl_result = system_execute(curl_command);

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

#if 1
TEST(PermissionTest, DeleteEmptyDir)
{
  // Deleting dirs need DirDelete Permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        {fineftp::Permission::All, true},
        {fineftp::Permission::None, false},
        {fineftp::Permission::DirList | fineftp::Permission::DirDelete, true},
        {fineftp::Permission::All & (~fineftp::Permission::DirDelete), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_a_empty.string();

#ifdef WIN32
                              std::string curl_output_file = "NUL";
#else // WIN32
                              std::string curl_output_file = "/dev/null";
#endif // WIN32

    std::string curl_command = "curl -Q \"RMD " + ftp_source_path + "\" "
                                    + " -S -s "
                                    + " -o " + curl_output_file + " "
                                    + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

    auto curl_result = system_execute(curl_command);

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the dir does not exist anymore
      ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_a_empty));
    }
    else
    {
      // Test for Failure
      ASSERT_NE(curl_result, 0);

      // Make sure that the dir still exists
      ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_a_empty));
    }
  }
}
#endif

#if 1
TEST(PermissionTest, DownloadFile)
{
  // Downloading a file needs FileRead Permissions

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        { fineftp::Permission::All, true},
        { fineftp::Permission::None, false},
        { fineftp::Permission::DirList | fineftp::Permission::FileRead, true},
        { fineftp::Permission::All & (~fineftp::Permission::FileRead), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    // Create curl string to download a file to the local download dir
    std::filesystem::path download_path = dir_preparer.local_download_dir / "test.txt";
    std::string curl_command = std::string("curl -o ")
                            + " \"" + download_path.string() + "\" "
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "/" + dir_preparer.ftp_file_b1.string() + "\" "
                            + " -s -S ";

    auto curl_result = system_execute(curl_command);

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Make sure that the file exists and has the correct content
      ASSERT_TRUE(std::filesystem::exists(download_path));

      std::ifstream ifs(download_path.string());
      std::string content((std::istreambuf_iterator<char>(ifs)),
                       (std::istreambuf_iterator<char>()));
      ASSERT_EQ(content, dir_preparer.ftp_file_b1_content);
    }
    else
    {
      // Test for Failure
      // ASSERT_EQ(curl_result, curl_return_code_resource_does_not_exist);
      ASSERT_NE(curl_result, 0); // We test for != 0 here, as the curl return values differ too much between different Operating Systems

      // Make sure that the file does not exist
      ASSERT_FALSE(std::filesystem::exists(download_path));
    }
  }
}
#endif

#if 1
TEST(PermissionTest, ListFilesWithLIST)
{
  // Listing a directory needs DirList permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        { fineftp::Permission::All, true},
        { fineftp::Permission::None, false},
        { fineftp::Permission::DirList, true},
        { fineftp::Permission::All & (~fineftp::Permission::DirList), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    // Create curl string to LIST the root dir
    std::filesystem::path curl_output_file = dir_preparer.local_download_dir / "list.txt";

    std::string curl_command = "curl \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\" "
                                    + " -S -s "
                                    + " -o \"" + curl_output_file.string() + "\" ";

    auto curl_result = system_execute(curl_command);


    std::ifstream ifs(curl_output_file.string());
    std::string curl_output((std::istreambuf_iterator<char>(ifs)),
                                (std::istreambuf_iterator<char>()));

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Check that the curl_output_file is not empty
      ASSERT_NE(curl_output, "");

      // Check that the curl_output contains the names of our both directories
      ASSERT_NE(curl_output.find(dir_preparer.ftp_subdir_a_empty.string()), std::string::npos);
      ASSERT_NE(curl_output.find(dir_preparer.ftp_subdir_b_full.string()), std::string::npos);
    }
    else
    {
      // Even when curl gets a Permission Denied for LIST, it still returns success (0).
      // Check that the curl_output_file is empty
      ASSERT_EQ(curl_output, "");
    }
  }
}
#endif

#if 1
TEST(PermissionTest, ListFilesWithNLST)
{
  // Listing a directory needs DirList permissions only

  const std::vector<std::pair<fineftp::Permission, bool>> permissions_under_test
    = {
        { fineftp::Permission::All, true},
        { fineftp::Permission::None, false},
        { fineftp::Permission::DirList, true},
        { fineftp::Permission::All & (~fineftp::Permission::DirList), false},
      };

  for (const auto permission_pair : permissions_under_test)
  {
    const DirPreparer dir_preparer;

    // Create FTP Server
    fineftp::FtpServer server(0);
    server.start(1);
    uint16_t ftp_port = server.getPort();

    server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), permission_pair.first);

    // Create curl string to LIST the root dir
    std::filesystem::path curl_output_file = dir_preparer.local_download_dir / "list.txt";

    std::string curl_command = "curl --list-only \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\" "
                                    + " -S -s "
                                    + " -o \"" + curl_output_file.string() + "\" ";

    auto curl_result = system_execute(curl_command);


    std::ifstream ifs(curl_output_file.string());
    std::string curl_output((std::istreambuf_iterator<char>(ifs)),
                                (std::istreambuf_iterator<char>()));

    if (permission_pair.second)
    {
      // Test for Success
      ASSERT_EQ(curl_result, 0);

      // Check that the curl_output_file is not empty
      ASSERT_NE(curl_output, "");

      // Check that the curl_output contains the names of our both directories
      ASSERT_NE(curl_output.find(dir_preparer.ftp_subdir_a_empty.string()), std::string::npos);
      ASSERT_NE(curl_output.find(dir_preparer.ftp_subdir_b_full.string()), std::string::npos);
    }
    else
    {
      // Even when curl gets a Permission Denied for LIST, it still returns success (0).
      // Check that the curl_output_file is empty
      ASSERT_EQ(curl_output, "");
    }
  }
}
#endif


/////////////////////////////////
// Commands that always fail
/////////////////////////////////

#if 1
TEST(PermissionTest, WrongLogin)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  // Upload with a wrong password
  {
    std::string curl_command = std::string("curl -T ")
                              + " \"" + dir_preparer.local_file_1.string() + "\" "
                              + " \"ftp://myuser:wrongpass@localhost:" + std::to_string(ftp_port) + "/test.txt\""
                              + " -s -S ";


    auto curl_result = system_execute(curl_command);

    // Test for Failure
    ASSERT_EQ(curl_result, curl_return_code_login_failed);

    // Make sure that the file does not exist
    ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "test.txt"));
  }

  // Upload with a non-existing username
  {
    std::string curl_command = std::string("curl -T ")
                              + " \"" + dir_preparer.local_file_1.string() + "\" "
                              + " \"ftp://wronguser:pass@localhost:" + std::to_string(ftp_port) + "/test.txt\""
                              + " -s -S ";


    auto curl_result = system_execute(curl_command);

    // Test for Failure
    ASSERT_EQ(curl_result, curl_return_code_login_failed);

    // Make sure that the file does not exist
    ASSERT_FALSE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / "test.txt"));
  }
}
#endif

#if 1
TEST(PermissionTest, DeleteFullDirWithRMD)
{
  // Deleting full dirs with the RMD command always fails
  // RFC 959 does not specify a recursive delete command.

  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_b_full.string();

#ifdef WIN32
                            std::string curl_output_file = "NUL";
#else // WIN32
                            std::string curl_output_file = "/dev/null";
#endif // WIN32

  std::string curl_command = "curl -Q \"RMD " + ftp_source_path + "\" "
                                  + " -S -s "
                                  + " -o " + curl_output_file + " "
                                  + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_quote_command_error);

  // Make sure that the dir still exists
  ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_b_full));
}
#endif

#if 1
TEST(PermissionTest, DeleteDirWithDELE)
{
  // Deleting full dirs with the RMD command always fails
  // RFC 959 does not specify a recursive delete command.

  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_a_empty.string();

#ifdef WIN32
                            std::string curl_output_file = "NUL";
#else // WIN32
                            std::string curl_output_file = "/dev/null";
#endif // WIN32

  std::string curl_command = "curl -Q \"DELE " + ftp_source_path + "\" "
                                  + " -S -s "
                                  + " -o " + curl_output_file + " "
                                  + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_quote_command_error);

  // Make sure that the dir still exists
  ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_a_empty));
}
#endif

#if 1
TEST(PermissionTest, UploadToPathThatIsADir)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string target_filename_that_is_a_dir = "/" + dir_preparer.ftp_subdir_a_empty.string();

  // Create curl string to upload a file to a new location
  std::string curl_command = std::string("curl -T ")
                            + " \"" + dir_preparer.local_file_1.string() + "\" "
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + target_filename_that_is_a_dir + "\""
                            + " -s -S ";


  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_upload_failed);

  // Make sure that the dir still exists and that it in fact is a dir
  ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_a_empty));
  ASSERT_TRUE(std::filesystem::is_directory(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_a_empty));
}
#endif

#if 1
TEST(PermissionTest, AppendToPathThatIsADir)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string target_filename_that_is_a_dir = "/" + dir_preparer.ftp_subdir_a_empty.string();

  // Create curl string to upload a file to a new location
  std::string curl_command = std::string("curl -T ")
                            + " \"" + dir_preparer.local_file_1.string() + "\" "
                            + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + target_filename_that_is_a_dir + "\""
                            + " -s -S --append";


  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_upload_failed);

  // Make sure that the dir still exists and that it in fact is a dir
  ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_a_empty));
  ASSERT_TRUE(std::filesystem::is_directory(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_subdir_a_empty));
}
#endif

#if 1
TEST(PermissionTest, RenameNonExistingFile)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_b_full.string();

#ifdef WIN32
                            std::string curl_output_file = "NUL";
#else // WIN32
                            std::string curl_output_file = "/dev/null";
#endif // WIN32

  std::string curl_command = std::string("curl -Q \"RNFR /nonexisting_file\" ")
                                  + " -Q \"RNTO /someotherfile\" "
                                  + " -S -s "
                                  + " -o " + curl_output_file + " "
                                  + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_quote_command_error);
}
#endif

#if 1
TEST(PermissionTest, RenameTargetExistsAlready)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_b_full.string();

#ifdef WIN32
                            std::string curl_output_file = "NUL";
#else // WIN32
                            std::string curl_output_file = "/dev/null";
#endif // WIN32

  std::string rename_source_file = "/" + dir_preparer.ftp_file_b1.string();
  std::string rename_target_file = "/" + dir_preparer.ftp_file_b2.string();

  std::string curl_command = "curl -Q \"RNFR " + rename_source_file + "\" "
                                  + " -Q \"RNTO " + rename_target_file + "\" "
                                  + " -S -s "
                                  + " -o " + curl_output_file + " "
                                  + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_quote_command_error);

  // Test that both old files still exist and have their old content
  ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1));
  ASSERT_TRUE(std::filesystem::exists(dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b2));

  std::ifstream ifs((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b1).string());
  std::string content((std::istreambuf_iterator<char>(ifs)),
                                   (std::istreambuf_iterator<char>()));
  ASSERT_EQ(content, dir_preparer.ftp_file_b1_content);

  ifs = std::ifstream((dir_preparer.local_ftp_root_dir / dir_preparer.ftp_file_b2).string());
  content = std::string((std::istreambuf_iterator<char>(ifs)),
                                        (std::istreambuf_iterator<char>()));
  ASSERT_EQ(content, dir_preparer.ftp_file_b2_content);
}
#endif

#if 1
TEST(PermissionTest, DeleteNonExistingWithDELE)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_b_full.string();

#ifdef WIN32
                            std::string curl_output_file = "NUL";
#else // WIN32
                            std::string curl_output_file = "/dev/null";
#endif // WIN32

  std::string curl_command = std::string("curl -Q \"DELE /nonexisting_file.txt\" ")
                                  + " -S -s "
                                  + " -o " + curl_output_file + " "
                                  + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_quote_command_error);
}
#endif

#if 1
TEST(PermissionTest, DeleteNonExistingWithRMD)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  std::string ftp_source_path = "/" + dir_preparer.ftp_subdir_b_full.string();

#ifdef WIN32
                            std::string curl_output_file = "NUL";
#else // WIN32
                            std::string curl_output_file = "/dev/null";
#endif // WIN32

  std::string curl_command = std::string("curl -Q \"RMD /nonexisting_dir\" ")
                                  + " -S -s "
                                  + " -o " + curl_output_file + " "
                                  + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "\"";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_quote_command_error);
}
#endif

#if 1
TEST(PermissionTest, DownloadNonexistingFile)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  // Create curl string to download a file to the local download dir
  std::filesystem::path download_path = dir_preparer.local_download_dir / "test.txt";
  std::string curl_command = std::string("curl -o ")
                          + " \"" + download_path.string() + "\" "
                          + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "/nonexisting_file.txt\" "
                          + " -s -S ";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_ftp_download_failed);

  // Make sure that the file does not exist
  ASSERT_FALSE(std::filesystem::exists(download_path));
}
#endif

#if 1
TEST(PermissionTest, DownloadPathIsADir)
{
  const DirPreparer dir_preparer;

  // Create FTP Server
  fineftp::FtpServer server(0);
  server.start(1);
  uint16_t ftp_port = server.getPort();

  server.addUser("myuser", "mypass", dir_preparer.local_ftp_root_dir.string(), fineftp::Permission::All);

  // Create curl string to download a file to the local download dir
  std::filesystem::path download_path = dir_preparer.local_download_dir / "test.txt";
  std::string curl_command = std::string("curl -o ")
                          + " \"" + download_path.string() + "\" "
                          + " \"ftp://myuser:mypass@localhost:" + std::to_string(ftp_port) + "/" + dir_preparer.ftp_subdir_a_empty.string() + "\" "
                          + " -s -S ";

  auto curl_result = system_execute(curl_command);

  // Test for Failure
  ASSERT_EQ(curl_result, curl_return_code_ftp_download_failed);

  // Make sure that the file does not exist
  ASSERT_FALSE(std::filesystem::exists(download_path));
}
#endif
