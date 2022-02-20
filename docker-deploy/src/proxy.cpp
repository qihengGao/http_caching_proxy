#include "proxy.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

std::ofstream _log("/var/log/erss/proxy.log");
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
Cache sharedCache;
void* serveClient(void* clientPtr);

void Proxy::startService() {
  int listen_fd = initServer(this->portNum);
  if (listen_fd == -1) {
    return;
  }
  int connect_fd;
  while (true) {
    std::string ipReader;
    connect_fd = acceptClient(listen_fd, ipReader);
    if (connect_fd == -1) {
      continue;
    }
    pthread_t thread;
    Client* client = new Client(this->clientCount, connect_fd, ipReader);
    ++this->clientCount;
    pthread_create(&thread, NULL, serveClient, client);
  }
}

void* serveClient(void* clientPtr) {
  Client* client = (Client*)clientPtr;
  char recvbuf[65536];
  int readBytes = recv(client->getSocket(), &recvbuf, sizeof(recvbuf), 0);
  if (readBytes <= 0) {
    delete client;
    return NULL;
  }
  std::string requeststr(recvbuf, readBytes);
  Request* request = new Request(client->getId(), client->getIp(), requeststr);
  if (request->getMethod() == "GET") {
    int toServer_fd = Proxy::connectToServer(request);
    if (toServer_fd == -1) {
      return NULL;
    }
    Proxy::recordNewRequest(request);
    Proxy::getHandler(request, client, toServer_fd, readBytes);
    close(toServer_fd);
  } else if (request->getMethod() == "POST") {
    int toServer_fd = Proxy::connectToServer(request);
    if (toServer_fd == -1) {
      return NULL;
    }
    Proxy::recordNewRequest(request);
    Proxy::postHandler(request, client, toServer_fd, readBytes);
    close(toServer_fd);
  } else if (request->getMethod() == "CONNECT") {
    int toServer_fd = Proxy::connectToServer(request);
    if (toServer_fd == -1) {
      return NULL;
    }
    Proxy::recordNewRequest(request);
    Proxy::connectHandler(request, client, toServer_fd, readBytes);
    Proxy::recordTunnelClose(request);
    close(toServer_fd);
  } else {
    std::string status("HTTP/1.1 400 Bad Request");
    Proxy::respondRequest(request, status);
    status += "\r\n\r\n";
    Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
  }
  close(client->getSocket());
  delete client;
  delete request;
  return NULL;
}

void Proxy::getHandler(Request* request, Client* client, int toServer_fd,
                       int readBytes) {
  Proxy::recordRequesting(request);
  MyTime now;
  if (sharedCache.has(request->getUrl())) {
    // use cache
    Response& resp_incache = sharedCache.get(request->getUrl());
    if (resp_incache.ifNoCache()) {
      // if no-cache, revalidate
      Proxy::revalidate(resp_incache, client, request, toServer_fd, now);
    } else {
      if (resp_incache.isFresh(now)) {
        // use cache
        Proxy::recordValidCache(request);
        Proxy::respondRequest(request, resp_incache.getLine());
        int status = sendAll(client->getSocket(), resp_incache.getMsg(), resp_incache.getMsgSize());
        if (status == -1) {
          return;
        }
      } else {
        // revalidate
        Proxy::recordExpired(request, resp_incache);
        revalidate(resp_incache, client, request, toServer_fd, now);
      }
    }
  } else {
    Proxy::recordNotInCache(request);
    // cannot find in cache
    // request
    char msg[65536];
    memset(msg, 0, sizeof(msg));
    strcpy(msg, request->getMsg().c_str());
    send(toServer_fd, msg, readBytes + 1, 0);
    char response[65536];
    int response_len = recv(toServer_fd, response, sizeof(response), 0);
    if (response_len <= 0) {
      return;
    }
    std::string msgStr(response, response_len);
    // check 502
    if (msgStr.find("\r\n\r\n") == std::string::npos) {
      std::string status("HTTP/1.1 502 Bad Gateway");
      Proxy::respondRequest(request, status);
      status += "\r\n\r\n";
      Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
      return;
    }
    Response resp(msgStr);
    Proxy::recordResponse(request, resp);
    Proxy::respondRequest(request, resp.getLine());
    if (resp.isChunked()) {
      Proxy::recordNotCachable(request, std::string("chunked transfer"));
      int status =
          sendAll(client->getSocket(), resp.getMsg(), resp.getMsgSize());
      if (status == -1) {
        return;
      }
      char chunkedBuff[response_len];
      int nByte;
      while ((nByte = recv(toServer_fd, chunkedBuff, sizeof(chunkedBuff), 0)) >
             0) {
        send(client->getSocket(), chunkedBuff, nByte, 0);
      }
      if (nByte <= 0) {
        Proxy::recordError(client->getId(),
                           " in sending response back to client");
        std::string status("HTTP/1.1 502 Bad Gateway");
        Proxy::respondRequest(request, status);
        status += "\r\n\r\n";
        Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
        return;
      }
    } else {
      // no chunk, check if-save
      // not chunked, continue to recv
      int currReceived = response_len;
      while (currReceived < resp.getMsgSize()) {
        int nBytes = recv(toServer_fd, &resp.msg.data()[currReceived],
                          resp.getMsgSize() - currReceived, 0);
        if (nBytes <= 0) {
          Proxy::recordError(client->getId(),
                             " in sending response back to client");
          std::string status("HTTP/1.1 502 Bad Gateway");
          Proxy::respondRequest(request, status);
          status += "\r\n\r\n";
          Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
          return;
        }
        currReceived += nBytes;
      }
      if (resp.ifSaveInCache()) {
        // if save true, save
        sharedCache.add(request->getUrl(), resp, now);
        if (resp.ifNoCache()) {
          Proxy::recordNoCache(request);
        } else {
          Proxy::recordWillExpired(request, resp);
        }
      } else {
        Proxy::recordNotCachable(request,
                                 std::string("has 'private' or 'no-store'"));
      }
      // send back user
      int status;
      status = sendAll(client->getSocket(), resp.getMsg(), resp.getMsgSize());
      if (status == -1) {
        return;
      }
    }
  }
}

