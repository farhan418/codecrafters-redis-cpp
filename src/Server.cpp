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
#include <unordered_set>

#include "RedisCommandCenter.hpp"
#include "RespParser.hpp"
// #include "RdbFileReader.hpp"
#include "utility.hpp"
#include "argparse.hpp"
#include "PollManager.hpp"


// int clientHandler(int, resp::RespParser&, RCC::RedisCommandCenter&, pm::PollManager&, std::unordered_set<int>&);
// int receiveCommandsFromMaster(int, resp::RespParser&, RCC::RedisCommandCenter&, pm::PollManager&);
int process_cmdline_args(int, char**, argparse::ArgumentParser&);

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  argparse::ArgumentParser arg_parser("Redis Server");  // to parse command line arguments
  pm::SocketSetting socketSetting;  // struct object to pass socket settings to PollManager obj 
  pm::PollManager pollManager;  // object to manage and poll() sockets
  RCC::RedisCommandCenter rcc;  // to execute Redis commands
  const int timeout_ms = 500;  // 0 means non blocking; if > 0 then poll() in pm::PollManager::pollSockets() will block for timeout_ms seconds
  // std::vector<struct pollfd> readySocketPollfdVec;  // to get list of sockets which are ready to readFrom or writeTo
  bool isSlaveServer = false;  // to track if current server running is replica or master
  bool isHandShakeSuccessful = false;  // true if replica has done the handshake with the master else false
  bool isConnectedToMasterServer = false;  // true if replica and connected to master else false
  int serverListenerSocketFD = -1;  // if master server; to keep track of socket using which server is listening 
  int masterConnectorSocketFD = -1;  // if replica server; to keep track of socket using which replica is connected to master
  std::ostringstream ss;  // to use in DEBUG_LOG()

  // parsing cmd line arguments
  process_cmdline_args(argc, argv, arg_parser);

  // fetch listeningPortNumber from cmd line argument --port  
  std::string listeningPortNumber = arg_parser.get<std::string>("--port");

  // creating listener socket irrespective of the fact current server is replica or master
  socketSetting.socketPortOrService = listeningPortNumber;  // rest members default value, see definition of struct SocketSetting
  DEBUG_LOG(utility::colourize("listener socket setting : " + socketSetting.getSocketSettingsString(), utility::cc::GREEN));
  serverListenerSocketFD = pollManager.createListenerSocket(socketSetting);

  // if current server is replica, then store info in config_kv and connect to master by creating a new socket
  std::string replicaof = arg_parser.get<std::string>("--replicaof");
  // if (auto replicaof = arg_parser.present("--replicaof")) {
  if (replicaof != "NA") {
    // RCC::RedisCommandCenter::setSlaveInfo(replicaof, listeningPortNumber, "psync2");
    rcc.setSlaveInfo(replicaof, listeningPortNumber, "psync2");
    DEBUG_LOG(utility::colourize("this server is a replica of " + (replicaof), utility::cc::GREEN));
    isSlaveServer = true;
    //start
    // if (0 != socketSetting.resetSocketSettings()) {
    //   DEBUG_LOG("error resetting socketSetting");
    // }
    // std::vector<std::string> hostPortVec = utility::split(replicaof, " ");
    // DEBUG_LOG("hostPortVec[0]=" + hostPortVec[0] + ", hostPortVec[1]=" + hostPortVec[1]);
    // socketSetting.socketHostOrIP = hostPortVec[0];
    // socketSetting.socketPortOrService = hostPortVec[1];
    // socketSetting.socketDomain = AF_INET;
    // socketSetting.isReuseSocket = false;
    // socketSetting.isSocketNonBlocking = false;
    // DEBUG_LOG("connector socket setting: " + socketSetting.getSocketSettingsString());
    // int counter = 0;
    // while (counter < 3) {
    //   masterConnectorSocketFD = pollManager.createConnectorSocket(socketSetting);
    //   counter++;
    //   if (masterConnectorSocketFD > 0)
    //     break;
    // }
    // if (masterConnectorSocketFD < 1) {
    //   DEBUG_LOG("failed to connect to master : " + (replicaof));
    // }
    // else {
    //   DEBUG_LOG("successfully connected to master : " + (replicaof));
    //   if (0 == rcc.doReplicaMasterHandshake(masterConnectorSocketFD)) {
    //     DEBUG_LOG("successfully done handshake with master");
    //     isHandShakeSuccessful = true;
    //     isConnectedToMasterServer = true;
    //   }
    //   else {
    //     DEBUG_LOG("failed to do handshake with master");
    //     isHandShakeSuccessful = false;
    //     isConnectedToMasterServer = false;
    //   }
    // }
    //end
  }
  else {  // means master server & by default isSlaveServer = false; isConnectedToMasterServer = false;
    RCC::RedisCommandCenter::setMasterInfo();
    isSlaveServer = false;
    isConnectedToMasterServer = false;
  }

  if (auto dir = arg_parser.present("--dir")) {
    if (auto dbfilename = arg_parser.present("--dbfilename")) {
      RCC::RedisCommandCenter::setConfigKv("dir", *dir);
      RCC::RedisCommandCenter::setConfigKv("dbfilename", *dbfilename);
      RCC::RedisCommandCenter::read_rdb_file();
    }
    else {
      // continue without reading RDB file, graceful handling
      DEBUG_LOG(utility::colourize("--dir flag not provided not read rdb file", utility::cc::YELLOW));
    }
  }

  uint64_t counter = 0;
  std::unordered_set<int> replicaSocketsSet;  // to keep track of replica sockets
  std::vector<struct pollfd> readySocketPollfdVec;  // to get list of sockets which are ready to readFrom or writeTo

  // infinite loop to poll sockets and listen form new connections and server connected sockets
  for(;;) {
    if (isSlaveServer && !isConnectedToMasterServer) {
      if (0 == rcc.connectToMasterServer(masterConnectorSocketFD, replicaof, pollManager)) {
        isConnectedToMasterServer = true;
        isHandShakeSuccessful = true;
        DEBUG_LOG(utility::colourize("replica SUCCESSFULLY to connect to master server", utility::cc::GREEN));
      }
      else {
        isConnectedToMasterServer = false;  
        isHandShakeSuccessful = false;  
        DEBUG_LOG(utility::colourize("replica failed to connect to master server", utility::cc::RED));
      }
    }

    readySocketPollfdVec.clear();
    if (0 != pollManager.pollSockets(timeout_ms, readySocketPollfdVec)) {
      DEBUG_LOG(utility::colourize("encountered error while polling", utility::cc::RED));
    }
    counter++;
    if ((counter % 100000) == 0) {
      DEBUG_LOG(utility::colourize("polled sockets, now looping...", utility::cc::YELLOW));
    }  

    for(const struct pollfd& pfd : readySocketPollfdVec) {

      if (pfd.fd != masterConnectorSocketFD) {
        // if here => not master socket but client socket
        // client can be any redis client or even a replica server
        // this block handles clients
        // pm::printPollFD(pfd);
        // if (clientHandler(pfd.fd, respParser, rcc, pollManager, replicaSocketsSet) != 0) {
        if (rcc.clientHandler(pfd.fd, pollManager) != 0) {
          ss.clear();
          ss << "error while handling socketFD = " << pfd.fd;
          ss << ", serverListenerSocketFD = " << serverListenerSocketFD;
          ss << ", masterConnectorSocketFD = " << masterConnectorSocketFD;
          DEBUG_LOG(utility::colourize(ss.str(), utility::cc::RED));
          continue;
        }
      }
      else {
        // this block handles replication (if current running server is a replica of another server)
        // if ((!isHandShakeSuccessful) || (!isConnectedToMasterServer)) {
        //   // do handshake if not connected to master server
        //   if (0 == rcc.doReplicaMasterHandshake(masterConnectorSocketFD)) {
        //     DEBUG_LOG("successfully done handshake with master");
        //     isHandShakeSuccessful = true;
        //     isConnectedToMasterServer = true;
        //   }
        //   else {
        //     DEBUG_LOG("failed to do handshake with master");
        //     isHandShakeSuccessful = false;
        //     isConnectedToMasterServer = false;
        //   }
        // }
        if (isConnectedToMasterServer) {
          // if handshake is successful and current server (replica) is connected to master
          // listen to master (masterConnectorSocketFD) for commands
          // if (0 == receiveCommandsFromMaster(masterConnectorSocketFD, respParser, rcc, pollManager)) {
          if (0 == rcc.receiveCommandsFromMaster(masterConnectorSocketFD, pollManager)) {
            DEBUG_LOG(utility::colourize("successfully received from master and updated state", utility::cc::GREEN));
          }
          else {
            DEBUG_LOG(utility::colourize("Failed to receive commands from master", utility::cc::RED));
          }
        }
      }
    }  // looping through all FDs which are ready to be read from or write to
  } // infinite for loop

  return 0;
}

