#ifndef __CACHE_HPP__
#define __CACHE_HPP__
#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unordered_map>

#include "response.hpp"

class Cache {
 private:
  const size_t capacity;
  std::unordered_map<std::string, Response> data;

 public:
  Cache() : capacity(100000) {}
  Cache(int _cap) : capacity(_cap) {}
  bool has(const std::string& url);
  void add(const std::string& url, const Response& response,
           const MyTime& addTime);
  Response& get(const std::string& url);
  bool isFull();
  size_t getCapacity();
  void clearStales(const MyTime& time);
};

#endif