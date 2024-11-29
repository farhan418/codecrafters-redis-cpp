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
#include "PollManager.hpp"
#include "utility.hpp"

namespace RCC {
  class RedisCommandCenter {
  public:
    RedisCommandCenter() {};

    static std::optional<std::string> getConfigKv(const std::string& key) {
      std::lock_guard<std::mutex> guard(configStoreMutex);
      if (configStore.count(key) == 0) {
          return std::nullopt;
      }
      return configStore[key];
    }

    static int setConfigKv(const std::string& key, const std::string& value) {
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
      std::string db_file_path;
      auto result = getConfigKv("dir");
      if (result.has_value())
        db_file_path = *result;
      result = getConfigKv("dbfilename");
      if(result)
        db_file_path += "/" + *result;
      RdbFileReader rdbFileReader;
      return rdbFileReader.readFile(db_file_path);
    }

    static int setMasterInfo() {
      setConfigKv("role", "master");
      RedisCommandCenter::setConfigKv("master_replid", "8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb");
      RedisCommandCenter::setConfigKv("master_repl_offset", "0");
      return 0;
    }

    static int setSlaveInfo(const std::string replicaof, const std::string listeningPortNumber, const std::string capa) {
      setConfigKv("role", "slave");
      RedisCommandCenter::setConfigKv("replicaof", replicaof);
      RedisCommandCenter::setConfigKv("listening-port", listeningPortNumber);
      RedisCommandCenter::setConfigKv("capa", capa);
      return 0;
    }

    std::vector<std::string> processCommands(int& socketFD, const std::vector<std::string>& commands) {
      // socketFD is the socket which sent commands (to be used if it is replica)
      std::stringstream ss;
      ss << "processing command = ";
      for (auto& c : commands)
        ss << "\"" << c << "\" | ";
      DEBUG_LOG(ss.str());

      std::vector<std::string> responseStrVec;
      for (auto& eachCommand : commands) {
        responseStrVec.push_back(_processSingleCommand(socketFD, eachCommand));
      }
      ss.clear();
      ss << "responseStr : ";
      for (auto& responseStr : responseStrVec) {
        ss << "\"" << utility::printExact(responseStr) << "\" | ";
      }
      // for (auto& responseStr : responseStrVec)
      //   ss << "\n" << responseStr;
      DEBUG_LOG(ss.str());
      return responseStrVec;
    }

    int generateRDBFile(const std::string& outputRDBFileName) {
      if (0 == _generateRDBFile(outputRDBFileName)){
        return 0;
      }
      else
        return -1;
    }

    int connectToMasterServer(int& masterConnectorSocketFD, std::string replicaof, pm::PollManager& pollManager) {
      std::vector<std::string> hostPortVec = utility::split(replicaof, " ");
      DEBUG_LOG("hostPortVec[0]=" + hostPortVec[0] + ", hostPortVec[1]=" + hostPortVec[1]);
      pm::SocketSetting socketSetting;
      socketSetting.socketHostOrIP = hostPortVec[0];
      socketSetting.socketPortOrService = hostPortVec[1];
      socketSetting.socketDomain = AF_INET;
      socketSetting.isReuseSocket = true;
      socketSetting.isSocketNonBlocking = false;//false;
      DEBUG_LOG("connector socket setting: " + socketSetting.getSocketSettingsString());
      // int counter = 0;
      // while (counter < 3) {
      masterConnectorSocketFD = pollManager.createConnectorSocket(socketSetting);
        // counter++;
        // if (masterConnectorSocketFD > 0)
          // break;
      // }
      DEBUG_LOG("masterConnectorSocketFD=" + std::to_string(masterConnectorSocketFD));
      if (masterConnectorSocketFD < 1) {
        DEBUG_LOG("failed to connect to master : " + replicaof);
        return -1;
      }
      else {
        DEBUG_LOG("successfully created master connector socket : " + std::to_string(masterConnectorSocketFD) + ", starting handshake...");
        if (0 == doReplicaMasterHandshake(masterConnectorSocketFD)) {
          DEBUG_LOG("successfully done handshake with master");
        }
        else {
          DEBUG_LOG("failed to do handshake with master, master connector socket : " + std::to_string(masterConnectorSocketFD));
          return -1;
        }
      }
      return 0;
    }

    int doReplicaMasterHandshake(int& serverConnectorSocketFD) {
      return _doReplicaMasterHandshake(serverConnectorSocketFD);
    }