// int clientHandler(int currentSocketFD, resp::RespParser& respParser, RCC::RedisCommandCenter& rcc, pm::PollManager& pollManager, std::unordered_set<int>& replicaSocketsSet) {
  
//   const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
//   char buffer[bufferSize];

//   int numBytes = utility::readFromSocketFD(currentSocketFD, buffer, bufferSize);
//   if (numBytes == 0) {  // if 0 bytes read, it means connection closed
//     DEBUG_LOG("Failed to read message from socket : connection closed\n");
//     pollManager.deleteSocketFDFromPollfdArr(currentSocketFD);
//     return 0;
//   }
//   else if (numBytes < 0) {
//     DEBUG_LOG("Failed to read message from socket.\n");
//     return -1;
//   } 

//   // parsing the buffer for commands
//   respParser.resetParser(buffer);
//   std::vector<std::string> command;
//   respParser.parseCommands(command);

//   // for (auto& eachCommand : command) {
//   //   if (eachCommand.find("REPLCONF") != std::string::npos) {
//   //     replicaSocketsSet.insert(currentSocketFD);
//   //   }
//   // }

//   // find write commands and send to all replicas
//   // for (auto& eachCommand : command) {
//   //   std::unordered_set<std::string> writeCommandsSet{"SET", "DEL"};
//   //   for (auto& writeCommand : writeCommandsSet) {
//   //     if (eachCommand.find(writeCommand) != std::string::npos) {
//   //       for (auto& replicaFD : replicaSocketsSet) {
//   //         numBytes = utility::writeToSocketFD(currentSocketFD, buffer, bufferSize, resp::RespParser::serialize({utility::split(eachCommand, " ")}, resp::RespType::Array));
//   //         if (numBytes < 0) {
//   //           DEBUG_LOG("Failed to write message to replica socket : " + std::to_string(replicaFD));
//   //         }
//   //         else if (numBytes == 0) {  // if 0 bytes, it means connection closed
//   //           DEBUG_LOG("Failed to write message to replica socket : " + std::to_string(replicaFD) + " connection closed\n");
//   //           pollManager.deleteSocketFDFromPollfdArr(replicaFD);
//   //         }
//   //       }
//   //     }
//   //   }
//   // }

