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
  static std::map<std::string, std::string> configStore;
  static std::mutex configStoreMutex;
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

  static std::string get_config_kv(const std::string& key) {
    std::lock_guard<std::mutex> guard(configStoreMutex);
    if (configStore.count(key) == 0) {
        return "-1";
    }
    return configStore[key];
  }

  static int set_config_kv(const std::string& key, const std::string& value) {
    try {
        std::lock_guard<std::mutex> guard(configStoreMutex);
        configStore[key] = value;
        return 0;
    }
    catch (std::exception& e) {
        return -1;
    }
    catch(...) {
        return -1;
    }
  }

  std::string process(const std::vector<std::string>& command) {
    std::string response;
    std::string data_type;
    std::vector<std::string> reply;
    std::cerr << "\nin process(...), command : ";
    for (auto& c : command)
        std::cerr << c << "|, ";

    if (compareCaseInsensitive("PING", command[0])) {
      reply.push_back("PONG");
      data_type = "simple_string";
      response = RespParser::serialize(reply, data_type);
    }
    else if (compareCaseInsensitive("ECHO", command[0])) {
      if (command.size() <2) {
        throw std::runtime_error("few arguments provided for ECHO command.");
      }
      reply.push_back(command[1]);
      data_type = "bulk_string";
      response = RespParser::serialize(reply, data_type);
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
        throw std::runtime_error("Error while storing key value pair.");
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
      response = RespParser::serialize(reply, data_type);    
    }
    else if (compareCaseInsensitive("GET", command[0])) {
      if (command.size() < 2) {
        throw std::runtime_error("few arguments provided for GET command.");
      }
      data_type = "bulk_string";
      reply.push_back(get_kv(command[1]));
      if (reply[0] == "-1")
        response = "$-1\r\n";
      else
        response = RespParser::serialize(reply, data_type);
    }
    else if (command.size() >= 2 && 
            compareCaseInsensitive("CONFIG", command[0]) &&
            compareCaseInsensitive("GET", command[1])) {
        if (command.size() < 3) {
            throw std::runtime_error("few arguments provided for CONFIG GET command.");
        }
        std::cerr << "\nin config get ";
        reply.push_back(command[2]);
        reply.push_back(get_config_kv(command[2]));
        data_type = "array";
        response = RespParser::serialize(reply, data_type);
    }
    else if (command.size() == 2 && 
            compareCaseInsensitive("KEYS", command[0])) {
        if (command.size() < 3) {
            throw std::runtime_error("few arguments provided for KEY command.");
        }
        std::string pattern_text = command[1];
        {
            size_t pos = 0;
            while (pos = pattern_text.find("*") != std::string::npos) {
                pattern_text.replace(pos, std::to_string("*"), std::to_string(".*"));
                pos += 2;
            }
            std::regex pattern(pattern_text);
            std::lock_guard<std::mutex> guard(keyStoreMutex);
            for (auto& pair : keyStore) {
                if(std::regex_match(pair.first, pattern)) {
                    reply.push_back(pair.first);
                }
            }
        }
        data_type = "array";
        response = RespParser::serialize(reply, data_type);
    }
    else {
      reply.push_back("-err invalid command : " + command[0]);
      data_type = "error";
    }
    return response;
  }

};

std::map<std::string, std::string> RedisCommandCenter::keyStore;
std::mutex RedisCommandCenter::keyStoreMutex;
std::map<std::string, std::string> RedisCommandCenter::configStore;
std::mutex RedisCommandCenter::configStoreMutex;

#endif  // REDISCOMMANDCENTER_HPP