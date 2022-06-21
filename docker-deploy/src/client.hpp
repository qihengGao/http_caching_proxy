#ifndef __CLIENT_HPP__
#define __CLIENT_HPP__
#include <unistd.h>

#include <cstddef>
#include <string>

#include "cache.hpp"

class Client {
 private:
  size_t id;
  int socket;
  std::string ip;

 public:
  Client(size_t id, int socket, std::string& ip)
      : id(id), socket(socket), ip(ip) {}
  size_t getId() { return this->id; }
  int getSocket() { return this->socket; }
  std::string& getIp() { return this->ip; }
};
#endif