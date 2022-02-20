#ifndef __PROXY_HPP__
#define __PROXY_HPP__
#include <cstddef>
#include <fstream>

#include "cache.hpp"
#include "client.hpp"
#include "request.hpp"
#include "response.hpp"

class Proxy {
 private:
  const char* portNum;
  size_t clientCount;

 public:
  Proxy(const char* portNum) : portNum(portNum), clientCount(0) {}
  void startService();
  static int initServer(const char* port);
  static int acceptClient(int socket, std::string& ip);
  static int connectToServer(Request* request);
  static int sendAll(int socket, const char* buf, size_t bytesLeft);
  static void getHandler(Request* request, Client* client, int toServer_fd,
                         int readBytes);
  static void revalidate(Response& resp, Client* client, Request* request,
                         int toServer_fd, MyTime now);
  static void responseHandler(std::string& response_tem, Client* client,
                              Request* request, int toServer_fd, MyTime now,
                              size_t response_len);
  static void postHandler(Request* request, Client* client, int toServer_fd,
                          int readBytes);
  static void connectHandler(Request* request, Client* client, int toServer_fd,
                             int readBytes);
  static void recordNewRequest(Request* request);
  static void recordRequesting(Request* request);
  static void recordResponse(Request* request, Response& Response);
  static void recordTunnelClose(Request* request);
  static void recordNotInCache(Request* request);
  static void recordNeedValid(Request* request);
  static void recordValidCache(Request* request);
  static void recordNotCachable(Request* request, const std::string& reason);
  static void respondRequest(Request* request, const std::string& status);
  static void recordExpired(Request* request, Response& response);
  static void recordNoCache(Request* request);
  static void recordWillExpired(Request* request, Response& response);
  static void recordError(size_t id, const std::string& reason);
};

#endif