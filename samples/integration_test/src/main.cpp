#include <fineftp/server.h>

int main() {

  fineftp::FtpServer server(2121);
  server.start(4);

  return 0;
}