void Proxy::revalidate(Response& resp, Client* client, Request* request,
                       int toServer_fd, MyTime now) {
  Proxy::recordNeedValid(request);
  std::string msg = request->getMsg();
  if (resp.hasLastModify()) {
    std::stringstream ss;
    ss << "If-Modified-Since: " << resp.getLastModify().getPrintTime()
       << "\r\n";
    msg.insert(request->getInsertPos(), ss.str());
  }
  if (resp.getEtag() != "") {
    std::stringstream ss;
    ss << "If-None-Match: " << resp.getEtag() << "\r\n";
    msg.insert(request->getInsertPos(), ss.str());
  }
  char req_send[msg.size() + 1];
  memset(req_send, 0, sizeof(req_send));
  strcpy(req_send, msg.c_str());
  send(toServer_fd, &req_send, sizeof(req_send), 0);
  char resp_from_server[65536];
  int response_len = recv(toServer_fd, &resp_from_server,
                          sizeof(resp_from_server), MSG_WAITALL);
  std::string tem(resp_from_server);
  if (tem.find("\r\n\r\n") == std::string::npos) {
    std::string status("HTTP/1.1 502 Bad Gateway");
    Proxy::respondRequest(request, status);
    status += "\r\n\r\n";
    Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
    return;
  }
  Response temm(tem);
  if (tem.find("304 Not Modified") != -1) {
    Proxy::recordValidCache(request);
    Proxy::recordResponse(request, temm);
    Proxy::respondRequest(request, resp.getLine());
    int statuss = sendAll(client->getSocket(), resp.getMsg(), resp.getMsgSize());
    if (statuss == -1) {
      return;
    }
  } else {
    Response resp(std::string(resp_from_server, response_len));
    Proxy::recordResponse(request, resp);
    Proxy::respondRequest(request, resp.getLine());
    if (resp.isChunked()) {
      Proxy::recordNotCachable(request, std::string("chunked transfer"));
      send(client->getSocket(), resp.getMsg(), resp.getMsgSize(), 0);
      char chunkedBuff[response_len];
      int nByte;
      while ((nByte = recv(toServer_fd, chunkedBuff, sizeof(chunkedBuff), 0)) >
             0) {
        send(client->getSocket(), chunkedBuff, nByte, 0);
      }
      if (nByte <= 0) {
        Proxy::recordError(client->getId(),
                           " in sending response back to client");
        std::string status("HTTP/1.1 502 Bad Gateway");
        Proxy::respondRequest(request, status);
        status += "\r\n\r\n";
        Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
        return;
      }
    } else {
      // no chunk, check if-save
      int currReceived = response_len;
      while (currReceived < resp.getMsgSize()) {
        int nBytes = recv(toServer_fd, &resp.msg.data()[currReceived],
                          resp.getMsgSize() - currReceived, 0);
        if (nBytes <= 0) {
          Proxy::recordError(client->getId(),
                             " in sending response back to client");
          std::string status("HTTP/1.1 502 Bad Gateway");
          Proxy::respondRequest(request, status);
          status += "\r\n\r\n";
          Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
          return;
        }
        currReceived += nBytes;
      }
      if (resp.ifSaveInCache()) {
        // if save true, save
        sharedCache.add(request->getUrl(), resp, now);
        if (resp.ifNoCache()) {
          Proxy::recordNoCache(request);
        } else {
          Proxy::recordWillExpired(request, resp);
        }
      } else {
        Proxy::recordNotCachable(request,
                                 std::string("has 'private' or 'no-store'"));
      }
      // send back user
      int status =
          sendAll(client->getSocket(), resp.getMsg(), resp.getMsgSize());
      if (status == -1) {
        return;
      }
    }
  }
}

