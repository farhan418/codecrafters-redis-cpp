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

    std::vector<std::string> process(const std::vector<std::string>& commands) {
      std::vector<std::string> responseStrVec = _process(commands);
      std::stringstream ss;
      ss << "processed command = \"";
      for (auto& c : command)
        ss << c << " ";
      ss << "\", responseStr : ";
      for (auto& responseStr : responseStrVec) {
        ss << utility::printExact(responseStr);
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

    int doReplicaMasterHandshake(int& serverConnectorSocketFD, resp::RespParser& respParser) {
      return _doReplicaMasterHandshake(serverConnectorSocketFD, respParser);
    }

  private:

    int _doReplicaMasterHandshake(int& serverConnectorSocketFD, resp::RespParser& respParser) {
      auto listeningPortNumber = getConfigKv("listening-port");
      if (listeningPortNumber.has_value())
        handShakeCommands[1] += " " + (*listeningPortNumber);

      auto capa = getConfigKv("capa");
      if (capa.has_value())
        handShakeCommands[2] += " " + (*capa);
  
      std::vector<std::string> handShakeCommands{"PING", "REPLCONF listening-port", "REPLCONF capa", "PSYNC ? -1"};
      std::vector<std::string> expectedResultVec{"PONG", "OK", "OK", "FULLRESYNC abcdefghijklmnopqrstuvwxyz1234567890ABCD 0"};
      std::ostringstream respStrHandShakeCommandsToSend;
      std::ostringstream respStrExpectedHandShakeResponse;
      std::vector<std::string> commandVec;
      // for (auto& hcommand : handShakeCommands) {
      //   std::vector<std::string> commandVec = utility::split(hcommand, " ");
      //   replicaHandshakeCommands << resp::RespParser::serialize(commandVec, resp::RespType::Array);
      // }

      for (int i = 0; i < handShakeCommands.size(); i++) {
        commandVec = utility::split(handShakeCommands[i], " ");
        respStrHandShakeCommandsToSend << resp::RespParser::serialize(commandVec, resp::RespType::Array);
        commandVec = utility::split(expectedResultVec[i], " ");
        respStrExpectedHandShakeResponse << resp::RespParser::serialize(commandVec, resp::RespType::SimpleString);
      }
      
      // for (int i = 0; i < handShakeCommands.size(); i++) {
      // std::string str = resp::RespParser::serialize(utility::split(handShakeCommands[i], " "), sendDataType);
      const uint16_t bufferSize = 1024;  // 1KB buffer to use when reading from or writing to socket
      char buffer[bufferSize];
      const int retryCount = 3;
      std::string str = respStrHandShakeCommandsToSend.str();
      int numBytes = utility::writeToSocketFD(serverConnectorSocketFD, buffer, bufferSize, str, retryCount);
      if (numBytes > 0) {
        DEBUG_LOG("successfully sent command : " + utility::printExact(str));
      }
      else if (numBytes == 0){
        DEBUG_LOG("writing to socket during handshake failed : connection closed");
        return -1;
      }
      else {  // numBytes is -ve
        DEBUG_LOG("writing to socket during handshake failed");
      }

      numBytes = utility::readFromSocketFD(serverConnectorSocketFD, buffer, bufferSize, retryCount);
      if (numBytes > 0) {
        DEBUG_LOG("successfully read command : " + utility::printExact(buffer));
        break;
      }
      else if (numBytes == 0) {
        DEBUG_LOG("Error reading from socket : connection closed\n");
        return -1;
      }
      else {  // numBytes is -ve
        DEBUG_LOG("error while reading from socket");
      }

      std::string response(buffer);
      str = respStrExpectedHandShakeResponse.str();
      bool isExpectedResponse = response.substr(0, 29) == str.substr(0, 29);
      isExpectedResponse &= response.length() == str.length();
      // bool isCase3Matching = false;
      // if ((i==3/*PSYNC command*/)) {
      //   std::vector<std::string> responseVec = utility::split(buffer);
      //   isCase3Matching = utility::compareCaseInsensitive("+FULLRESYNC", responseVec[0]);
      //   isCase3Matching = isCase3Matching && (responseVec[1].length() == 40);
      //   isCase3Matching = isCase3Matching && (responseVec.size() == 3);
      // }
      // if (!isCase3Matching || !utility::compareCaseInsensitive(resp::RespParser::serialize(utility::split(expectedResultVec[i]), receiveDataType), response)) {
      // }
      if (isExpectedResponse) {
        DEBUG_LOG("got reply to " + handShakeCommands[i] + " as expected, handshake successful");
      }
      else {
        DEBUG_LOG("error occurred while replica master handshake - response not as expected");
      }
      // }
      return 0;
    }

    // int _doReplicaMasterHandshake(int& serverConnectorSocketFD, resp::RespParser& respParser) {
    //   std::vector<std::string> handShakeCommands{"PING", "REPLCONF listening-port", "REPLCONF capa", "PSYNC ? -1"};
    //   const std::string sendDataType = "array";
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
    //     std::string str = RespParser::serialize(utility::split(handShakeCommands[i], " "), sendDataType);
    //     numBytes = utility::writeToSocketFD(serverConnectorSocketFD, buffer, bufferSize, str, retryCount);
    //     if (numBytes > 0) {
    //       DEBUG_LOG("successfully sent command : " + str);
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
    //       DEBUG_LOG("successfully read command : " + std::string(buffer));
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
    //     if (!isCase3Matching || !utility::compareCaseInsensitive(resp::RespParser::serialize(utility::split(expectedResultVec[i]), receiveDataType), response)) {
    //       DEBUG_LOG("error occurred while replica master handshake - did not receive reply for ");
    //     }
    //     else {
    //       DEBUG_LOG("got reply to " + handShakeCommands[i] + " as expected");
    //     }
    //   }
    //   return 0;
    // }


    std::vector<std::string> _process(const std::vector<std::string>& command) {
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
      else if (command.size() >= 2 && 
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
      // command PSYNC ? -1
      else if (utility::compareCaseInsensitive("PSYNC", command[0])) {
        return _commandPSYNC(command);
      }
      // Invalid command
      else {
        std::string errStr("err invalid command : " + command[0]);
        return {resp::RespParser::serialize({errStr}, resp::RespType::SimpleError)};
      }
    }

    // std::vector<std::string> _commandPING() {
    //   std::vector<std::string> reply;
    //   reply.push_back(resp::RespParser::serialize({"PONG"}, "simple_string"));
    //   return reply;
    // }

    std::vector<std::string> _commandPING() {
      std::string response{"PONG"};
      return {resp::RespParser::serialize({response}, resp::RespType::SimpleString)};
    }

    std::vector<std::string> _commandECHO(const std::vector<std::string>& command) {
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
      return {resp::RespParser::serialize({response}, dataType)};
    }

    std::vector<std::string> _commandSET(const std::vector<std::string>& command) {
      if (command.size() < 3) {
        response = "few arguments provided for SET command.";
        return {resp::RespParser::serialize({response}, resp::RespType::SimpleError)};
      }

      std::string response;
      resp::RespType dataType;
      uint64_t expiry_time_ms = UINT64_MAX;
      if (5 == command.size() && utility::compareCaseInsensitive("PX", command[3])) {
        expiry_time_ms = std::stol(command[4]);
      }
      
      if (0 == redis_data_store_obj.set_kv(command[1], command[2], expiry_time_ms)) {
        response = "OK";
        dataType = resp::RespType::SimpleString;
      }
      else {
        response = "Error while storing key value pair.";
        dataType = resp::RespType::SimpleError;
      }
      return {resp::RespParser::serialize({response}, dataType)};
    }

    std::vector<std::string> _commandGET(const std::vector<std::string>& command) {
      if (command.size() < 2) {
        response = "-few arguments provided for GET command.";
        return {resp::RespParser::serialize({response}, resp::RespType::SimpleError)};
      }
      
      std::string response;
      std::vector<std::string> reply;
      auto result = redis_data_store_obj.get_kv(command[1]);
      if (result.has_value()) {
        response = *result;
        reply.push_back(resp::RespParser::serialize({response}, resp::RespType::BulkString));
      }
      else {
        reply.push_back(resp::RespConstants::NULL_BULK_STRING);
      }
      return reply;
    }

    std::vector<std::string> _commandCONFIG_GET(const std::vector<std::string>& command) {
      if (command.size() < 3) {
        response = "few arguments provided for CONFIG GET command.";
        return {resp::RespParser::serialize({response}, resp::RespType::SimpleError)};
      }

      // DEBUG_LOG("in CONFIG GET ");
      auto result = getConfigKv(command[2]);
      if (result.has_value()) {
        response = resp::RespParser::serialize({command[2], *result}, resp::RespType::Array);
      }
      else {
        response = "$-1\r\n";
      }
      return {response};
    }

    std::vector<std::string> _commandKEYS(const std::vector<std::string>& command) {
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
      return {resp::RespParser::serialize(reply, resp::RespType::Array)};
    }

    std::vector<std::string> _commandINFO(const std::vector<std::string>& command) {
      std::vector<std::string> reply;
      std::string response;
      std::string dataType;
      std::string required_section("all");

      if (command.size() >= 2)
        required_section = command[1];

      if (0 != _getInfo(reply, required_section)) {
        response = "error while getting info";
        DEBUG_LOG(response);
        return {resp::RespParser::serialize({response}, resp::RespType::SimpleError)};
      }
      std::stringstream ss;  
      for(auto& e : reply) 
        ss << e << ",| ";
      DEBUG_LOG(ss.str());
      return {resp::RespParser::serialize(reply, resp::RespType::BulkString)};
    }
    
    std::vector<std::string> _commandREPLCONF(const std::vector<std::string>& command) {
      std::string response;
      
      if (command.size() < 3) {
        response = "few arguments provided for REPLCONF command.";
        return {resp::RespParser::serialize({response}, resp::RespType::SimpleError)};
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
      return {resp::RespParser::serialize({response}, resp::RespType::SimpleString)};
    }

    std::vector<std::string> _commandPSYNC(const std::vector<std::string>& command) {
      std::string response;

      if (command.size() < 3) {
        response = "few arguments provided for PSYNC command.";
        return {resp::RespParser::serialize({response}, resp::RespType::SimpleError)};
      }

      // get master replication id; generate replid if not present
      auto masterReplid = getConfigKv("master_replid").value_or("8371b4fb1155b71f4a04d3e1b<random-replid>");

      // get master replication offset; assign default "0" if not present
      auto masterReplOffset = getConfigKv("master_repl_offset").value_or("0");

      std::vector<std::string> reply;
      response = "FULLRESYNC " + masterReplid + " " + masterReplOffset;
      reply.push_back(resp::RespParser::serialize({response}, resp::RespType::SimpleString));

      // generate rdb file if PSYNC ? -1 
      if ("?" == command[1]) {
        std::string rdbFileName = "rdbFile_" + masterReplid;
        _generateRDBFile(RDB_FILE_DIR + rdbFileName);

        std::ifstream rdbFile(RDB_FILE_DIR + rdbFileName, std::ios::binary);
        if (!rdbFile) {
          DEBUG_LOG("failed to open file : " + RDB_FILE_DIR + rdbFileName);
          response = "failed to open file : " + RDB_FILE_DIR + rdbFileName;
          reply.push_back(resp::RespParser::serialize({response}, resp::RespType::SimpleError));
        }
        // get the file size
        rdbFile.seekg(0, std::ios::end);
        std::streamsize fileSize = rdbFile.tellg();
        rdbFile.seekg(0, std::ios::beg);

        std::ostringstream rdbFileContent;
        rdbFileContent << "$" << fileSize << "\r\n";
        const std::size_t bufferSize = 4096;
        char buffer[bufferSize];
        while (rdbFile.read(buffer, bufferSize)) {
            rdbFileContent.write(buffer, rdbFile.gcount());
        }
        rdbFile.close();
        reply.push_back(rdbFileContent.str());
      }
      return reply;
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

    int _generateRDBFile(const std::string& rdbFileName) {
      std::string emptyRdbFile("524544495330303131fa0972656469732d76657205372e322e30fa0a72656469732d62697473c040fa056374696d65c26d08bc65fa08757365642d6d656dc2b0c41000fa08616f662d62617365c000fff06e3bfec0ff5aa2");
      std::stringstream ss(emptyRdbFile);
      // std::stringstream ss(hexContent);
      std::ofstream fout(rdbFileName, std::ios::binary);
      if (!fout) {
        DEBUG_LOG("error opening file in binary mode : " + rdbFileName);
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
      return 0;
    }

    static std::map<std::string, std::string> configStore;
    static std::mutex configStoreMutex;
    static RedisDataStore redis_data_store_obj;
    static const std::string RDB_FILE_DIR;
  };

  std::map<std::string, std::string> RedisCommandCenter::configStore;
  std::mutex RedisCommandCenter::configStoreMutex;
  RedisDataStore RedisCommandCenter::redis_data_store_obj;
  const std::string RedisCommandCenter::RDB_FILE_DIR("../rdbfiles/");
};

#endif  // REDISCOMMANDCENTER_HPP