    bool canReadFromSocket(int socketFD) {
            // returns true if socket is open else returns false
            if (socketFD < 0)
                return false;  // socket is invalid or closed

            const int bufferSize = 1024; 
            char buffer[bufferSize];
            size_t result = recv(socketFD, &buffer, bufferSize, MSG_PEEK | MSG_DONTWAIT);
            DEBUG_LOG(utility::colourize(utility::printExact(buffer), utility::cc::BLUE));
            if ( (result == -1) && (errno == EBADF)) {
                return false;  // socket is closed
            }
            return true;  // socket is open
        }

    int receiveCommandsFromMaster(int& masterConnectorSocketFD, pm::PollManager& pollManager) {
      if (!canReadFromSocket(masterConnectorSocketFD)) {
        return -1;
      }
      else {
        DEBUG_LOG(utility::colourize("Data available from master", utility::cc::BLUE));
      }
      
      const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
      char buffer[bufferSize];

      int numBytes = utility::readFromSocketFD(masterConnectorSocketFD, buffer, bufferSize);
      if (numBytes == 0) {  // if 0 bytes read, it means connection closed
        DEBUG_LOG("Failed to read message from socket : connection closed\n");
        // pollManager.deleteSocketFDFromPollfdArr(masterConnectorSocketFD);
        replicaSocketFDSet.erase(masterConnectorSocketFD);
        return 0;
      }
      else if (numBytes < 0) {
        DEBUG_LOG("Failed to read message from socket = " + std::to_string(masterConnectorSocketFD));
        return -1;
      } 

      // parsing the buffer for commands
      respParser.resetParser(buffer);
      std::vector<std::string> command;
      respParser.parseCommands(command); 
      // process the commands
      std::vector<std::string> responseStrVec = processCommands(masterConnectorSocketFD, command);
      for (auto& responseStr : responseStrVec) {
        DEBUG_LOG("processed commands one by one (not sending response to master only updating ), respective responseStr : " + utility::printExact(responseStr))
      }
      return 0;
    }

    int clientHandler(int currentSocketFD, pm::PollManager& pollManager) {
      const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
      char buffer[bufferSize];

      int numBytes = utility::readFromSocketFD(currentSocketFD, buffer, bufferSize);
      if (numBytes == 0) {  // if 0 bytes read, it means connection closed
        DEBUG_LOG("Failed to read message from socket : connection closed\n");
        pollManager.deleteSocketFDFromPollfdArr(currentSocketFD);
        replicaSocketFDSet.erase(currentSocketFD);
        return 0;
      }
      else if (numBytes < 0) {
        DEBUG_LOG("Failed to read message from socket.\n");
        return -1;
      } 

      // parsing the buffer for commands
      respParser.resetParser(buffer);
      std::vector<std::string> command;
      respParser.parseCommands(command);

      // for (auto& eachCommand : command) {
      //   if (eachCommand.find("REPLCONF") != std::string::npos) {
      //     replicaSocketsSet.insert(currentSocketFD);
      //   }
      // }

      // find write commands and send to all replicas
      // for (auto& eachCommand : command) {
      //   std::unordered_set<std::string> writeCommandsSet{"SET", "DEL"};
      //   for (auto& writeCommand : writeCommandsSet) {
      //     if (eachCommand.find(writeCommand) != std::string::npos) {
      //       for (auto& replicaFD : replicaSocketsSet) {
      //         numBytes = utility::writeToSocketFD(currentSocketFD, buffer, bufferSize, resp::RespParser::serialize({utility::split(eachCommand, " ")}, resp::RespType::Array));
      //         if (numBytes < 0) {
      //           DEBUG_LOG("Failed to write message to replica socket : " + std::to_string(replicaFD));
      //         }
      //         else if (numBytes == 0) {  // if 0 bytes, it means connection closed
      //           DEBUG_LOG("Failed to write message to replica socket : " + std::to_string(replicaFD) + " connection closed\n");
      //           pollManager.deleteSocketFDFromPollfdArr(replicaFD);
      //         }
      //       }
      //     }
      //   }
      // }

      // process the commands
      std::vector<std::string> responseStrVec = processCommands(currentSocketFD, command);
      for (auto& responseStr : responseStrVec) {
        numBytes = utility::writeToSocketFD(currentSocketFD, buffer, bufferSize, responseStr);
        if (numBytes < 0) {
          DEBUG_LOG("Failed to write message to socket.\n");
          return -1;
        }
        else if (numBytes == 0) {  // if 0 bytes, it means connection closed
          DEBUG_LOG("Failed to write message to socket(" + std::to_string(currentSocketFD) + ") : connection closed\n");
          // pollManager.deleteSocketFDFromPollfdArr(currentSocketFD);
          replicaSocketFDSet.erase(currentSocketFD);
          return 0;
        }
      }
      // } // while loop to process all tokens (even multiple commands)
      // close(currentSocketFD);  // PollManager should close connection
      return 0;
    }

