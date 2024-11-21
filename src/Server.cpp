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
#include "PollManager.hpp"


int handle_client(int, RespParser&, RedisCommandCenter&);
int process_cmdline_args(int, char**, argparse::ArgumentParser&);

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  argparse::ArgumentParser arg_parser("Redis Server");
  process_cmdline_args(argc, argv, arg_parser);
  DEBUG_LOG("in main after return");
  // uint16_t portNumber = std::stoi(arg_parser.get<std::string>("--port"));
  std::string portNumber = arg_parser.get<std::string>("--port");
  DEBUG_LOG("in main after getting portNumber = " + portNumber);

  if (auto replicaof = arg_parser.present("--replicaof")) {
    RedisCommandCenter::set_slave_info(*replicaof);
  }
  else {  // master
    RedisCommandCenter::set_master_info();
  }

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

  // configuring socket settings
  pm::SocketSettings socketSettings(portNumber);
  // creating SocketManager socket which creates a socket using socketSettings
  pm::PollManager pollManager(socketSettings);

  RespParser respParser;
  RedisCommandCenter rcc;
  const int timeout_ms = 0;  // non blocking; if > 0 then poll() in pollSockets() will block for timeout_ms seconds
  std::vector<struct pollfd> readySocketPollfdVec;

  for(;;) {
    readySocketPollfdVec.clear();
    if (0 != pollManager.pollSockets(timeout_ms, readySocketPollfdVec)) {
      DEBUG_LOG("encountered error while polling");
    }

    for(const struct pollfd pfd : readySocketPollfdVec) {
      int senderSocketFD = pfd.fd;
      // int bytesReceived = recv();
      if (handle_client(senderSocketFD, respParser, rcc) != 0) {
        // delete and close this pollfd.fd
      }
    }

  }
  
  return 0;
}

int handle_client(int clientFD, RespParser& respParser, RedisCommandCenter& rcc) {
  int numBytes = 0;
  char buffer[1024];
  std::stringstream strstream;

  memset(buffer, 0, sizeof(buffer));  // bzero is also deprecated POSIX function
  numBytes = read(clientFD, buffer, sizeof(buffer));
  if (numBytes < 0) {
    DEBUG_LOG("Error reading from socket.\n");
    return -1;
  }

  {
    strstream << "read " << numBytes << " bytes : " << buffer;
    DEBUG_LOG(strstream.str());
  }

  respParser.resetParser(buffer);
  while(!respParser.isParsedAllTokens()) {
    std::vector<std::string> command = respParser.deserialize(respParser.parseNextToken(""));
    std::string response_str = rcc.process(command);
    DEBUG_LOG("response_str : " + response_str);

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, response_str.str(), response_str.length());
    numBytes = write(clientFD, buffer, response_str.length());
    
    strstream << "\nSent " << numBytes << " bytes : " << buffer;
    DEBUG_LOG(strstream.str());
    
    if (numBytes < 0) {
      DEBUG_LOG("Failed to write message to socket.\n");
      return -1;
    }
  } // while loop to process all tokens (even multiple commands)
  // close(clientFD);  // PollManager should close connection
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
    .default_value("6379");
    // .scan<'d', int>();

  argument_parser.add_argument("--replicaof")
    .help("this server is a slave of which server, mention \"<master_host> <master_port>\"");

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
