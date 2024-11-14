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
#include "logging_utility.hpp"


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

    std::string debug_message;
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
      
      if (0 == redis_data_store_obj.set_kv(command[1], command[2], expiry_time_ms)) {
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
      auto result = redis_data_store_obj.get_kv(command[1]);
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

        DEBUG_LOG("in config get ");
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
        // if (command.size() < 2) {
        //   reply.push_back("-few arguments provided for KEY command.");
        //   data_type = "error";
        //   return RespParser::serialize(reply, data_type);
        //     // throw std::runtime_error("few arguments provided for KEY command.");
        // }
        DEBUG_LOG("command[0]=" + command[0] + ", command[1]=" + command[1]);
        redis_data_store_obj.display_all_key_value_pairs();
        if (0 != redis_data_store_obj.get_keys_with_pattern(reply, command[1])) {
          throw std::runtime_error("error occurred while fetching keys");
        }
        data_type = "array";
        std::stringstream ss;  
        for(auto& e : reply) 
          ss << e << ",| ";
        DEBUG_LOG(ss.str());
        response = RespParser::serialize(reply, data_type);
    }
    // command INFO, INFO <section>
    else if (compareCaseInsensitive("INFO", command[0])) {
      std::string required_section("all");
      if (command.size() >= 2)
        required_section = command[1];
      get_info(reply, required_section);
      std::stringstream ss;  
      for(auto& e : reply) 
        ss << e << ",| ";
      DEBUG_LOG(ss.str());
      data_type = "bulk_string";
      response = RespParser::serialize(reply, data_type);
    }
    // Invalid command
    else {
      reply.push_back("-err invalid command : " + command[0]);
      data_type = "error";
      response = RespParser::serialize(reply, data_type);
    }
    return response;
  }

private:

  int get_info(std::vector<std::string>& reply, const std::string& section) {
    const std::vector<std::string> supported_sections = {"Replication"};
    if (compareCaseInsensitive(section, "all")) {
      for (auto& section : supported_sections)
          get_info(reply, section);
    }
    else if (compareCaseInsensitive(section, "Replication"))
      reply.push_back("role" + get_config_kv("role"));
    return 0;
  }

  bool compareCaseInsensitive(const std::string& str1, const std::string& str2) {
    std::string str1_lower = str1;
    std::string str2_lower = str2;

    std::transform(str1_lower.begin(), str1_lower.end(), str1_lower.begin(), ::tolower);
    std::transform(str2_lower.begin(), str2_lower.end(), str2_lower.begin(), ::tolower);

    return str1_lower.compare(str2_lower) == 0;
  }

  static std::map<std::string, std::string> configStore;
  static std::mutex configStoreMutex;
  static RedisDataStore redis_data_store_obj;
};

std::map<std::string, std::string> RedisCommandCenter::configStore;
std::mutex RedisCommandCenter::configStoreMutex;
RedisDataStore RedisCommandCenter::redis_data_store_obj;

#endif  // REDISCOMMANDCENTER_HPP