  private:

    int sendCommandToAllReplicas(const std::string& commandRespStr) {
      const int bufferSize = 1024;
      char buffer[bufferSize];
      int retryCount = 3;
      int numBytes;
      for (int replicaSocketFD : replicaSocketFDSet) {
        // send single command to all replicas
        numBytes = utility::writeToSocketFD(replicaSocketFD, buffer, bufferSize, commandRespStr, retryCount);
        if (numBytes > 0) {
          DEBUG_LOG("successfully sent " + std::to_string(numBytes) + " bytes to socket=" + std::to_string(replicaSocketFD) + ", command : " + utility::printExact(commandRespStr));
        }
        else if (numBytes == 0){
          DEBUG_LOG("writing to replica socket(" + std::to_string(replicaSocketFD) + ") during handshake failed : connection closed");
          return -1;
        }
        else {  // numBytes is -ve
          DEBUG_LOG("writing to replica socket(" + std::to_string(replicaSocketFD) + ") during handshake failed");
        }
      }
      return 0;
    }

    // int _doReplicaMasterHandshake(int& serverConnectorSocketFD, resp::RespParser& respParser) {
    //   std::vector<std::string> handShakeCommands{"PING", "REPLCONF listening-port", "REPLCONF capa", "PSYNC ? -1"};
    //   std::vector<std::string> expectedResultVec{"PONG", "OK", "OK", "FULLRESYNC abcdefghijklmnopqrstuvwxyz1234567890ABCD 0"};
      
    //   auto listeningPortNumber = getConfigKv("listening-port");
    //   if (listeningPortNumber.has_value())
    //     handShakeCommands[1] += " " + (*listeningPortNumber);

    //   auto capa = getConfigKv("capa");
    //   if (capa.has_value())
    //     handShakeCommands[2] += " " + (*capa);
  
    //   std::ostringstream respStrHandShakeCommandsToSend;
    //   std::ostringstream respStrExpectedHandShakeResponse;
    //   std::vector<std::string> commandVec;
    //   // for (auto& hcommand : handShakeCommands) {
    //   //   std::vector<std::string> commandVec = utility::split(hcommand, " ");
    //   //   replicaHandshakeCommands << resp::RespParser::serialize(commandVec, resp::RespType::Array);
    //   // }

    //   for (int i = 0; i < handShakeCommands.size(); i++) {
    //     commandVec = utility::split(handShakeCommands[i], " ");
    //     respStrHandShakeCommandsToSend << resp::RespParser::serialize(commandVec, resp::RespType::Array);
    //     commandVec = utility::split(expectedResultVec[i], " ");
    //     respStrExpectedHandShakeResponse << resp::RespParser::serialize(commandVec, resp::RespType::SimpleString);
    //   }
      
    //   // for (int i = 0; i < handShakeCommands.size(); i++) {
    //   // std::string str = resp::RespParser::serialize(utility::split(handShakeCommands[i], " "), sendDataType);
    //   const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
    //   char buffer[bufferSize];
    //   const int retryCount = 3;
    //   std::string str = respStrHandShakeCommandsToSend.str();
    //   int numBytes = utility::writeToSocketFD(serverConnectorSocketFD, buffer, bufferSize, str, retryCount);
    //   if (numBytes > 0) {
    //     DEBUG_LOG("successfully sent command : " + utility::printExact(str));
    //   }
    //   else if (numBytes == 0){
    //     DEBUG_LOG("writing to socket during handshake failed : connection closed");
    //     return -1;
    //   }
    //   else {  // numBytes is -ve
    //     DEBUG_LOG("writing to socket during handshake failed");
    //   }

    //   numBytes = utility::readFromSocketFD(serverConnectorSocketFD, buffer, bufferSize, retryCount);
    //   if (numBytes > 0) {
    //     DEBUG_LOG("successfully read command : " + utility::printExact(buffer));
    //   }
    //   else if (numBytes == 0) {
    //     DEBUG_LOG("Error reading from socket : connection closed\n");
    //     return -1;
    //   }
    //   else {  // numBytes is -ve
    //     DEBUG_LOG("error while reading from socket");
    //   }

