#ifndef __RESPONSE_HPP__
#define __RESPONSE_HPP__
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mytime.hpp"

class Response {
 public:
  // std::string msg;
  std::vector<char> msg;

 private:
  std::string max_age;
  std::string date;
  bool chunked;
  bool save_in_cache;
  bool no_cache;
  std::string content_length;
  size_t content_size;
  size_t header_size;
  std::string expire_time;
  std::string etag;
  std::string last_modify;
  std::string line;

 public:
  Response() {}
  Response(const std::string& msgStr) {
    this->msg = std::vector<char>(msgStr.begin(), msgStr.end());
    int empty_line_index = msgStr.find("\r\n\r\n");
    header_size = empty_line_index + 4;
    this->chunked = (msgStr.substr(0, header_size+1).find("chunked") != std::string::npos);
    int max_age_index = msgStr.substr(0, header_size+1).find("max-age");
    if (max_age_index != -1) {
      int i = max_age_index + 8;
      while (isdigit(msgStr[i])) {
        i++;
      }
      max_age = msgStr.substr(max_age_index + 8, i - max_age_index - 8);
    } else {
      max_age = "";
    }
    if (msgStr.substr(0, header_size+1).find("private") != -1 || msgStr.substr(0, header_size+1).find("no-store") != -1) {
      save_in_cache = false;
    } else {
      save_in_cache = true;
    }
    content_length = general_parse(msgStr.substr(0, header_size + 1),
                                   "Content-Length", 16, "\r\n");
    std::cout << "-------看看header----------" << std::endl;
    std::cout << msgStr.substr(0, header_size) << std::endl;
    std::cout << "----------------------------\r\n";
    if (content_length != "") {
      content_size = std::stoul(content_length);
      this->msg.resize(content_size + header_size);
    } else {
      content_size = 0;
    }

    no_cache = (msgStr.substr(0, header_size+1).find("no-cache") != -1 || msgStr.substr(0, header_size+1).find("must-revalidate") != -1);
    date = general_parse(msgStr.substr(0, header_size+1), "Date", 6, "\r\n");
    expire_time = general_parse(msgStr.substr(0, header_size+1), "Expires", 9, "\r\n");
    etag = general_parse(msgStr.substr(0, header_size+1), "ETag", 6, "\r\n");
    last_modify = general_parse(msgStr.substr(0, header_size+1), "Last-Modified", 15, "\r\n");
    line = general_parse(msgStr, "", 0, "\r\n");
    if (expire_time == "" && max_age == "" && last_modify != "" && date != "") {
      MyTime t1(last_modify);
      MyTime t2(date);
      max_age = std::to_string((t2.now - t1.now) / 10);
    }
  }

 private:
  std::string general_parse(const std::string& msgStr, const std::string& aim,
                            int length, const std::string& end) {
    int start_index = msgStr.find(aim);
    if (start_index != -1) {
      int end_index = msgStr.find(end, start_index + 1);
      return msgStr.substr(start_index + length,
                           end_index - start_index - length);
    } else {
      return "";
    }
  }

 public:
  char* getMsg() { return msg.data(); }
  size_t getMsgSize() { return msg.size(); }
  bool isChunked() { return chunked; }
  bool ifSaveInCache() { return save_in_cache; }
  bool ifNoCache() { return no_cache; }
  std::string& getEtag() { return etag; }
  const std::string& getLine() const { return line; }
  bool hasMaxAge() { return max_age != ""; }
  size_t getMaxAge() { return std::stoul(max_age); }
  bool hasDate() { return date != ""; }
  MyTime getDate() {
    MyTime t(date);
    return t;
  }
  bool hasExpireTime() { return expire_time != ""; }
  MyTime getExpireTime() {
    MyTime t(expire_time);
    return t;
  }
  bool hasLastModify() { return last_modify != ""; }
  MyTime getLastModify() {
    MyTime t(last_modify);
    return t;
  }
  bool isFresh(const MyTime& curr) {
    if (hasMaxAge()) {
      return curr.getDiff(this->getDate()) < this->getMaxAge();
    } else if (hasExpireTime()) {
      return curr.gmttime < this->getExpireTime().now;
    }
    return false;
  }
};
#endif