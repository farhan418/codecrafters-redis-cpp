#ifndef REDISCOMMANDCENTER_HPP
#define REDISCOMMANDCENTER_HPP

#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdint>
#include "RespParser.hpp"
#include "RedisDataStore.hpp"


class RedisCommandCenter {
public:
  RedisCommandCenter() {};

  static std::string get_config_kv(const std::string& key) {
    std::lock_guard<std::mutex> guard(configStoreMutex);
    if (configStore.count(key) == 0) {
        return "-1";
    }
    return configStore[key];
  }

  static int set_config_kv(const std::string& key, const std::string& value) {
    int status = 0;
    try {
        std::lock_guard<std::mutex> guard(configStoreMutex);
        configStore[key] = value;
    }
    catch(...) {
        status = -1;
    }
    return status;
  }

  std::string process(const std::vector<std::string>& command) {
    std::string response;
    std::string data_type;
    std::vector<std::string> reply;

    std::cerr << "\nin process(...), command : ";
    for (auto& c : command)
        std::cerr << c << "|, ";

    // command PING
    if (compareCaseInsensitive("PING", command[0])) {
      reply.push_back("PONG");
      data_type = "simple_string";
      response = RespParser::serialize(reply, data_type);
    }
    // command ECHO
    else if (compareCaseInsensitive("ECHO", command[0])) {
      if (command.size() <2) {
        reply.push_back("-few arguments provided for ECHO command.");
        data_type = "error";
      }
      else {
        reply.push_back(command[1]);
        data_type = "bulk_string";
      }
      response = RespParser::serialize(reply, data_type);
    }
    // command SET, SET key value PX ms
    else if (compareCaseInsensitive("SET", command[0])) {
      if (command.size() < 3) {
        reply.push_back("-few arguments provided for SET command.");
        data_type = "error";
        return RespParser::serialize(reply, data_type);;
      }

      uint64_t expiry_time_ms = UINT64_MAX;
      if (5 == command.size() && compareCaseInsensitive("PX", command[3])) {
        expiry_time_ms = std::stol(command[4]);
      }
      
      if (0 == redis_data_store.set_kv(command[1], command[2], expiry_time_ms)) {
        reply.push_back("OK");
        data_type = "simple_string";
      }
      else {
        reply.push_back("-Error while storing key value pair.");
        data_type = "error";
      }
      response = RespParser::serialize(reply, data_type);    
    }
    // command GET key
    else if (compareCaseInsensitive("GET", command[0])) {
      if (command.size() < 2) {
        reply.push_back("-few arguments provided for GET command.");
        data_type = "error";
        return RespParser::serialize(reply, data_type);
      }
      data_type = "bulk_string";
      reply.push_back(redis_data_store.get_kv(command[1]));
      if (reply[0] == std::nullopt)
        response = "$-1\r\n";
      else
        response = RespParser::serialize(reply, data_type);
    }
    // command CONFIG GET
    else if (command.size() >= 2 && 
            compareCaseInsensitive("CONFIG", command[0]) &&
            compareCaseInsensitive("GET", command[1])) {
        if (command.size() < 3) {
          reply.push_back("-few arguments provided for CONFIG GET command.");
          data_type = "error";
          return RespParser::serialize(reply, data_type);
        }
        std::cerr << "\nin config get ";
        reply.push_back(command[2]);
        reply.push_back(get_config_kv(command[2]));
        data_type = "array";
        response = RespParser::serialize(reply, data_type);
    }
    // command KEYS
    else if (command.size() == 2 && 
            compareCaseInsensitive("KEYS", command[0])) {
        if (command.size() < 3) {
          reply.push_back("-few arguments provided for KEY command.");
          data_type = "error";
          return RespParser::serialize(reply, data_type);
            // throw std::runtime_error("few arguments provided for KEY command.");
        }
        redis_data_store.get_keys_with_pattern(reply, command[1]);
        data_type = "array";
        response = RespParser::serialize(reply, data_type);
    }
    else {
      reply.push_back("-err invalid command : " + command[0]);
      data_type = "error";
      response = RespParser::serialize(reply, data_type);
    }
    return response;
  }

private:
  bool compareCaseInsensitive(const std::string& str1, const std::string& str2) {
    std::string str1_lower = str1;
    std::string str2_lower = str2;

    std::transform(str1_lower.begin(), str1_lower.end(), str1_lower.begin(), ::tolower);
    std::transform(str2_lower.begin(), str2_lower.end(), str2_lower.begin(), ::tolower);

    return str1_lower.compare(str2_lower) == 0;
  }

  static std::map<std::string, std::string> configStore;
  static std::mutex configStoreMutex;
  static RedisDataStore redis_data_store;
};

std::map<std::string, std::string> RedisCommandCenter::configStore;
std::mutex RedisCommandCenter::configStoreMutex;
RedisDataStore RedisCommandCenter::redis_data_store;

#endif  // REDISCOMMANDCENTER_HPP
