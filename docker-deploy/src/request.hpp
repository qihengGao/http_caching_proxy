#ifndef __REQUEST_HPP__
#define __REQUEST_HPP__
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include "mytime.hpp"
// TODO: handlse invalid input
class Request {
 private:
  size_t id;
  std::string ip;
  MyTime receiveTime;
  std::string msg;
  std::string firstLine;
  std::string method;
  std::string url;
  std::string hostname;
  std::string port;
  std::string protocol;
  int insert_pos;

 public:
  Request(size_t _id, std::string& _ip, std::string& msg)
      : id(_id), ip(_ip), receiveTime() {
    this->msg = msg;
    int method_end = msg.find(" ");
    method = msg.substr(0, method_end);
    int url_end = msg.find(" ", method_end + 1);
    url = msg.substr(method_end + 1, url_end - method_end - 1);
    int protocol_end = msg.find("\r\n", url_end + 1);
    protocol = msg.substr(url_end + 1, protocol_end - url_end - 1);
    int host_index = msg.find("Host");
    if (method == "CONNECT") {
      int host_end = msg.find(":", host_index + 5);
      hostname = msg.substr(host_index + 6, host_end - host_index - 6);
      int url_end = msg.find("\r\n", host_index + 5);
      port = msg.substr(host_end + 1, url_end - host_end - 1);
      insert_pos = url_end + 2;
    } else {
      int host_end = msg.find("\r\n", host_index + 1);
      hostname = msg.substr(host_index + 6, host_end - host_index - 6);
      insert_pos = host_end + 2;
      port = "80";
    }
    std::stringstream ss;
    ss << method << " " << url << " " << protocol;
    this->firstLine = ss.str();
  }

  size_t getId() { return id; }
  std::string& getFirstLine() { return firstLine; }
  std::string& getIp() { return ip; }
  std::string& getMethod() { return method; }
  std::string& getUrl() { return url; }
  std::string& getHostname() { return hostname; }
  std::string& getPort() { return port; }
  std::string& getProtocol() { return protocol; }
  MyTime& getReceiveTime() { return receiveTime; }
  std::string& getMsg() { return msg; }
  int& getInsertPos() { return insert_pos; }
};
#endif