    //   std::string response(buffer);
    //   str = respStrExpectedHandShakeResponse.str();
    //   bool isExpectedResponse = response.substr(0, 29) == str.substr(0, 29);
    //   isExpectedResponse &= response.length() == str.length();
    //   // bool isCase3Matching = false;
    //   // if ((i==3/*PSYNC command*/)) {
    //   //   std::vector<std::string> responseVec = utility::split(buffer);
    //   //   isCase3Matching = utility::compareCaseInsensitive("+FULLRESYNC", responseVec[0]);
    //   //   isCase3Matching = isCase3Matching && (responseVec[1].length() == 40);
    //   //   isCase3Matching = isCase3Matching && (responseVec.size() == 3);
    //   // }
    //   // if (!isCase3Matching || !utility::compareCaseInsensitive(resp::RespParser::serialize(utility::split(expectedResultVec[i]), receiveDataType), response)) {
    //   // }
    //   if (isExpectedResponse) {
    //     DEBUG_LOG("got reply to handShakeCommands as expected, handshake successful");
    //   }
    //   else {
    //     DEBUG_LOG("error occurred while replica master handshake - response not as expected, response : " + utility::printExact(response));
    //   }
    //   // }
    //   return 0;
    // }

    int _doReplicaMasterHandshake(int& serverConnectorSocketFD) {
      std::vector<std::string> handShakeCommands{"PING", "REPLCONF listening-port", "REPLCONF capa", "PSYNC ? -1"};
      std::vector<std::string> expectedResultVec{"PONG", "OK", "OK", "FULLRESYNC abcdefghijklmnopqrstuvwxyz1234567890ABCD 0"};
      
      auto listeningPortNumber = getConfigKv("listening-port");
      if (listeningPortNumber.has_value())
        handShakeCommands[1] += " " + (*listeningPortNumber);

      auto capa = getConfigKv("capa");
      if (capa.has_value())
        handShakeCommands[2] += " " + (*capa);
  
      std::ostringstream respStrHandShakeCommandsToSend;
      std::ostringstream respStrExpectedHandShakeResponse;
      std::vector<std::string> commandVec;

      const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
      char buffer[bufferSize];
      const int retryCount = 3;
      int numBytes;
      std::string singleCommandToSend;

      for (int i = 0; i < handShakeCommands.size(); i++) {
        commandVec = utility::split(handShakeCommands[i], " ");
        singleCommandToSend = resp::RespParser::serialize(commandVec, resp::RespType::Array);
        
        // send single command
        numBytes = utility::writeToSocketFD(serverConnectorSocketFD, buffer, bufferSize, singleCommandToSend, retryCount);
        if (numBytes > 0) {
          // DEBUG_LOG("successfully sent command : \"" + handShakeCommands[i] + "\"");
        }
        else if (numBytes == 0){
          DEBUG_LOG("writing to socket during handshake failed : connection closed");
          return -1;
        }
        else {  // numBytes is -ve
          DEBUG_LOG("writing to socket during handshake failed");
        }

        // reading response
        numBytes = utility::readFromSocketFD(serverConnectorSocketFD, buffer, bufferSize, retryCount);
        if (numBytes > 0) {
          DEBUG_LOG("successfully read reply for handShakeCommand=\"" + handShakeCommands[i] + "\", reply = " + utility::printExact(buffer));
        }
        else if (numBytes == 0) {
          DEBUG_LOG("Error reading from socket : connection closed\n");
          return -1;
        }
        else {  // numBytes is -ve
          DEBUG_LOG("error while reading from socket");
        }

        // checking if response is expected 
        std::string response(buffer);
        commandVec = utility::split(expectedResultVec[i], " ");
        std::string expectedResponse = resp::RespParser::serialize(commandVec, resp::RespType::SimpleString);
        bool isExpectedResponse = false;
        if (i != 3) {
          if (response == expectedResponse) {
            isExpectedResponse = true;
          }
          else {
            isExpectedResponse = false;
          }
        }
        else /*i==3 - PSYNC command*/ {
          std::vector<std::string> responseVec = utility::split(response, " ");
          std::ostringstream oss;
          for (int i = 0; i < responseVec.size(); i++)
            oss << "responseVec["<< i << "]=\"" << responseVec[i] << "\", "; 
          DEBUG_LOG("case 3 : PSYNC COMMAND comparing : " + oss.str());
          isExpectedResponse = utility::compareCaseInsensitive("+FULLRESYNC", responseVec[0]);
          isExpectedResponse = isExpectedResponse && (responseVec[1].length() == 40);
          isExpectedResponse = isExpectedResponse && (responseVec.size() >= 3);
        }

        if (isExpectedResponse) {
          // DEBUG_LOG("got reply to handShakeCommand as expected, handShakeCommand=\"" + handShakeCommands[i] + "\"");
        }
        else {
          DEBUG_LOG("error occurred while replica master handshake - response not as expected, response : " + utility::printExact(response));
          return -1;
        }
      }
      return 0;
    }

