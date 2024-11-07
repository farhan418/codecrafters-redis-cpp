#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <regex>

int handle_client(int, const struct sockaddr_in&);
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

  // void parse(const std::string& respStr) {
  //   resetParser();
  //   this->tokens = split(respStr);
  //   isParsed = true;
  // }

  std::string parse_simple_string(const std::string& simple_string) {
    return simple_string.substr(1, simple_string.length()-1);
  }

  std::string parse_bulk_string(const std::string& bulk_string) {
    // auto index = bulk_string.find("\r\n", 1);
    if (isParsedAllTokens()) {
      throw std::runtime_error("error extracting bulk string: " + bulk_string);
    }
    // long long length = std::stol(bulk_string.substr(1,index-1));
    // index += 2;
    return parseNextToken("");
  }

  std::vector<std::string> parse_array(const std::string& array_string) {
    long long length = std::stol(array_string.substr(1, array_string.length()-1));
    // index += 2;
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

    switch(respToken.at(0)) {
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
      throw std::runtime_error("invalid respToken");
      break;
    }
    return command;
  }

  RespParser() : isParsed(false), lastTokenIndex(0) {
    tokens.clear();
  }

  // void resetParser() {
    // isParsed = false;
    // lastTokenIndex = 0;
    // tokens.clear();
  // }

  void resetParser(const std::string& respStr) {
    // resetParser();
    isParsed = false;
    lastTokenIndex = 0;
    tokens.clear();
    tokens = split(respStr);
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

class RedisCommandCenter {
private:
public:
  RedisCommandCenter(){}

  std::string process(const std::vector<std::string>& command) {
    std::string response;
    std::vector<std::string> reply;
    if ("PING" == command[0]) {
      reply.push_back("PONG");
      response = RespParser::serialize(reply, "simple_string");
    }
    else if ("ECHO" == command[0]) {
      if (command.size() <2) {
        throw std::runtime_error("few arguments provided for ECHO command.");
      }
      reply.push_back(command[1]);
      response = RespParser::serialize(reply, "bulk_string");
    }
    else {
      reply.push_back("-err invalid command : " + command[0]);
      response = RespParser::serialize(reply, "error");
    }
    return response;
  }
};


int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  // std::cout << "Logs from your program will appear here!\n";
  // Uncomment this block to pass the first stage
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  
  while(true) {
    memset(&client_addr, 0, sizeof(client_addr));
    std::cout << "Waiting for a client to connect...\n";

    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if (client_fd == -1) {
      std::cerr << "Failed to accept client connection\n";
      return 1;
    }
    std::cout << "Client connected\n";
     
    std::thread t([client_fd, client_addr](){
      try {
        handle_client(client_fd, client_addr);
      }
      catch(const std::exception& e) {
        std::cerr << "\nException occurred in thread: " << e.what() << std::endl;
      }
      catch(...) {
        std::cerr << "\nUnknown exception occurred in thread.\n";
      }
    });
    t.detach();
  }
  close(server_fd);
  return 0;
}

int handle_client(int client_fd, const struct sockaddr_in& client_addr) {
  int n = 0;
  char buffer[1024];
  RespParser resp_parser;
  RedisCommandCenter rcc;

  while(1) {
    memset(buffer, 0, sizeof(buffer));  // bzero is also deprecated POSIX function
    n = read(client_fd, buffer, sizeof(buffer));
    if (n < 0) {
      std::cerr << "Error reading from socket.\n";
      return -1;
    }
    resp_parser.resetParser(buffer);
    while(!resp_parser.isParsedAllTokens()) {
      std::cerr << " in loop";
      std::vector<std::string> command = resp_parser.deserialize(resp_parser.parseNextToken(""));
      std::string response_str = rcc.process(command);

      memset(buffer, 0, sizeof(buffer));
      memcpy(buffer, response_str.c_str(), response_str.length());
      n = write(client_fd, buffer, response_str.length());
      std::cerr << "\nSent " << n <<" bytes : " << buffer << std::endl;
      if (n < 0) {
        std::cerr << "Failed to write message to socket.\n";
        return -1;
      }
      if (std::string(buffer).find("END") != std::string::npos)
        break;
    } 
  }
  close(client_fd);
  return 0;
}