void Proxy::postHandler(Request* request, Client* client, int toServer_fd,
                        int readBytes) {
  char msg[65536];
  memset(msg, 0, sizeof(msg));
  strcpy(msg, request->getMsg().c_str());
  Proxy::recordRequesting(request);
  send(toServer_fd, msg, readBytes + 1, 0);
  char response[65536];
  int response_len = recv(toServer_fd, response, sizeof(response), 0);
  std::string msgStr(response);
  if (msgStr.find("\r\n\r\n") == std::string::npos) {
    std::string status("HTTP/1.1 502 Bad Gateway");
    Proxy::respondRequest(request, status);
    status += "\r\n\r\n";
    Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
    return;
  }
  Response resp(std::string(response, response_len));
  Proxy::recordResponse(request, resp);
  Proxy::respondRequest(request, resp.getLine());
  if (resp.isChunked()) {
    int status = sendAll(client->getSocket(), resp.getMsg(), resp.getMsgSize());
    if (status == -1) {
      return;
    }
    char chunkedBuff[65536];
    int nByte;
    while ((nByte = recv(toServer_fd, chunkedBuff, sizeof(chunkedBuff), 0)) >
           0) {
      send(client->getSocket(), chunkedBuff, nByte, 0);
    }
    if (nByte <= 0) {
      Proxy::recordError(client->getId(), " in sending response back to client");
      std::string status("HTTP/1.1 502 Bad Gateway");
      Proxy::respondRequest(request, status);
      status += "\r\n\r\n";
      Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
      return;
    }
  } else {
    int currReceived = response_len;
    while (currReceived < resp.getMsgSize()) {
      int nBytes = recv(toServer_fd, &resp.msg.data()[currReceived],
                        resp.getMsgSize() - currReceived, 0);
      if (nBytes <= 0) {
        Proxy::recordError(client->getId(),
                           " in sending response back to client");
        std::string status("HTTP/1.1 502 Bad Gateway");
        Proxy::respondRequest(request, status);
        status += "\r\n\r\n";
        Proxy::sendAll(client->getSocket(), status.c_str(), status.length());
        return;
      }
      currReceived += nBytes;
    }
    int status;
    status = sendAll(client->getSocket(), resp.getMsg(), resp.getMsgSize());
    if (status == -1) {
      return;
    }
  }
}

