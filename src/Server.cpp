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
#include <chrono>
#include <ctime>

#include "RedisCommandCenter.hpp"
#include "RespParser.hpp"
// #include "RdbFileReader.hpp"
#include "logging_utility.hpp"
#include "argparse.hpp"


int handle_client(int, const struct sockaddr_in&);
int process_cmdline_args(int, char**, argparse::ArgumentParser&);

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  argparse::ArgumentParser arg_parser("Redis Server");
  process_cmdline_args(argc, argv, arg_parser);
  DEBUG_LOG("in main after return");
  uint16_t port_number = arg_parser.get<int>("--port");
  DEBUG_LOG("in main after getting port_number = " + std::to_string(port_number));

  if (auto dir = arg_parser.present("--dir")) {
    if (auto dbfilename = arg_parser.present("--dbfilename")) {
      RedisCommandCenter::set_config_kv("dir", *dir);
      RedisCommandCenter::set_config_kv("dbfilename", *dbfilename);
      RedisCommandCenter::read_rdb_file();
    }
    else {
      // continue without reading RDB file, graceful handling
    }
  }

  // std::stringstream ss1;
  // if (argc == 3 && ("--port" == std::string(argv[1]))) {
  //   ss1 << "port_Number = " << argv[2];
  //   DEBUG_LOG(ss1.str());
  //   port_number = std::stoi(argv[2]);
  // }
  // ss1.str("");
  // ss1 << "port_Number = " << port_number;
  // DEBUG_LOG(ss1.str());

  // if (argc == 5 && ("--dir" == std::string(argv[1])) && ("--dbfilename" == std::string(argv[3]))) { 
  //   RedisCommandCenter::set_config_kv("dir", argv[2]);
  //   RedisCommandCenter::set_config_kv("dbfilename", argv[4]);
  //   if (0 != RedisCommandCenter::read_rdb_file()) {
  //     DEBUG_LOG("Failed to read rdb file.");
  //   } 
  // }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   DEBUG_LOG("Failed to create server socket\n");
   return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    DEBUG_LOG("setsockopt failed\n");
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port_number);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    DEBUG_LOG("Failed to bind to port 6379\n");
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    DEBUG_LOG("listen failed\n");
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  
  while(true) { 
    memset(&client_addr, 0, sizeof(client_addr));
    DEBUG_LOG("Waiting for a client to connect...\n");

    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t*) &client_addr_len);
    if (client_fd == -1) {
      DEBUG_LOG("Failed to accept client connection\n");
      return 1;
    }
    DEBUG_LOG("Client connected\n");
     
    std::thread t([client_fd, client_addr](){
      try {
        handle_client(client_fd, client_addr);
      }
      catch(const std::exception& e) {
        DEBUG_LOG("Exception occurred in thread: " + std::string(e.what()) + "\n");
      }
      catch(...) {
        DEBUG_LOG("Unknown exception occurred in thread.\n");
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
  std::string debug_message;

  while(true) {
    memset(buffer, 0, sizeof(buffer));  // bzero is also deprecated POSIX function
    n = read(client_fd, buffer, sizeof(buffer));
    if (n < 0) {
      DEBUG_LOG("Error reading from socket.\n");
      return -1;
    }
    debug_message = "read " + std::to_string(n) + " bytes : ";
    for (auto& c : buffer) 
      debug_message += c;
    DEBUG_LOG(debug_message);
    resp_parser.resetParser(buffer);
    while(!resp_parser.isParsedAllTokens()) {
      // DEBUG_LOG(" in loop parsing command...");
      std::vector<std::string> command = resp_parser.deserialize(resp_parser.parseNextToken(""));
      // DEBUG_LOG("after in loop parsing command...");
      // std::string str;
      // for(auto& e : command)
      //   str += e + " |, ";
      // DEBUG_LOG(str);
      std::string response_str = rcc.process(command);
      DEBUG_LOG("response_str : " + response_str);

      memset(buffer, 0, sizeof(buffer));
      memcpy(buffer, response_str.c_str(), response_str.length());
      n = write(client_fd, buffer, response_str.length());
      debug_message = "\nSent " + std::to_string(n) + " bytes : ";
      for (auto& c : buffer)
        debug_message += c;
      DEBUG_LOG(debug_message);
      if (n < 0) {
        DEBUG_LOG("Failed to write message to socket.\n");
        return -1;
      }
      if (std::string(buffer).find("END") != std::string::npos)
        break;
    } 
  }

  close(client_fd);
  return 0;
}

int process_cmdline_args(int argc, char** argv, argparse::ArgumentParser& argument_parser) {
  std::string debug_message = "argc = " + std::to_string(argc) + ", argv = [";
  for (int i = 0; i < argc; i++) {
    debug_message += std::string(argv[i]) + "|, ";
  }
  DEBUG_LOG(debug_message);

  argument_parser.add_argument("--dir")
    .help("path to the directory containing Redis RDB file");

  argument_parser.add_argument("--dbfilename")
    .help("Redis RDB filename which is stored in --dir");

  argument_parser.add_argument("-p", "--port")
    .help("port number to bind the server socket to")
    .default_value(6379)
    .scan<'d', int>();

  try {
    DEBUG_LOG("in try block");
    argument_parser.parse_args(argc, argv);
  }
  catch(const std::exception& err) {
    std::stringstream ss;  
    ss << err.what() << std::endl << argument_parser;
    DEBUG_LOG(ss.str()); 
    std::exit(1);
  }
  DEBUG_LOG("returning...");
  return 0;
}
