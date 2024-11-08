#ifndef REDISCOMMANDCENTER_HPP
#define REDISCOMMANDCENTER_HPP

#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include "RespParser.hpp"


class RedisCommandCenter {
private:
  static std::map<std::string, std::string> keyStore;
  static std::mutex keyStoreMutex;
public:
  RedisCommandCenter(){}

  bool compareCaseInsensitive(const std::string& str1, const std::string& str2) {
    std::string str1_lower = str1;
    std::string str2_lower = str2;

    std::transform(str1_lower.begin(), str1_lower.end(), str1_lower.begin(), ::tolower);
    std::transform(str2_lower.begin(), str2_lower.end(), str2_lower.begin(), ::tolower);

    return str1_lower.compare(str2_lower) == 0;
  }

  static std::string get_kv(const std::string& key) {
    std::lock_guard<std::mutex> guard(keyStoreMutex);
    if (keyStore.count(key) == 0) {
        return "-1";
    }
    return keyStore[key];
  }

  static int set_kv(const std::string& key, const std::string& value) {
    try {
        std::lock_guard<std::mutex> guard(keyStoreMutex);
        keyStore[key] = value;
        return 0;
    }
    catch (std::exception& e) {
        return -1;
    }
    catch(...) {
        return -1;
    }
  }

  static int delete_kv(const std::string& key) {
    std::lock_guard<std::mutex> guard(keyStoreMutex);
    if (keyStore.count(key) == 0) {
        return -1;
    }
    keyStore.erase(key);
    return 0;
  }

  std::string process(const std::vector<std::string>& command) {
    std::string data_type;
    std::vector<std::string> reply;
    if (compareCaseInsensitive("PING", command[0])) {
      reply.push_back("PONG");
      data_type = "simple_string";
    }
    else if (compareCaseInsensitive("ECHO", command[0])) {
      if (command.size() <2) {
        throw std::runtime_error("few arguments provided for ECHO command.");
      }
      reply.push_back(command[1]);
      data_type = "bulk_string";
    }
    else if (compareCaseInsensitive("SET", command[0])) {
      if (command.size() < 3) {
        throw std::runtime_error("few arguments provided for SET command.");
      }
      
      if (0 == set_kv(command[1], command[2])) {
        reply.push_back("OK");
        data_type = "simple_string";
      }
      else {
        throw std::runtime_error("Error while storing key value pair.")
      }
      
      if (command.size() == 5) {
        if (compareCaseInsensitive("PX", command[3])) {
          std::thread t([&command](){
            std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(command[4])));
            delete_kv(command[1]);
          });
          t.detach();
        }
      }      
    }
    else if (compareCaseInsensitive("GET", command[0])) {
      if (command.size() < 2) {
        throw std::runtime_error("few arguments provided for GET command.");
      }
      data_type = "bulk_string";
      reply.push_back(get_kv([command[1]));
    }
    else {
      reply.push_back("-err invalid command : " + command[0]);
      data_type = "error";
    }
    return RespParser::serialize(reply, data_type);
  }

};

std::map<std::string, std::string> RedisCommandCenter::keyStore;
std::mutex RedisCommandCenter::keyStoreMutex;

#endif  // REDISCOMMANDCENTER_HPP