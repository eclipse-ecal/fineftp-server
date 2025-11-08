#pragma once

#include <functional>
#include <string>

namespace fineftp
{
    /**
     * @brief Enumeration of FTP command for file operations.
     * This enum is used to identify FTP commands that perform filesystem operations.
     */
    enum class command_type {
        FTP_CMD_NONE = 0, /* No Command (to be used as placeholder) */
        FTP_CMD_RNFR, /* Rename From */
        FTP_CMD_RNTO, /* Rename To */
        FTP_CMD_APPE, /* Append File */
        FTP_CMD_DELE, /* Delete File */
        FTP_CMD_MKD,  /* Create New Directory */
        FTP_CMD_RMD,  /* Remove Directory */
        FTP_CMD_REST, /* Restart File Transfer (Not Implemented) */
        FTP_CMD_ALLO, /* Allocate (Not Implemented) */
        FTP_CMD_PWD,  /* Print Working Directory */
        FTP_CMD_CWD,  /* Change Working Directory */
        FTP_CMD_CDUP, /* Change to Parent Directory */
        FTP_CMD_STOR, /* Store File*/
        FTP_CMD_STOU, /* Store Unique File (Not Implemented)*/
        FTP_CMD_RETR  /* Retrieve File*/
    };

    /**
     * @brief Type definition for FTP command callback function.
     * This callback is invoked for FTP commands that perform filesystem operations.
     * The callback should be lightweight and thread-safe, as it will be called synchronously
     * during command processing.
     * @param command_type The type of FTP command being executed.
     * @param std::string The argument associated with the command (e.g., file or directory path).
     */
    using FtpCommandCallback = std::function<void(const command_type, const std::string&)>;

}