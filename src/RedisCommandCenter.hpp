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

  static int set_slave_info(const std::string replicaof, const std::string listeningPortNumber, const std::string capa) {
    RedisCommandCenter::set_config_kv("role", "slave");
    RedisCommandCenter::set_config_kv("replicaof", replicaof);
    RedisCommandCenter::set_config_kv("listening-port", listeningPortNumber);
    RedisCommandCenter::set_config_kv("capa", capa);
    return 0;
  }

  std::string process(const std::vector<std::string>& command) {
    std::stringstream ss;
    for (auto& c : command)
        ss << c << "|, ";
    DEBUG_LOG(ss.str());

    // command PING
    if (utility::compareCaseInsensitive("PING", command[0])) {
      return _commandPING();
    }
    // command ECHO  
    else if (utility::compareCaseInsensitive("ECHO", command[0])) {
      return _commandECHO(command);
    }
    // command SET, SET key value PX ms
    else if (utility::compareCaseInsensitive("SET", command[0])) {
      return _commandSET(command);   
    }
    // command GET key
    else if (utility::compareCaseInsensitive("GET", command[0])) {
      return _commandGET(command);
    }
    // command CONFIG GET
    else if (command.size() >= 2 && 
            utility::compareCaseInsensitive("CONFIG", command[0]) &&
            utility::compareCaseInsensitive("GET", command[1])) {
        return _commandCONFIG_GET(command);
    }
    // command KEYS
    else if (command.size() == 2 && 
            utility::compareCaseInsensitive("KEYS", command[0])) {
      return _commandKEYS(command);
    }
    // command INFO, INFO <section>
    else if (utility::compareCaseInsensitive("INFO", command[0])) {
      return _commandINFO(command);
    }
    // command REPLCONF listening-port <replicaListenerPort>, REPLCONF capa psync2
    else if (utility::compareCaseInsensitive("REPLCONF", command[0])) {
      return _commandREPLCONF(command);
    }
    // Invalid command
    else {
      std::vector<std::string> reply;
      std::string dataType("error");
      reply.push_back("err invalid command : " + command[0]);
      return RespParser::serialize(reply, dataType);
    }
  }

private:

  std::string _commandPING() {
    std::vector<std::string> reply;
    std::string dataType = "simple_string";
    reply.push_back("PONG");
    return RespParser::serialize(reply, dataType);
  }

  std::string _commandECHO(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType = "bulk_string";
    if (command.size() <2) {
      reply.push_back("few arguments provided for ECHO command.");
      dataType = "error";
    }
    else {
      reply.push_back(command[1]);
    }
    return RespParser::serialize(reply, dataType);
  }

  std::string _commandSET(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;

    if (command.size() < 3) {
        reply.push_back("few arguments provided for SET command.");
        dataType = "error";
    }
    else {
      uint64_t expiry_time_ms = UINT64_MAX;
      if (5 == command.size() && utility::compareCaseInsensitive("PX", command[3])) {
        expiry_time_ms = std::stol(command[4]);
      }
      
      if (0 == redis_data_store_obj.set_kv(command[1], command[2], expiry_time_ms)) {
        reply.push_back("OK");
        dataType = "simple_string";
      }
      else {
        reply.push_back("Error while storing key value pair.");
        dataType = "error";
      }
    }
    return RespParser::serialize(reply, dataType);
  }

  std::string _commandGET(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;
    std::string response;
    if (command.size() < 2) {
      reply.push_back("-few arguments provided for GET command.");
      dataType = "error";
      response = RespParser::serialize(reply, dataType);
    }
    auto result = redis_data_store_obj.get_kv(command[1]);
    if (result.has_value()) {
      reply.push_back(*result);
      dataType = "bulk_string";
      response = RespParser::serialize(reply, dataType);
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
      dataType = "error";
      return RespParser::serialize(reply, dataType);
    }
    DEBUG_LOG("in config get ");
    auto result = get_config_kv(command[2]);
    if (result.has_value()) {
      dataType = "array";
      reply.push_back(command[2]);
      reply.push_back(*result);
      response = RespParser::serialize(reply, dataType);
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
    //   dataType = "error";
    //   return RespParser::serialize(reply, dataType);
    //     // throw std::runtime_error("few arguments provided for KEY command.");
    // }
    DEBUG_LOG("command[0]=" + command[0] + ", command[1]=\"" + command[1] + "\"");
    redis_data_store_obj.display_all_key_value_pairs();
    if (0 != redis_data_store_obj.get_keys_with_pattern(reply, command[1])) {
      throw std::runtime_error("error occurred while fetching keys");
    }
    dataType = "array";
    std::stringstream ss;  
    for(auto& e : reply) 
      ss << e << ",| ";
    DEBUG_LOG(ss.str());
    return RespParser::serialize(reply, dataType);
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
    dataType = "bulk_string";
    return RespParser::serialize(reply, dataType);
  }
  
  std::string _commandREPLCONF(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType;
    
    if (command.size() < 3) {
      reply.push_back("few arguments provided for REPLCONF command.");
      dataType = "error";
      return RespParser::serialize(reply, dataType);
    }

    if (utility::compareCaseInsensitive("listening-port", command[1])) {
      // save port
      DEBUG_LOG("replica is listening at port - " + command[2]);
    }
    else if (utility::compareCaseInsensitive("capa", command[1])) {
      // save capabilities
      DEBUG_LOG("Capabilities : " + command[2]);
    }
    reply.push_back("OK");
    dataType = "simple_string";
    return RespParser::serialize(reply, dataType);
  }

  std::string _commandPSYNC(const std::vector<std::string>& command) {
    std::vector<std::string> reply;
    std::string dataType = "simple_string";
    if (command.size() < 3) {
      reply.push_back("few arguments provided for PSYNC command.");
      dataType = "error";
      return RespParser::serialize(reply, dataType);
    }
    std::string str = "FULLRESYNC ";
    auto masterReplid = get_config_kv("master_replid");
    if (masterReplid.has_value()) {
      str += (*masterReplid);
    }
    else {
      str += "8371b4fb1155b71f4a04d3e1b<random-replid>"
    }
    auto master_repl_offset = get_config_kv("master_replid");
    if (master_repl_offset.has_value()) {
      str += (*master_repl_offset);
    }
    return RespParser::serialize(reply, dataType);
  }

  int _getInfo(std::vector<std::string>& reply, const std::string& section) {
    static const std::vector<std::string> supported_sections = {"Replication"};
    if (utility::compareCaseInsensitive(section, "all")) {
      for (auto& section : supported_sections)
          _getInfo(reply, section);
    }
    else if (utility::compareCaseInsensitive(section, "Replication")) {
      std::string str;
      for (auto& key : std::vector<std::string>{"role", "master_replid", "master_repl_offset"})
        if (auto result = get_config_kv(key)) {
          str += "\r\n" + key + ":" + *result;
        }
      reply.push_back(str.substr(2, str.length()-2));
    }
    return 0;
  }

  static std::map<std::string, std::string> configStore;
  static std::mutex configStoreMutex;
  static RedisDataStore redis_data_store_obj;
};

std::map<std::string, std::string> RedisCommandCenter::configStore;
std::mutex RedisCommandCenter::configStoreMutex;
RedisDataStore RedisCommandCenter::redis_data_store_obj;

#endif  // REDISCOMMANDCENTER_HPP
