#ifndef REDISCOMMANDCENTER_HPP
#define REDISCOMMANDCENTER_HPP

#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include "RespParser.hpp"


class RedisCommandCenter {
private:
  std::map<std::string, std::string> keyStore;
public:
  RedisCommandCenter(){}

  bool compareCaseInsensitive(const std::string& str1, const std::string& str2) {
    std::string str1_lower = str1;
    std::string str2_lower = str2;

    std::transform(str1_lower.begin(), str1_lower.end(), str1_lower.begin(), ::tolower);
    std::transform(str2_lower.begin(), str2_lower.end(), str2_lower.begin(), ::tolower);

    return str1_lower.compare(str2_lower) == 0;
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
      
      keyStore[command[1]] = command[2];
      reply.push_back("OK");
      data_type = "simple_string";

      if (command.size() == 5) {
        if (compareCaseInsensitive("PX", command[3])) {
          thread t([&command](){
            std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(command[4])));
            keyStore.erase(command[1]);
          });
        }
      }      
    }
    else if (compareCaseInsensitive("GET", command[0])) {
      if (command.size() < 2) {
        throw std::runtime_error("few arguments provided for GET command.");
      }
      data_type = "bulk_string";
      if (keyStore.count(command[1]) == 0)
        reply.push_back("-1");
      else
        reply.push_back(keyStore[command[1]]);
    }
    else {
      reply.push_back("-err invalid command : " + command[0]);
      data_type = "error";
    }
    return RespParser::serialize(reply, data_type);
  }

};
#endif  // REDISCOMMANDCENTER_HPP