void Proxy::connectHandler(Request* request, Client* client, int toServer_fd,
                           int readBytes) {
  Proxy::recordRequesting(request);
  std::string messageStr("HTTP/1.1 200 OK\r\n\r\n");
  Proxy::respondRequest(request, "HTTP/1.1 200 OK");
  char message[messageStr.size() + 1];
  strcpy(message, messageStr.c_str());
  send(client->getSocket(), message, sizeof(message), 0);
  fd_set toMonitor;
  int maxSocketNum = std::max(toServer_fd, client->getSocket()) + 1;
  while (true) {
    FD_ZERO(&toMonitor);
    FD_SET(toServer_fd, &toMonitor);
    FD_SET(client->getSocket(), &toMonitor);
    select(maxSocketNum, &toMonitor, NULL, NULL, NULL);
    int numBytes;
    if (FD_ISSET(toServer_fd, &toMonitor)) {
      char message[65536];
      numBytes = recv(toServer_fd, message, sizeof(message), 0);
      if (numBytes <= 0) {
        return;
      } else {
        if (send(client->getSocket(), message, numBytes, 0) <= 0) {
          return;
        }
      }
    }
    if (FD_ISSET(client->getSocket(), &toMonitor)) {
      char message[65536];
      numBytes = recv(client->getSocket(), message, sizeof(message), 0);
      if (numBytes <= 0) {
      } else {
        if (send(toServer_fd, message, numBytes, 0) <= 0) {
          return;
        }
      }
    }
  }
}