    // int _doReplicaMasterHandshake(int& serverConnectorSocketFD, resp::RespParser& respParser) {
    //   std::vector<std::string> handShakeCommands{"PING", "REPLCONF listening-port", "REPLCONF capa", "PSYNC ? -1"};
    //   const std::string receiveDataType = "simple_string";
    //   std::vector<std::string> expectedResultVec{"PONG", "OK", "OK", "FULLRESYNC abcdefghijklmnopqrstuvwxyz1234567890ABCD 0"};
    //   // std::vector<std::string> dataTypeVec{"array", "array", "array", "array"};
    //   // std::vector<std::string> resultDataTypeVec{"simple_string", "simple_string", "simple_string", "simple_string"};
      
    //   auto listeningPortNumber = getConfigKv("listening-port");
    //   if (listeningPortNumber.has_value())
    //     handShakeCommands[1] += " " + (*listeningPortNumber);

    //   auto capa = getConfigKv("capa");
    //   if (capa.has_value())
    //     handShakeCommands[2] += " " + (*capa);
      
    //   const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
    //   char buffer[bufferSize];
    //   const int retryCount = 3;
    //   int numBytes;

    //   for (int i = 0; i < handShakeCommands.size(); i++) {
    //     std::string str = resp::RespParser::serialize(utility::split(handShakeCommands[i], " "), resp::RespType::Array);
    //     numBytes = utility::writeToSocketFD(serverConnectorSocketFD, buffer, bufferSize, str, retryCount);
    //     if (numBytes > 0) {
    //       DEBUG_LOG("successfully sent command : " + utility::printExact(str));
    //     }
    //     else if (numBytes == 0){
    //       DEBUG_LOG("writing to socket during handshake failed : connection closed");
    //       return -1;
    //     }
    //     else {  // numBytes is -ve
    //       DEBUG_LOG("writing to socket during handshake failed");
    //     }

    //     numBytes = utility::readFromSocketFD(serverConnectorSocketFD, buffer, bufferSize, retryCount);
    //     if (numBytes > 0) {
    //       DEBUG_LOG("successfully read command : " + utility::printExact(buffer));
    //       break;
    //     }
    //     else if (numBytes == 0) {
    //       DEBUG_LOG("Error reading from socket : connection closed\n");
    //       return -1;
    //     }
    //     else {  // numBytes is -ve
    //       DEBUG_LOG("error while reading from socket");
    //     }

    //     std::string response(buffer);
    //     bool isCase3Matching = false;
    //     if ((i==3/*PSYNC command*/)) {
    //       std::vector<std::string> responseVec = utility::split(buffer);
    //       isCase3Matching = utility::compareCaseInsensitive("+FULLRESYNC", responseVec[0]);
    //       isCase3Matching = isCase3Matching && (responseVec[1].length() == 40);
    //       isCase3Matching = isCase3Matching && (responseVec.size() == 3);
    //     }
    //     if (!isCase3Matching || !utility::compareCaseInsensitive(resp::RespParser::serialize(utility::split(expectedResultVec[i]), resp::RespType::SimpleString), response)) {
    //       DEBUG_LOG("error occurred while replica master handshake - did not receive reply for ");
    //     }
    //     else {
    //       DEBUG_LOG("got reply to " + handShakeCommands[i] + " as expected");
    //     }
    //   }
    //   return 0;
    // }


