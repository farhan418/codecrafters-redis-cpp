#ifndef REDISCOMMANDCENTER_HPP
#define REDISCOMMANDCENTER_HPP

#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <ctime>
#include <cstdint>
#include "RespParser.hpp"
#include "RedisDataStore.hpp"
#include "RdbFileReader.hpp"


#ifndef DEBUG_LOG
#define DEBUG_LOG(msg)
auto now = std::chrono::system_clock::now();\
std::time_t now_time = std::chrono::system_clock::to_time_t(now);\
std::cerr << "[" << now_time << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
#endif


class RedisCommandCenter {
public:
  RedisCommandCenter() {};

  static std::optional<std::string> get_config_kv(const std::string& key) {
    std::lock_guard<std::mutex> guard(configStoreMutex);
    if (configStore.count(key) == 0) {
        return std::nullopt;
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

  static int read_rdb_file() {
    RdbFileReader rdb_file_reader;
    std::string db_file_path;
    auto result = get_config_kv("dir");
    if (result.has_value())
      db_file_path = *result;
    result = get_config_kv("dbfilename");
    if(result)
      db_file_path += "/" + *result;
    return rdb_file_reader.readFile(db_file_path);
  }
  

  std::string process(const std::vector<std::string>& command) {
    std::string response;
    std::string data_type;
    std::vector<std::string> reply;

    std::string debug_message = "\nin process(...), command : ";
    for (auto& c : command)
        debug_message += c + "|, ";

    DEBUG_LOG(debug_message);

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
      auto result = redis_data_store.get_kv(command[1]);
      if (result.has_value()) {
        reply.push_back(*result);
        data_type = "bulk_string";
        response = RespParser::serialize(reply, data_type);
      }
      else 
        response = "$-1\r\n";
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

        DEBUG_LOG("\nin config get ");
        reply.push_back(command[2]);
        auto result = get_config_kv(command[2]);
        if (result.has_value()) {
          data_type = "array";
          reply.push_back(*result);
          response = RespParser::serialize(reply, data_type);
        }
        else 
          response = "$-1\r\n";
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
