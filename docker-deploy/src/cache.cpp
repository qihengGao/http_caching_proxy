#include "cache.hpp"
pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

size_t Cache::getCapacity() { return this->capacity; }

bool Cache::isFull() {
  pthread_rwlock_rdlock(&lock);
  bool result = this->data.size() == this->getCapacity();
  pthread_rwlock_unlock(&lock);
  return result;
}

bool Cache::has(const std::string& url) {
  pthread_rwlock_rdlock(&lock);
  bool res = (data.find(url) != data.end());
  pthread_rwlock_unlock(&lock);
  return res;
}

Response& Cache::get(const std::string& url) {
  pthread_rwlock_rdlock(&lock);
  Response& resp = this->data[url];
  pthread_rwlock_unlock(&lock);
  return resp;
}

void Cache::add(const std::string& url, const Response& response,
                const MyTime& addTime) {
  if (isFull()) {
    clearStales(addTime);
  }
  pthread_rwlock_wrlock(&lock);
  this->data[url] = response;
  pthread_rwlock_unlock(&lock);
}

void Cache::clearStales(const MyTime& time) {
  pthread_rwlock_wrlock(&lock);
  auto it = this->data.begin();
  while (it != this->data.end()) {
    if (!it->second.isFresh(time)) {
      it = this->data.erase(it);
    } else {
      ++it;
    }
  }
  pthread_rwlock_unlock(&lock);
}