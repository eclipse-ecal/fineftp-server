#include "user_database.h"

#include <iostream>


namespace fineftp
{
  UserDatabase::UserDatabase() // NOLINT(modernize-use-equals-default) Reason: I don't want to change API / ABI and wnat the possibility to implement a non-trivial version that still is ABI compatible
  {}

  UserDatabase::~UserDatabase() // NOLINT(modernize-use-equals-default) Reason: I don't want to change API / ABI and wnat the possibility to implement a non-trivial version that still is ABI compatible
  {}

  bool UserDatabase::addUser(const std::string& username, const std::string& password, const std::string& local_root_path, Permission permissions)
  {
    const std::lock_guard<decltype(database_mutex_)> database_lock(database_mutex_);

    if (isUsernameAnonymousUser(username))
    {
      if (anonymous_user_)
      {
        std::cerr << "Error adding user with username \"" << username << "\". The username denotes the anonymous user, which is already present." << std::endl;
        return false;
      }
      else
      {
        anonymous_user_ = std::make_shared<FtpUser>(password, local_root_path, permissions);
#ifndef NDEBUG
        std::cout << "Successfully added anonymous user." << std::endl;
#endif // !NDEBUG
        return true;
      }
    }
    else
    {
      auto user_it = database_.find(username);
      if (user_it == database_.end())
      {
        database_.emplace(username, std::make_shared<FtpUser>(password, local_root_path, permissions));
#ifndef NDEBUG
        std::cout << "Successfully added user \"" << username << "\"." << std::endl;
#endif // !NDEBUG
        return true;
      }
      else
      {
        std::cerr << "Error adding user with username \"" << username << "\". The user already exists." << std::endl;
        return false;
      }
    }
  }

  std::shared_ptr<FtpUser> UserDatabase::getUser(const std::string& username, const std::string& password) const
  {
    const std::lock_guard<decltype(database_mutex_)> database_lock(database_mutex_);

    if (isUsernameAnonymousUser(username))
    {
      return anonymous_user_;
    }
    else
    {
      auto user_it = database_.find(username);
      if (user_it == database_.end())
      {
        return nullptr;
      }
      else
      {
        if (user_it->second->password_ == password)
          return user_it->second;
        else
          return nullptr;
      }
    }
  }

  bool UserDatabase::isUsernameAnonymousUser(const std::string& username) const // NOLINT(readability-convert-member-functions-to-static) Reason: I don't want to break the ABI. Otherwise this is a good finding and should be accepted.
  {
    return (username.empty()
      || username == "ftp"
      || username == "anonymous");
  }

}