//cite from ECE650 Example
int Proxy::initServer(const char* port) {
  int status;
  int socket_fd;
  struct addrinfo host_info;
  struct addrinfo* host_info_list;

  memset(&host_info, 0, sizeof(host_info));
  host_info.ai_family = AF_UNSPEC;
  host_info.ai_socktype = SOCK_STREAM;  // TCP
  host_info.ai_flags = AI_PASSIVE;      // use my IP

  status = getaddrinfo(NULL, port, &host_info, &host_info_list);
  if (status != 0) {
    return -1;
  }

  // Ask os to automatically assign idle port
  if (strlen(port) == 0) {
    struct sockaddr_in* addr_in =
        (struct sockaddr_in*)(host_info_list->ai_addr);
    addr_in->sin_port = 0;
  }

  socket_fd = socket(host_info_list->ai_family, host_info_list->ai_socktype,
                     host_info_list->ai_protocol);
  if (socket_fd == -1) {
    return -1;
  }

  int yes = 1;
  status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  status = bind(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
  if (status == -1) {
    return -1;
  }

  status = listen(socket_fd, 100);
  if (status == -1) {
    return -1;
  }

  freeaddrinfo(host_info_list);
  return socket_fd;
}

/**
 * Accept client and restore the client's to ip.
 **/
 //cite from ECE650 Example
int Proxy::acceptClient(int socket_fd, std::string& ip) {
  struct sockaddr_storage socket_addr;
  socklen_t socket_addr_len = sizeof(socket_addr);
  int client_connection_fd;

  client_connection_fd =
      accept(socket_fd, (struct sockaddr*)&socket_addr, &socket_addr_len);
  if (client_connection_fd == -1) {
    return -1;
  }

  struct sockaddr_in* addr = (struct sockaddr_in*)&socket_addr;

  ip = inet_ntoa(addr->sin_addr);

  return client_connection_fd;
}

//cite from ECE650 Example
int Proxy::connectToServer(Request* request) {
  const char* hostName = request->getHostname().c_str();
  const char* port = request->getPort().c_str();
  int status;
  int socket_fd;
  struct addrinfo host_info;
  struct addrinfo* host_info_list;

  memset(&host_info, 0, sizeof(host_info));
  host_info.ai_family = AF_UNSPEC;
  host_info.ai_socktype = SOCK_STREAM;  // TCP

  status = getaddrinfo(hostName, port, &host_info, &host_info_list);
  if (status != 0) {
    return -1;
  }

  socket_fd = socket(host_info_list->ai_family, host_info_list->ai_socktype,
                     host_info_list->ai_protocol);
  if (socket_fd == -1) {
    return -1;
  }

  status =
      connect(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
  if (status == -1) {
    return -1;
  }
  freeaddrinfo(host_info_list);
  return socket_fd;
}

int Proxy::sendAll(int socket, const char* buf, size_t bytesLeft) {
  size_t total = 0;
  int n;
  while (total < bytesLeft) {
    n = send(socket, buf + total, bytesLeft, 0);
    if (n == -1) break;
    total += n;
    bytesLeft -= n;
  }
  return (n == -1) ? -1 : 0;
}

// ID: "REQUEST" from IP @ TIME
void Proxy::recordNewRequest(Request* request) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": \"" << request->getMethod() << " "
       << request->getUrl() << " " << request->getProtocol() << "\" from "
       << request->getIp() << " @ " << request->getReceiveTime().getPrintTime()
       << std::endl;
  std::cout << request->getId() << ": \"" << request->getMethod() << " "
            << request->getUrl() << " " << request->getProtocol() << "\" from "
            << request->getIp() << " @ "
            << request->getReceiveTime().getPrintTime() << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: not in cache
void Proxy::recordNotInCache(Request* request) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": not in cache" << std::endl;
  std::cout << request->getId() << ": not in cache" << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: in cache, requires validation
void Proxy::recordNeedValid(Request* request) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": in cache, requires validation" << std::endl;
  std::cout << request->getId() << ": in cache, requires validation"
            << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: in cache, but expired at EXPIREDTIME
void Proxy::recordExpired(Request* request, Response& response) {
  time_t expiretime;
  std::string res;
  if (response.hasMaxAge()) {
    expiretime = (time_t)((response.getMaxAge()) + (response.getDate()).now);
    char* dt = ctime(&expiretime);
    std::string tem(dt);
    res = tem.substr(0, tem.size() - 1);
  } else if (response.hasExpireTime()) {
    std::string tem = response.getExpireTime().getPrintTime();
    res = tem;
  } else {
    std::string tem = "there is no expired time in header";
    res = tem;
  }
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": in cache, but expired at " << res << std::endl;
  std::cout << request->getId() << ": in cache, but expired at " << res
            << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: in cache, valid
void Proxy::recordValidCache(Request* request) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": in cache, valid" << std::endl;
  std::cout << request->getId() << ": in cache, valid" << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: Requsting "REQUEST" from SERVER
void Proxy::recordRequesting(Request* request) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": Requesting \"" << request->getMethod() << " "
       << request->getUrl() << " " << request->getProtocol() << "\" from "
       << request->getUrl() << std::endl;
  std::cout << request->getId() << ": Requesting\"" << request->getMethod()
            << " " << request->getUrl() << " " << request->getProtocol()
            << "\" from " << request->getUrl() << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: Received "RESPONSE" from SERVER
void Proxy::recordResponse(Request* request, Response& response) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": Received: " << response.getLine() << " from "
       << request->getIp() << std::endl;
  std::cout << request->getId() << ": Received: " << response.getLine()
            << " from " << request->getIp() << std::endl;
  pthread_mutex_unlock(&mutex);
}

void Proxy::recordNotCachable(Request* request, const std::string& reason) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": not cacheable because " << reason << ""
       << std::endl;
  std::cout << request->getId() << ": not cacheable because " << reason << ""
            << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: cached, but requires re-validation,done
void Proxy::recordNoCache(Request* request) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << " cached, but requires re-validation"
       << std::endl;
  std::cout << request->getId() << " cached, but requires re-validation"
            << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: cached, expires at EXPIRES, done
void Proxy::recordWillExpired(Request* request, Response& response) {
  time_t expiretime;
  std::string res;
  if (response.hasMaxAge()) {
    expiretime = (time_t)((response.getMaxAge()) + (response.getDate()).now);
    // std::cout << response.getMaxAge()
    //           << " :date: " << response.getDate().getPrintTime() <<
    //           std::endl;
    char* dt = ctime(&expiretime);
    std::string tem(dt);
    res = tem.substr(0, tem.size() - 1);
  } else if (response.hasExpireTime()) {
    std::string tem = response.getExpireTime().getPrintTime();
    res = tem;
  } else {
    std::string tem = "there is no expired time in header";
    res = tem;
  }
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": cached, expires at " << res << std::endl;
  std::cout << request->getId() << ": cached, expires at " << res << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: proxy responds client
void Proxy::respondRequest(Request* request, const std::string& status) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": Responding \"" << status << "\"" << std::endl;
  std::cout << request->getId() << ": Responding \"" << status << "\""
            << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: Tunnel closed
void Proxy::recordTunnelClose(Request* request) {
  pthread_mutex_lock(&mutex);
  _log << request->getId() << ": Tunnel closed" << std::endl;
  std::cout << request->getId() << ": Tunnel closed" << std::endl;
  pthread_mutex_unlock(&mutex);
}

// ID: ERROR Msg
void Proxy::recordError(size_t id, const std::string& reason) {
  pthread_mutex_lock(&mutex);
  std::string idStr = (id >= 0) ? std::to_string(id) : "(no-id)";
  _log << idStr << ": ERROR" << reason << std::endl;
  pthread_mutex_unlock(&mutex);
}