    std::string _processSingleCommand(int& socketFD, const std::string& command) {
      std::stringstream ss;
      // for (auto& c : commandVec)
      // ss << "\"" << command << "\" | ";
      // DEBUG_LOG("before command = " + ss.str());
      std::vector<std::string> commandVec = utility::split(command, " ");
      ss.clear();
      for (auto& c : commandVec)
        ss << "\"" << c << "\" | ";
      DEBUG_LOG("after commandVec = " + ss.str() + "commandVec.size()=" + std::to_string(commandVec.size()));
      // commandVec = utility::split(commandVec, " 

      // commandVec PING
      if (utility::compareCaseInsensitive("PING", commandVec[0])) {
        return _commandPING();
      }
      // commandVec ECHO  
      else if (utility::compareCaseInsensitive("ECHO", commandVec[0])) {
        return _commandECHO(commandVec);
      }
      // command SET, SET key value PX ms
      else if (utility::compareCaseInsensitive("SET", commandVec[0])) {
        return _commandSET(socketFD, commandVec);   
      }
      // command GET key
      else if (utility::compareCaseInsensitive("GET", commandVec[0])) {
        return _commandGET(commandVec);
      }
      // command CONFIG GET
      else if (commandVec.size() >= 2 && 
              utility::compareCaseInsensitive("CONFIG", commandVec[0]) &&
              utility::compareCaseInsensitive("GET", commandVec[1])) {
          return _commandCONFIG_GET(commandVec);
      }
      // command KEYS
      else if (commandVec.size() >= 2 && 
              utility::compareCaseInsensitive("KEYS", commandVec[0])) {
        return _commandKEYS(commandVec);
      }
      // command INFO, INFO <section>
      else if (utility::compareCaseInsensitive("INFO", commandVec[0])) {
        return _commandINFO(commandVec);
      }
      // command REPLCONF listening-port <replicaListenerPort>, REPLCONF capa psync2
      else if (utility::compareCaseInsensitive("REPLCONF", commandVec[0])) {
        return _commandREPLCONF(commandVec);
      }
      // command PSYNC ? -1
      else if (utility::compareCaseInsensitive("PSYNC", commandVec[0])) {
        return _commandPSYNC(socketFD, commandVec);
      }
      // Invalid command
      else {
        std::string errStr("err invalid command : " + commandVec[0]);
        return resp::RespParser::serialize({errStr}, resp::RespType::SimpleError);
      }
    }

    // std::vector<std::string> _commandPING() {
    //   std::vector<std::string> reply;
    //   reply.push_back(resp::RespParser::serialize({"PONG"}, "simple_string"));
    //   return reply;
    // }

    std::string _commandPING() {
      std::string response{"PONG"};
      return resp::RespParser::serialize({response}, resp::RespType::SimpleString);
    }

    std::string _commandECHO(const std::vector<std::string>& command) {
      std::string response;
      resp::RespType dataType;
      if (command.size() <2) {
        response = "few arguments provided for ECHO command.";
        dataType = resp::RespType::SimpleError;
      }
      else {
        response = command[1];
        dataType = resp::RespType::BulkString;
      }
      return resp::RespParser::serialize({response}, dataType);
    }

    std::string _commandSET(int& socketFD, const std::vector<std::string>& commandVec) {
      std::string response;
      if (commandVec.size() < 3) {
        response = "few arguments provided for SET command.";
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
      }

      std::string sss = resp::RespParser::serialize(commandVec, resp::RespType::Array);
      DEBUG_LOG(utility::printExact(sss));
      if (0 == sendCommandToAllReplicas(sss)) {
        DEBUG_LOG("successfully sent command to all replicas");
      }
      else {
        DEBUG_LOG("failed to send command to all replicas");
      }

      resp::RespType dataType;
      uint64_t expiry_time_ms = UINT64_MAX;
      if (5 == commandVec.size() && utility::compareCaseInsensitive("PX", commandVec[3])) {
        expiry_time_ms = std::stol(commandVec[4]);
      }
      
      if (0 == redis_data_store_obj.set_kv(commandVec[1], commandVec[2], expiry_time_ms)) {
        response = "OK";
        dataType = resp::RespType::SimpleString;
      }
      else {
        response = "Error while storing key value pair.";
        dataType = resp::RespType::SimpleError;
      }
      return resp::RespParser::serialize({response}, dataType);
    }

    std::string _commandGET(const std::vector<std::string>& command) {
      std::string response;
      if (command.size() < 2) {
        response = "-few arguments provided for GET command.";
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
      }
      
      auto result = redis_data_store_obj.get_kv(command[1]);
      if (result.has_value()) {
        response = *result;
        response = resp::RespParser::serialize({response}, resp::RespType::BulkString);
      }
      else {
        response = resp::RespConstants::NULL_BULK_STRING;
      }
      return response;
    }

