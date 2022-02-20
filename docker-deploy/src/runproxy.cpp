#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "proxy.hpp"

int main() {
  Proxy proxy("12345");
  proxy.startService();
  return EXIT_SUCCESS;
}