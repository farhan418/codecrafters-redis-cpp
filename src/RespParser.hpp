#ifndef RESPPARSER_HPP
#define RESPPARSER_HPP

#include <vector>
#include <iostream>
#include <regex>
#include <string>
#include "logging_utility.hpp"

class RespParser {
private:
  bool isParsed;
  long long lastTokenIndex;
  std::vector<std::string> tokens;

  std::vector<std::string> split(const std::string& respStr) const {
    std::vector<std::string> result;
    std::regex re("\r\n");
    std::sregex_token_iterator first(respStr.begin(), respStr.end(), re, -1);
    std::sregex_token_iterator last;
    result.assign(first, last);
    return result;
  }

  std::string parse_simple_string(const std::string& simple_string) {
    return simple_string.substr(1, simple_string.length()-1);
  }

  std::string parse_bulk_string(const std::string& bulk_string) {
    if (isParsedAllTokens()) {
      throw std::runtime_error("error extracting bulk string: " + bulk_string);
    }
    return parseNextToken("");
  }

  std::vector<std::string> parse_array(const std::string& array_string) {
    long long length = std::stol(array_string.substr(1, array_string.length()-1));
    std::vector<std::string> command;
    for (long long i = 0; i < length; i++) {
      std::string next_token = parseNextToken("");
      auto temp = deserialize(next_token);
      command.push_back(temp[0]);
    }
    return command;
  }

public:
  std::vector<std::string> deserialize(const std::string& respToken) {
    std::vector<std::string> command;
    DEBUG_LOG("\'" + respToken + "\' in deserialize, respToken[0] = " + respToken[0]);
    switch(respToken[0]) {
      case '+':
      command.push_back(parse_simple_string(respToken));
      break;

      case '-':
      command.push_back(respToken);
      break;

      case '$':
      command.push_back(parse_bulk_string(respToken));
      break;

      case '*':
      command = parse_array(respToken);
      break;

      default:
      throw std::runtime_error("invalid respToken \'" + respToken + "\'");
      break;
    }
    return command;
  }

  RespParser() : isParsed(false), lastTokenIndex(0) {
    tokens.clear();
  }

  void resetParser() {
    isParsed = false;
    lastTokenIndex = 0;
    tokens.clear();
  }

  void resetParser(const std::string& respStr) {
    resetParser();
    tokens = split(respStr);
    isParsed = true;
    // for(auto& element : tokens) {
    //   std::cerr << element << "|, ";
    // }
  }

  std::string parseNextToken(const std::string& respStr) {
    if (!isParsed) {
      resetParser(respStr);
    }
    if (lastTokenIndex == tokens.size()) {
      return "";  // empty string is false
    }
    return tokens[lastTokenIndex++];
  }

  bool isParsedAllTokens() const {
    return lastTokenIndex == tokens.size();
  }

  static std::string serialize(const std::vector<std::string>& vec, const std::string& data_type) {
    const std::string DELIMETER = "\r\n";
    std::string result;

    if (data_type == "simple_string")
      result = "+" + vec[0] + DELIMETER;
    else if (data_type == "bulk_string")
      result = "$" + std::to_string(vec[0].length()) + DELIMETER + vec[0] + DELIMETER;
    else if (data_type == "array") {
      result = "*" + std::to_string(vec.size()) + DELIMETER;
      for(auto& element : vec) {
        result += "$" + std::to_string(element.length()) + DELIMETER + element + DELIMETER;
      }
    }
    else if (data_type == "error") {
      result = "-" + vec[0] + DELIMETER;
    }
    else {
      throw std::runtime_error("invalid data type, cannot serialize");
    }
    return result;
  }
};
#endif  // RESPPARSER_HPP