    std::string _commandCONFIG_GET(const std::vector<std::string>& command) {
      std::string response;
      if (command.size() < 3) {
        response = "few arguments provided for CONFIG GET command.";
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
      }

      // DEBUG_LOG("in CONFIG GET ");
      auto result = getConfigKv(command[2]);
      if (result.has_value()) {
        response = resp::RespParser::serialize({command[2], *result}, resp::RespType::Array);
      }
      else {
        response = "$-1\r\n";
      }
      return response;
    }

    std::string _commandKEYS(const std::vector<std::string>& command) {
      std::string response;
      std::vector<std::string> reply;
      if (command.size() < 2) {
        response = "-few arguments provided for KEY command.";
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
      //     // throw std::runtime_error("few arguments provided for KEY command.");
      }
      DEBUG_LOG("command[0]=" + command[0] + ", command[1]=\"" + command[1] + "\"");
      // redis_data_store_obj.display_all_key_value_pairs();
      if (0 != redis_data_store_obj.get_keys_with_pattern(reply, command[1])) {
        response = "error occurred while fetching keys";
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
        // throw std::runtime_error("error occurred while fetching keys");
      }
      std::stringstream ss;
      for(auto& e : reply) 
        ss << e << ",| ";
      DEBUG_LOG(ss.str());
      return resp::RespParser::serialize(reply, resp::RespType::Array);
    }

    std::string _commandINFO(const std::vector<std::string>& command) {
      std::vector<std::string> reply;
      std::string response;
      std::string dataType;
      std::string required_section("all");

      if (command.size() >= 2)
        required_section = command[1];

      if (0 != _getInfo(reply, required_section)) {
        response = "error while getting info";
        DEBUG_LOG(response);
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
      }
      std::stringstream ss;  
      for(auto& e : reply) 
        ss << e << ",| ";
      DEBUG_LOG(ss.str());
      return resp::RespParser::serialize(reply, resp::RespType::BulkString);
    }
    
    std::string _commandREPLCONF(const std::vector<std::string>& command) {
      std::string response;
      
      if (command.size() < 3) {
        response = "few arguments provided for REPLCONF command.";
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
      }

      if (utility::compareCaseInsensitive("listening-port", command[1])) {
        // save port
        setConfigKv("replica_listening-port", command[2]);
        DEBUG_LOG("replica is listening at port - " + command[2]);
      }
      else if (utility::compareCaseInsensitive("capa", command[1])) {
        // save capabilities
        setConfigKv("replica_capabalities", command[2]);
        DEBUG_LOG("Capabilities : " + command[2]);
      }
      response = "OK";
      return resp::RespParser::serialize({response}, resp::RespType::SimpleString);
    }

    std::string _commandPSYNC(int& socketFD, const std::vector<std::string>& command) {
      std::string response;

      if (command.size() < 3) {
        response = "few arguments provided for PSYNC command.";
        return resp::RespParser::serialize({response}, resp::RespType::SimpleError);
      }

      // get master replication id; generate replid if not present
      auto masterReplid = getConfigKv("master_replid").value_or("8371b4fb1155b71f4a04d3e1b<random-replid>");

      // get master replication offset; assign default "0" if not present
      auto masterReplOffset = getConfigKv("master_repl_offset").value_or("0");

      std::vector<std::string> reply;
      response = "FULLRESYNC " + masterReplid + " " + masterReplOffset;
      // reply.push_back(resp::RespParser::serialize({response}, resp::RespType::SimpleString));
      response  = resp::RespParser::serialize({response}, resp::RespType::SimpleString);

      // generate rdb file if PSYNC ? -1 
      if ("?" == command[1]) {
        // std::string rdbFileName = "rdbFile_" + masterReplid;
        // _generateRDBFile(RDB_FILE_DIR + rdbFileName);

        // std::ifstream rdbFile(RDB_FILE_DIR + rdbFileName, std::ios::binary);
        // if (!rdbFile) {
        //   response = "failed to open file : " + utility::printExact(rdbFileName);
        //   DEBUG_LOG(response);
        //   reply.push_back(resp::RespParser::serialize({response}, resp::RespType::SimpleError));
        // }
        // get the file size
        // rdbFile.seekg(0, std::ios::end);
        // std::streamsize fileSize = rdbFile.tellg();
        // rdbFile.seekg(0, std::ios::beg);
        // DEBUG_LOG("opened file in binary mode : " + utility::printExact(rdbFileName) + ", fileSize = " + std::to_string(fileSize));

        // std::ostringstream rdbFileContent;
        std::string rdbFileContent = generateEmptyRdbFileContent();
        rdbFileContent = "$" + std::to_string(rdbFileContent.length()) + "\r\n" + rdbFileContent;
        // const std::size_t bufferSize = 4096;
        // char buffer[bufferSize];
        // while (rdbFile.read(buffer, bufferSize)) {
        //     rdbFileContent.write(buffer, rdbFile.gcount());
        //     // DEBUG_LOG("rdbFile.gcount()=" + std::to_string(rdbFile.gcount()));
        //     // for (int i = 0; i < rdbFile.gcount(); i++)
        //     //   rdbFileContent << buffer[i];
        // }
        // DEBUG_LOG("closing rdb file : " + utility::printExact(rdbFileName));
        // rdbFile.close();
        // reply.push_back(rdbFileContent.str());
        DEBUG_LOG("file content : " + utility::printExact(rdbFileContent));
        response += rdbFileContent;
      }
      // return reply;

      // if reached here, it means master is connected with the replica which sent : 1) PING 2) REPLCONF ... 3) REPLCONF ... 4) PSYNC ? -1
      // so add it to replicaSocketFDSet
      replicaSocketFDSet.insert(socketFD);
      std::ostringstream oss; 
      oss << "replicaSocketFDSet = {";
      for (auto& fd : replicaSocketFDSet) {
        oss << fd << ", ";
      }
      oss << "}";
      DEBUG_LOG(oss.str());
      DEBUG_LOG("returning PSYNC response = " + utility::printExact(response));
      return response;
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
          if (auto result = getConfigKv(key)) {
            str += "\r\n" + key + ":" + *result;
          }
        reply.push_back(str.substr(2, str.length()-2));
      }
      return 0;
    }

