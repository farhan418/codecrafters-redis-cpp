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

  static int set_master_info() {
    RedisCommandCenter::set_config_kv("role", "master");
    RedisCommandCenter::set_config_kv("master_replid", "8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb");
    RedisCommandCenter::set_config_kv("master_repl_offset", "0");
    return 0;
  }

  static int set_slave_info(const std::string replicaof) {
    RedisCommandCenter::set_config_kv("role", "slave");
    RedisCommandCenter::set_config_kv("replicaof", replicaof);
    return 0;
  }

  std::string process(const std::vector<std::string>& command) {
    std::stringstream ss;
    for (auto& c : command)
        ss << c << "|, ";
    DEBUG_LOG(ss.str());

    // command PING
    if (compareCaseInsensitive("PING", command[0])) {
      return _commandPING();
    }
    // command ECHO  
    else if (compareCaseInsensitive("ECHO", command[0])) {
      return _commandECHO(command);
    }
    // command SET, SET key value PX ms
    else if (compareCaseInsensitive("SET", command[0])) {
      return _commandSET(command);   
    }
    // command GET key
    else if (compareCaseInsensitive("GET", command[0])) {
      return _commandGET();
    }
    // command CONFIG GET
    else if (command.size() >= 2 && 
            compareCaseInsensitive("CONFIG", command[0]) &&
            compareCaseInsensitive("GET", command[1])) {
        return _commandCONFIG_GET(command);
    }
    // command KEYS
    else if (command.size() == 2 && 
            compareCaseInsensitive("KEYS", command[0])) {
      return _commandKEYS(command);
    }
    // command INFO, INFO <section>
    else if (compareCaseInsensitive("INFO", command[0])) {
      return _commandINFO(command);
    }
    // Invalid command
    else {
      std::vector<std::string> reply;
      std::string data_type("error");
      reply.push_back("err invalid command : " + command[0]);
      return RespParser::serialize(reply, data_type);
    }
  }

private:

  std::string _commandPING() {
    std::vector<std::string> reply;
    std::string dataType = "simple_string";
    reply.push_back("PONG");
    return RespParser::serialize(reply, data_type);
  }

  std::string _commandECHO(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType = "bulk_string";
    if (command.size() <2) {
      reply.push_back("few arguments provided for ECHO command.");
      data_type = "error";
    }
    else {
      reply.push_back(command[1]);
    }
    return RespParser::serialize(reply, data_type);
  }

  std::string _commandSET(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;

    if (command.size() < 3) {
        reply.push_back("few arguments provided for SET command.");
        data_type = "error";
    }
    else {
      uint64_t expiry_time_ms = UINT64_MAX;
      if (5 == command.size() && compareCaseInsensitive("PX", command[3])) {
        expiry_time_ms = std::stol(command[4]);
      }
      
      if (0 == redis_data_store_obj.set_kv(command[1], command[2], expiry_time_ms)) {
        reply.push_back("OK");
        data_type = "simple_string";
      }
      else {
        reply.push_back("Error while storing key value pair.");
        data_type = "error";
      }
    }
    return RespParser::serialize(reply, data_type);
  }

  std::string _commandGET(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;
    std::string response;
    if (command.size() < 2) {
      reply.push_back("-few arguments provided for GET command.");
      data_type = "error";
      response = RespParser::serialize(reply, data_type);
    }
    auto result = redis_data_store_obj.get_kv(command[1]);
    if (result.has_value()) {
      reply.push_back(*result);
      data_type = "bulk_string";
      response = RespParser::serialize(reply, data_type);
    }
    else 
      response = "$-1\r\n";
    return response;
  }

  std::string _commandCONFIG_GET(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;
    std::string response;

    if (command.size() < 3) {
      reply.push_back("few arguments provided for CONFIG GET command.");
      data_type = "error";
      return RespParser::serialize(reply, data_type);
    }
    DEBUG_LOG("in config get ");
    auto result = get_config_kv(command[2]);
    if (result.has_value()) {
      data_type = "array";
      reply.push_back(command[2]);
      reply.push_back(*result);
      response = RespParser::serialize(reply, data_type);
    }
    else 
      response = "$-1\r\n";
    return response;
  }

  std::string _commandKEYS(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;
    // if (command.size() < 2) {
    //   reply.push_back("-few arguments provided for KEY command.");
    //   data_type = "error";
    //   return RespParser::serialize(reply, data_type);
    //     // throw std::runtime_error("few arguments provided for KEY command.");
    // }
    DEBUG_LOG("command[0]=" + command[0] + ", command[1]=\"" + command[1] + "\"");
    redis_data_store_obj.display_all_key_value_pairs();
    if (0 != redis_data_store_obj.get_keys_with_pattern(reply, command[1])) {
      throw std::runtime_error("error occurred while fetching keys");
    }
    data_type = "array";
    std::stringstream ss;  
    for(auto& e : reply) 
      ss << e << ",| ";
    DEBUG_LOG(ss.str());
    return RespParser::serialize(reply, data_type);
  }

  std::string _commandINFO(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;
    std::string required_section("all");
      if (command.size() >= 2)
        required_section = command[1];

      if (0 != _getInfo(reply, required_section)) {
        DEBUG_LOG("error while getting info");
        return "error while getting info";
      }
      std::stringstream ss;  
      for(auto& e : reply) 
        ss << e << ",| ";
      DEBUG_LOG(ss.str());
      data_type = "bulk_string";
      return RespParser::serialize(reply, data_type);
  }

  int _getInfo(std::vector<std::string>& reply, const std::string& section) {
    static const std::vector<std::string> supported_sections = {"Replication"};
    if (compareCaseInsensitive(section, "all")) {
      for (auto& section : supported_sections)
          _getInfo(reply, section);
    }
    else if (compareCaseInsensitive(section, "Replication")) {
      std::string str;
      for (auto& key : std::vector<std::string>{"role", "master_replid", "master_repl_offset"})
        if (auto result = get_config_kv(key)) {
          str += "\r\n" + key + ":" + *result;
        }
      reply.push_back(str.substr(2, str.length()-2));
    }
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