//   // process the commands
//   std::vector<std::string> responseStrVec = rcc.processCommands(command);
//   for (auto& responseStr : responseStrVec) {
//     numBytes = utility::writeToSocketFD(currentSocketFD, buffer, bufferSize, responseStr);
//     if (numBytes < 0) {
//       DEBUG_LOG("Failed to write message to socket.\n");
//       return -1;
//     }
//     else if (numBytes == 0) {  // if 0 bytes, it means connection closed
//       DEBUG_LOG("Failed to write message to socket : connection closed\n");
//       pollManager.deleteSocketFDFromPollfdArr(currentSocketFD);
//       return 0;
//     }
//   }
//   // } // while loop to process all tokens (even multiple commands)
//   // close(currentSocketFD);  // PollManager should close connection
//   return 0;
// }

// int receiveCommandsFromMaster(int currentSocketFD, resp::RespParser& respParser, RCC::RedisCommandCenter& rcc, pm::PollManager& pollManager) {
//   const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
//   char buffer[bufferSize];

//   int numBytes = utility::readFromSocketFD(currentSocketFD, buffer, bufferSize);
//   if (numBytes == 0) {  // if 0 bytes read, it means connection closed
//     DEBUG_LOG("Failed to read message from socket : connection closed\n");
//     pollManager.deleteSocketFDFromPollfdArr(currentSocketFD);
//     return 0;
//   }
//   else if (numBytes < 0) {
//     DEBUG_LOG("Failed to read message from socket.\n");
//     return -1;
//   } 

//   // parsing the buffer for commands
//   respParser.resetParser(buffer);
//   std::vector<std::string> command;
//   respParser.parseCommands(command); 
//   // process the commands
//   std::vector<std::string> responseStrVec = rcc.processCommands(command);
//   for (auto& responseStr : responseStrVec) {
//     DEBUG_LOG("processed commands one by one, respective responseStr : " + utility::printExact(responseStr))
//   }
//   // } // while loop to process all tokens (even multiple commands)
//   // close(currentSocketFD);  // PollManager should close connection
//   return 0;
// }

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
    .help("this server is a slave of which server, mention \"<master_host> <master_port>\"")
    .default_value("NA");

  try {
    // DEBUG_LOG("in try block");
    argument_parser.parse_args(argc, argv);
  }
  catch(const std::exception& err) {
    std::stringstream ss;  
    ss << err.what() << std::endl << argument_parser;
    DEBUG_LOG(ss.str()); 
    std::exit(1);
  }
  // DEBUG_LOG("returning...");
  return 0;
}