    std::string generateEmptyRdbFileContent() {
      std::stringstream ss("524544495330303131fa0972656469732d76657205372e322e30fa0a72656469732d62697473c040fa056374696d65c26d08bc65fa08757365642d6d656dc2b0c41000fa08616f662d62617365c000fff06e3bfec0ff5aa2");
      std::string emptyRdbFileContent;
      // std::stringstream ss(emptyRdbFile);
      char high, low;
      uint8_t byte = 0;
      while(ss >> high >> low) {
        byte = utility::convertHexCharToByte(high) << 4;
        byte |= utility::convertHexCharToByte(low);
        emptyRdbFileContent += static_cast<unsigned char>(byte);
      }
      DEBUG_LOG("successfully generated empty rdb content : " + utility::printExact(emptyRdbFileContent));
      return emptyRdbFileContent;
    }

    int _generateRDBFile(const std::string& rdbFileName) {
      std::string emptyRdbFile("524544495330303131fa0972656469732d76657205372e322e30fa0a72656469732d62697473c040fa056374696d65c26d08bc65fa08757365642d6d656dc2b0c41000fa08616f662d62617365c000fff06e3bfec0ff5aa2");
      std::stringstream ss(emptyRdbFile);
      // std::stringstream ss(hexContent);
      std::ofstream fout(rdbFileName, std::ios::binary);
      if (!fout) {
        DEBUG_LOG("error opening file in binary mode : " + utility::printExact(rdbFileName));
        fout.close();
        return 0;
      }
      char high, low;
      uint8_t byte = 0;
      while(ss >> high >> low) {
        byte = utility::convertHexCharToByte(high) << 4;
        byte |= utility::convertHexCharToByte(low);
        fout.put(byte);
      }
      fout.close();
      DEBUG_LOG("successfully generated rdb file : " + utility::printExact(rdbFileName));
      return 0;
    }

    static std::map<std::string, std::string> configStore;
    static std::mutex configStoreMutex;
    static RedisDataStore redis_data_store_obj;
    static const std::string RDB_FILE_DIR;
    static std::unordered_set<int> replicaSocketFDSet;
    static resp::RespParser respParser;  // to parse RESP protocol
  };

  std::map<std::string, std::string> RedisCommandCenter::configStore;
  std::mutex RedisCommandCenter::configStoreMutex;
  RedisDataStore RedisCommandCenter::redis_data_store_obj;
  // const std::string RedisCommandCenter::RDB_FILE_DIR("../rdbfiles/");
  const std::string RedisCommandCenter::RDB_FILE_DIR("./");
  std::unordered_set<int> RedisCommandCenter::replicaSocketFDSet;
  resp::RespParser RedisCommandCenter::respParser;
};

#endif  // REDISCOMMANDCENTER_HPP
// +FULLRESYNC 8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb 0\r\n