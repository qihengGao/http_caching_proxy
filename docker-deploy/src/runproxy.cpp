#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fcntl.h>
#include<sys/stat.h>

#include "proxy.hpp"

int main() {

  //daemon
  if (fork() != 0) {
    exit(0);
  }
  setsid();
  int fd = open( "/dev/null", O_RDWR );
  dup2( fd, 0 );
  dup2( fd, 1 );
  dup2( fd, 2 );
  chdir("/");
  umask(0);
  if (fork() != 0) {
    exit(0);
  }

  //start program
  Proxy proxy("12345");
  proxy.startService();
  return EXIT_SUCCESS;
}