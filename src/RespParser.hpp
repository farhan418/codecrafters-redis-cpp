#ifndef RESPPARSER_HPP
#define RESPPARSER_HPP

#include <vector>
#include <iostream>
#include <string>
#include <sstream>
#include "utility.hpp"

namespace resp {
  enum class RespType : unsigned char {
    /* Refer https://redis.io/docs/latest/develop/reference/protocol-spec/#resp-protocol-description 
       for protocol description
    */
    SimpleString = '+',  // first byte is '+'
    /*Simple strings are encoded as a plus (+) character, followed by a string.
      The string mustn't contain a CR (\r) or LF (\n) character and is terminated by CRLF (i.e., \r\n).
      format : +<simple string (no \r or \n)>\r\n
    */

    SimpleError = '-',   // first byte is '-'
    /* format : -<ERRORPREFIX> <error-message>\r\n
       ERRORPREFIX: eg : ERR (generic), WRONGTYPE (operating on wrong type)
    */

    Integer = ':', // first byte is ":"
    /* This type is a CRLF-terminated string that represents a signed, base-10, 64-bit integer.
      format :[<+|->]<value>\r\n
      The colon (:) as the first byte.
      An optional plus (+) or minus (-) as the sign.
      One or more decimal digits (0..9) as the integer's unsigned, base-10 value.
      The CRLF terminator.
      For example, :0\r\n and :1000\r\n are integer replies (of zero and one thousand, respectively).
    */

    BulkString = '$', 
    /* A bulk string represents a single binary string. The string can be of any size, but by default, Redis limits it to 512 MB (see the proto-max-bulk-len configuration directive).
      RESP encodes bulk strings in the following way:
      format : $<length>\r\n<data>\r\n
      The dollar sign ($) as the first byte.
      One or more decimal digits (0..9) as the string's length, in bytes, as an unsigned, base-10 value.
      The CRLF terminator.
      The data.
      A final CRLF.
      So the string "hello" is encoded as follows:
      $5\r\nhello\r\n
      The empty string's encoding is:
      $0\r\n\r\n
    */

    Array = '*',  // first byte is '*'
    /*
      RESP Arrays' encoding uses the following format:
      *<number-of-elements>\r\n<element-1>...<element-n>
      An asterisk (*) as the first byte.
      One or more decimal digits (0..9) as the number of elements in the array as an unsigned, base-10 value.
      The CRLF terminator.
      An additional RESP type for every element of the array.
      So an empty Array is just the following:
      *0\r\n
      Whereas the encoding of an array consisting of the two bulk strings "hello" and "world" is:
      *2\r\n$5\r\nhello\r\n$5\r\nworld\r\n
    */

    // not implementing the below RESP3 data Types
    // Nulls = '_',  // first byte is '_'
    /* format : _\r\n 
       can be use to represent NULL_BULK_STRING and NULL_ARRAY in RESP3 only
    */

    // Boolean = "#"  // first byte is "#"
    /*
      RESP booleans are encoded as follows: (AVAILABLE ONLY IN RESP#)
      #<t|f>\r\n
      The octothorpe character (#) as the first byte.
      A t character for true values, or an f character for false ones.
      The CRLF terminator.
    */

  };

  namespace RespConstants {
    // RESP2 has a predetermined value for null bulk string but RESP3 have a dedicated type
    const std::string NULL_BULK_STRING = "$-1\r\n";

    // RESP2 has a predetermined value for null array but RESP3 have a dedicated type
    const std::string NULL_ARRAY = "*-1\r\n";  // null array is an alternative way to represent null value
  
    /* Null Bulk String, Null Arrays and Nulls:
      Due to historical reasons, RESP2 features two specially crafted values
      for representing null values of bulk strings and arrays. This duality
      has always been a redundancy that added zero semantical value to the
      protocol itself.
      The null type, introduced in RESP3, aims to fix this wrong.
    */
    const std::string CRLF = "\r\n";
  };

  class RespParser {
  public:
    RespParser() {
      respBuffer = "";
      respBufferIndex = 0;
    }

    void resetParser(const std::string& respStr) {
      // DEBUG_LOG(utility::printExact(respStr));
      respBuffer = respStr;
      respBufferIndex = 0;
      // DEBUG_LOG(utility::printExact(respBuffer));
    }

    bool isParsedRespBuffer() const {
      /*
        returns true if respBuffer is completely parsed else false
      */
      return respBufferIndex >= respBuffer.length();
    }

    int parseCommands(std::vector<std::string>& commandsVec) {
      /*
        this function parses the respBuffer and appends commands to this vector
        returns 0 on success
      */
      while(!isParsedRespBuffer()) {
        commandsVec.push_back(parseNextRespTypeData());
      }
      DEBUG_LOG("Successfully parsed respBuffer");
      return 0;
    }

    static std::string serialize(const std::vector<std::string>& vec, RespType respType) {
      if (respType == RespType::SimpleString) {
        return "+" + vec[0] + RespConstants::CRLF;
      }
      else if (respType == RespType::SimpleError) {
        return "-" + vec[0] + RespConstants::CRLF;
      }
      else if (respType == RespType::BulkString) {
        return "$" + std::to_string(vec[0].length()) + RespConstants::CRLF + vec[0] + RespConstants::CRLF;
      }
      else if (respType == RespType::Array) {
        std::ostringstream result;
        result << "*" <<vec.size() << RespConstants::CRLF;
        for(auto& element : vec) {
          result << "$" << element.length() << RespConstants::CRLF << element << RespConstants::CRLF;
        }
        return result.str();
      }
      else {
        std::string errMsg = "SERIALIZEERR error serializing data";
        DEBUG_LOG(errMsg);
        return serialize({errMsg}, RespType::SimpleError);
      }
    }
    
  private:
    bool isRespTypeCharOrEndOfRespBuffer() {
      /*
        this functions checks if the respBuffer is completely parsed
        and if the character at respBufferIndex is any of the valid RespType
        Returns true if so otherwise false;
      */
      // DEBUG_LOG(utility::printExact(respBuffer));
        if (isParsedRespBuffer()) {
          DEBUG_LOG("respBuffer was parsed completely");
            return true;
        }

        // reaching here means respBufferIndex < respBuffer.length()
        unsigned char ch = respBuffer[respBufferIndex];

        if (   ch == static_cast<unsigned char>(RespType::SimpleString)
            || ch == static_cast<unsigned char>(RespType::SimpleError)
            || ch == static_cast<unsigned char>(RespType::Integer)
            || ch == static_cast<unsigned char>(RespType::BulkString)
            || ch == static_cast<unsigned char>(RespType::Array)
        )
        {
            DEBUG_LOG("respBuffer[" + std::to_string(respBufferIndex) + "] = " + respBuffer[respBufferIndex] + " is valid state");
            return true;
        }
        DEBUG_LOG("respBuffer[" + std::to_string(respBufferIndex) + "] = " + respBuffer[respBufferIndex] + " is invalid state");
        return false;
    }
    
    std::string parseNextRespTypeData() {
      /*
        this function deserializes the next RESP data in respBuffer
      */
      if (isParsedRespBuffer()) {
        DEBUG_LOG("PARSEERR parsing error occurred due to invalid index (index out of bounds)");
        return RespConstants::NULL_BULK_STRING;
      }
      DEBUG_LOG("in parseNextRespTypeData(), respBufferIndex=" + std::to_string(respBufferIndex));
      if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::SimpleString)) {
        DEBUG_LOG("respBuffer[" + std::to_string(respBufferIndex) + "]=" + respBuffer[respBufferIndex] + ", parsing SimpleString");
        return parseSimpleString();
      }
      else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::SimpleError)) {
        DEBUG_LOG("respBuffer[" + std::to_string(respBufferIndex) + "]=" + respBuffer[respBufferIndex] + ", parsing SimpleError");
        return parseSimpleError();
      }
      else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::Integer)) {
        DEBUG_LOG("respBuffer[" + std::to_string(respBufferIndex) + "]=" + respBuffer[respBufferIndex] + ", parsing Integer");
        return parseInteger();
      }
      else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::BulkString)) {
        DEBUG_LOG("respBuffer[" + std::to_string(respBufferIndex) + "]=" + respBuffer[respBufferIndex] + ", parsing BulkString");
        return parseBulkString();
      }
      else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::Array)) {
        DEBUG_LOG("respBuffer[" + std::to_string(respBufferIndex) + "]=" + respBuffer[respBufferIndex] + ", parsing Array");
        return parseArray();
      }
      else {
        std::string errMsg = "respBuffer[" + std::to_string(respBufferIndex) + "]=" + respBuffer[respBufferIndex] + ",unsupported type";
        DEBUG_LOG(errMsg);
        throw std::runtime_error (errMsg);
        return RespConstants::NULL_BULK_STRING;
      }
    }

    std::string parseSimpleString() {
      size_t crlfIndex = respBuffer.find(RespConstants::CRLF, respBufferIndex);
      if (crlfIndex == std::string::npos) {
        std::string errMsg = "PARSEERR data does not conform to RESP SimpleString encoding : no \\r\\n at the end";
        DEBUG_LOG(errMsg);
        respBufferIndex = respBuffer.length();
        return serialize({errMsg}, RespType::SimpleError);
        // DEBUG_LOG("PARSEERR data does not conform to RESP SimpleString encoding");
        // return RespConstants::NULL_BULK_STRING;
      }
      size_t tempIndex = 1 + respBufferIndex;
      respBufferIndex = crlfIndex + 2;
      if (!isRespTypeCharOrEndOfRespBuffer()) {
        std::string errMsg = "PARSEERR data does not conform to RESP SimpleString encoding: char at " + std::to_string(respBufferIndex) + " is not valid";
        DEBUG_LOG(errMsg);
        respBufferIndex = respBuffer.length();
        return serialize({errMsg}, RespType::SimpleError);
      }
      return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
    }

    std::string parseSimpleError() {
      size_t crlfIndex = respBuffer.find(RespConstants::CRLF, respBufferIndex);
      if (crlfIndex == std::string::npos) {
        DEBUG_LOG("PARSEERR data does not conform to RESP SimpleError encoding : no \\r\\n at the end");
        respBufferIndex = respBuffer.length();
        return RespConstants::NULL_BULK_STRING;
      }
      size_t tempIndex = 1 + respBufferIndex;
      respBufferIndex = crlfIndex + 2;
      if (!isRespTypeCharOrEndOfRespBuffer()) {
        DEBUG_LOG("PARSEERR data does not conform to RESP SimpleError encoding: char at " + std::to_string(respBufferIndex) + " is not valid");
        respBufferIndex = respBuffer.length();
        return RespConstants::NULL_BULK_STRING;
      }
      return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
    }

    std::string parseInteger() {
      /* 
        returns interger in string format with the +/- sign
        not returning integer because returning string is uniform with parsing other RespTypes
      */
      size_t crlfIndex = respBuffer.find(RespConstants::CRLF, respBufferIndex);
      if (crlfIndex == std::string::npos) {
        DEBUG_LOG("PARSEERR data does not conform to RESP Integer encoding : no \\r\\n at the end");
        respBufferIndex = respBuffer.length();
        return RespConstants::NULL_BULK_STRING;
      }
      size_t tempIndex = 1 + respBufferIndex;
      respBufferIndex = crlfIndex + 2;
      if (!isRespTypeCharOrEndOfRespBuffer()) {
        DEBUG_LOG("PARSEERR data does not conform to RESP Integer encoding : char at " + std::to_string(respBufferIndex) + " is not valid");
        respBufferIndex = respBuffer.length();
        return RespConstants::NULL_BULK_STRING;
      }
      return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
    }
    // "*3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$4\r\n6380\r\n"
                //  456789011213
    std::string parseBulkString() {
      size_t crlfIndex = respBuffer.find(RespConstants::CRLF, respBufferIndex);
      if (crlfIndex == std::string::npos) {
        DEBUG_LOG("PARSEERR data does not conform to RESP BulkString encoding : no \\r\\n at the end of bulkString length");
        respBufferIndex = respBuffer.length();
        return RespConstants::NULL_BULK_STRING;
      }

      ++respBufferIndex;  // moving to the first digit of bufferLength
      uint16_t length = std::stol(respBuffer.substr(respBufferIndex, crlfIndex - respBufferIndex));
      respBufferIndex = crlfIndex + 2;  // moving to the first char of bulkString
      DEBUG_LOG("bulkStringLength = " + std::to_string(length) + ", respBufferIndex=" + std::to_string(respBufferIndex));
      
      std::ostringstream ss;
      bool isError = false;
      if (respBufferIndex + length + 1 < respBuffer.length()) {  // +1 is done to include ending \r\n in bulkstring
        ss << respBuffer.substr(respBufferIndex, length);
        respBufferIndex += length + 2;
      }
      else {
        isError = true;
        respBufferIndex = respBuffer.length();
      }
      // for (size_t i = 0; i < length && (!isParsedRespBuffer()); i++) {
        // ss << respBuffer[respBufferIndex++];//*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n
      // }
      DEBUG_LOG("parsed bulk string : " + ss.str() + ", respBufferIndex=" + std::to_string(respBufferIndex) + ", isError=" + std::to_string(isError));
    //   respBufferIndex += 2;
    //   if ((!isParsedRespBuffer()) && respBufferIndex != respBuffer.find(RespConstants::CRLF, respBufferIndex)) {
    //     /*
    //         scenaria when the index is not set to 1 position after next \r\n i.e. after end of bulk string
    //     */
    //     DEBUG_LOG("PARSEERR data does not conform to RESP BulkString encoding");
    //     respBufferIndex = respBuffer.length();
    //     return RespConstants::NULL_BULK_STRING;
    //   }
      if (!isRespTypeCharOrEndOfRespBuffer() || isError) {
        DEBUG_LOG("PARSEERR data does not conform to RESP BulkString encoding");
        return RespConstants::NULL_BULK_STRING;
      }
      return ss.str();
      // crlfIndex = respBuffer.find(RespConstants::CRLF, respBufferIndex);
      // if (crlfIndex == std::string::npos || (length != crlfIndex - respBufferIndex)) {
      //   DEBUG_LOG("PARSEERR data does not conform to RESP BulkString encoding");
      //   return RespConstants::NULL_BULK_STRING;
      // }

      // size_t tempIndex = respBufferIndex;
      // respBufferIndex = crlfIndex + 2;
      // return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
    }

    std::string parseArray() {
      size_t crlfIndex = respBuffer.find(RespConstants::CRLF, respBufferIndex);
      if (crlfIndex == std::string::npos) {
        DEBUG_LOG("PARSEERR data does not conform to RESP Array encoding: no \\r\\n at the end of arrayLength");
        respBufferIndex = respBuffer.length();
        return RespConstants::NULL_BULK_STRING;
      }

      ++respBufferIndex;
      size_t arrayLength = std::stol(respBuffer.substr(respBufferIndex, crlfIndex - respBufferIndex));
      respBufferIndex = crlfIndex + 2;
      DEBUG_LOG("arrayLength = " + std::to_string(arrayLength) + ", respBufferIndex=" + std::to_string(respBufferIndex));

      std::ostringstream command;
      // command << "[";
      for (size_t i = 0; i < arrayLength /*&& (!isParsedRespBuffer())*/; i++) {
        if (i != 0)
            command << " ";
        command << parseNextRespTypeData();
        
        // std::cout << command.str() << std::endl;
      }
      // command << "]";
      if (!isRespTypeCharOrEndOfRespBuffer()) {
        DEBUG_LOG("PARSEERR data does not conform to RESP Array encoding");
        respBufferIndex = respBuffer.length();
        return RespConstants::NULL_BULK_STRING;
      }
      return command.str();
    }

    // member variables
    std::string respBuffer;
    unsigned int respBufferIndex;
  };
};



// int testRespParser(resp::RespParser& respParser, const std::string& sampleRespStr) {
//     std::cout << "\nSample RESP string input : ";
//     printExact(sampleRespStr);

//     std::cout << "\nParsed output : ";
//     respParser.resetParser(sampleRespStr);

//     std::vector<std::string> commandVec;
//     respParser.parseCommands(commandVec);

//     for (auto& command : commandVec) {
//         printExact(command);
//         std::cout << std::endl;
//     }
//     return 0;
// }

// int main() {
//     resp::RespParser respParser;
//     std::string sampleRespStr;
//     // sampleRespStr = "+OK\r\n";
//     // sampleRespStr = "-ERR sample simple error\r\n";
//     // sampleRespStr = "$19sample\r\nbulk\r\nstring\r\n";
//     // sampleRespStr = ":234\r\n";

//     // sampleRespStr = "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     // sampleRespStr = "+OK\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }
    
//     // sampleRespStr = "-ERR error message simple\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     // sampleRespStr = "$2\r\nbulkstring\r\nwith\r\nCRLF\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     // sampleRespStr = "*3\r\n:1\r\n:2\r\n:3\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     sampleRespStr = "*10\r\n*3\r\n:1\r\n:2\r\n:3\r\n+OK\r\n*2\r\n+Hello\r\n-World\r\n+simpleMessage\r\n$24\r\nbulkstring\r\nwith\r\nCRLF\r\n\r\n:2335\r\n+OK\r\n:535235\r\n";
//     if (0 != testRespParser(respParser, sampleRespStr)) {
//         DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     }

//     return 0;
// }

#endif  // RESPPARSER_HPP


// void printExact(const std::string& sampleRespStr) {
//     std::cout << "\"";
//     for(auto& c : sampleRespStr) {
//         if (c == '\r') {
//             std::cout << "\\r";
//         }
//         else if (c == '\n') {
//             std::cout << "\\n";
//         }
//         else
//             std::cout << c;
//     }
//     std::cout << "\"";
// }

// int testRespParser(resp::RespParser& respParser, const std::string& sampleRespStr) {
//     std::cout << "\nSample RESP string input : ";
//     printExact(sampleRespStr);

//     std::cout << "\nParsed output : ";
//     respParser.resetParser(sampleRespStr);

//     std::vector<std::string> commandVec;
//     respParser.parseCommands(commandVec);

//     for (auto& command : commandVec) {
//         printExact(command);
//         std::cout << std::endl;
//     }
//     return 0;
// }

// int main() {
//     resp::RespParser respParser;
//     std::string sampleRespStr;
//     // sampleRespStr = "+OK\r\n";
//     // sampleRespStr = "-ERR sample simple error\r\n";
//     // sampleRespStr = "$19sample\r\nbulk\r\nstring\r\n";
//     // sampleRespStr = ":234\r\n";

//     // sampleRespStr = "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     // sampleRespStr = "+OK\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }
    
//     // sampleRespStr = "-ERR error message simple\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     // sampleRespStr = "$2\r\nbulkstring\r\nwith\r\nCRLF\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     // sampleRespStr = "*3\r\n:1\r\n:2\r\n:3\r\n";
//     // if (0 != testRespParser(respParser, sampleRespStr)) {
//     //     DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     // }

//     sampleRespStr = "*10\r\n*3\r\n:1\r\n:2\r\n:3\r\n+OK\r\n*2\r\n+Hello\r\n-World\r\n+simpleMessage\r\n$26\r\nbulkstring\r\nwith\r\nCRLF\r\n:2335\r\n";
//     if (0 != testRespParser(respParser, sampleRespStr)) {
//         DEBUG_LOG("error occurred while parsing " + sampleRespStr);
//     }

//     return 0;
// }
// #endif  // RESPPARSER_HPP


// namespace resp {
//   enum class RespType : unsigned char {
//     /* Refer https://redis.io/docs/latest/develop/reference/protocol-spec/#resp-protocol-description 
//        for protocol description
//     */
//     SimpleString = '+',  // first byte is '+'
//     /*Simple strings are encoded as a plus (+) character, followed by a string.
//       The string mustn't contain a CR (\r) or LF (\n) character and is terminated by CRLF (i.e., \r\n).
//       format : +<simple string (no \r or \n)>\r\n
//     */

//     SimpleError = '-',   // first byte is '-'
//     /* format : -<ERRORPREFIX> <error-message>\r\n
//        ERRORPREFIX: eg : ERR (generic), WRONGTYPE (operating on wrong type)
//     */

//     Integer = ":", // first byte is ":"
//     /* This type is a CRLF-terminated string that represents a signed, base-10, 64-bit integer.
//       format :[<+|->]<value>\r\n
//       The colon (:) as the first byte.
//       An optional plus (+) or minus (-) as the sign.
//       One or more decimal digits (0..9) as the integer's unsigned, base-10 value.
//       The CRLF terminator.
//       For example, :0\r\n and :1000\r\n are integer replies (of zero and one thousand, respectively).
//     */

//     BulkString = "$", 
//     /* A bulk string represents a single binary string. The string can be of any size, but by default, Redis limits it to 512 MB (see the proto-max-bulk-len configuration directive).
//       RESP encodes bulk strings in the following way:
//       format : $<length>\r\n<data>\r\n
//       The dollar sign ($) as the first byte.
//       One or more decimal digits (0..9) as the string's length, in bytes, as an unsigned, base-10 value.
//       The CRLF terminator.
//       The data.
//       A final CRLF.
//       So the string "hello" is encoded as follows:
//       $5\r\nhello\r\n
//       The empty string's encoding is:
//       $0\r\n\r\n
//     */

//     Array = "*",  // first byte is '*'
//     /*
//       RESP Arrays' encoding uses the following format:
//       *<number-of-elements>\r\n<element-1>...<element-n>
//       An asterisk (*) as the first byte.
//       One or more decimal digits (0..9) as the number of elements in the array as an unsigned, base-10 value.
//       The CRLF terminator.
//       An additional RESP type for every element of the array.
//       So an empty Array is just the following:
//       *0\r\n
//       Whereas the encoding of an array consisting of the two bulk strings "hello" and "world" is:
//       *2\r\n$5\r\nhello\r\n$5\r\nworld\r\n
//     */

//     // not implementing the below RESP3 data Types
//     // Nulls = '_',  // first byte is '_'
//     /* format : _\r\n 
//        can be use to represent NULL_BULK_STRING and NULL_ARRAY in RESP3 only
//     */

//     // Boolean = "#"  // first byte is "#"
//     /*
//       RESP booleans are encoded as follows: (AVAILABLE ONLY IN RESP#)
//       #<t|f>\r\n
//       The octothorpe character (#) as the first byte.
//       A t character for true values, or an f character for false ones.
//       The CRLF terminator.
//     */

//   };

//   struct RespConstants {
//     // RESP2 has a predetermined value for null bulk string but RESP3 have a dedicated type
//     const std::string NULL_BULK_STRING = "$-1\r\n";

//     // RESP2 has a predetermined value for null array but RESP3 have a dedicated type
//     const std::string NULL_ARRAY = "*-1\r\n";  // null array is an alternative way to represent null value
  
//     /* Null Bulk String, Null Arrays and Nulls:
//       Due to historical reasons, RESP2 features two specially crafted values
//       for representing null values of bulk strings and arrays. This duality
//       has always been a redundancy that added zero semantical value to the
//       protocol itself.
//       The null type, introduced in RESP3, aims to fix this wrong.
//     */
//     const std::string CRLF = "\r\n";
//   };

//   class RespParser {
//   public:
//     RespParser() {
//       resetParser();
//     }

//     void resetParser() {
//       respBuffer = "";
//       respBufferIndex = 0;
//     }

//     void resetParser(const std::string& respStr) {
//       resetParser();
//       respBuffer = respStr;
//     }

//     bool isParsedRespBuffer() const {
//       /*
//         returns true if respBuffer is completely parsed else false
//       */
//       return respBufferIndex >= respBuffer.length();
//     }

//     int parseCommands(std::vector<std::string>& commandsVec) {
//       /*
//         this function parses the respBuffer and appends commands to this vector
//         returns 0 on success
//       */
//       while(!isParsedRespBuffer()) {
//         commandsVec.push_back(parseNextRespTypeData());
//       }
//       DEBUG_LOG("Successfully parsed respBuffer");
//       return 0;
//     }

//     static std::string serialize(const std::vector<std::string>& vec, RespType& respType) {
//       if (respType == RespType::SimpleString) {
//         return "+" + vec[0] + RespConstants.CRLF;
//       }
//       else if (respType == RespType::SimpleError) {
//         return "-" + vec[0] + RespConstants.CRLF;
//       }
//       else if (respType == RespType::BulkString) {
//         return "$" + std::to_string(vec[0].length()) + RespConstants.CRLF + vec[0] + RespConstants.CRLF;
//       }
//       else if (respType == RespType::Array) {
//         std::string result = "*" + std::to_string(vec.size()) + RespConstants.CRLF;
//         for(auto& element : vec) {
//           result += "$" + std::to_string(element.length()) + RespConstants.CRLF + element + RespConstants.CRLF;
//         }
//         return result;
//       }
//       else {
//         std::string errMsg = "SERIALIZEERR error serializing data";
//         DEBUG_LOG(errMsg);
//         return serialize({errMsg}, RespType::SimpleError);
//       }
//     }
    
//   private:
//     std::string parseNextRespTypeData() {
//       /*
//         this function deserializes the next RESP data in respBuffer
//       */
//       if (respBufferIndex >= respBuffer.length()) {
//         DEBUG_LOG("PARSEERR parsing error occurred due to invalid index (index out of bounds)");
//         return RespConstants.NULL_BULK_STRING;
//       }

//       if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::SimpleString)) {
//         return parseSimpleString();
//       }
//       else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::SimpleError)) {
//         return parseSimpleError();
//       }
//       else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::Integer)) {
//         return parseInteger();
//       }
//       else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::BulkString)) {
//         return parseBulkString();
//       }
//       else if (respBuffer[respBufferIndex] == static_cast<unsigned char>(RespType::Array)) {
//         return parseArray();
//       }
//       else {
//         return RespConstants.NULL_BULK_STRING;
//       }
//     }

//     std::string parseSimpleString() {
//       size_t crlfIndex = respBuffer.find(RespConstants.CRLF, respBufferIndex);
//       if (crlfIndex == std::string::npos) {
//         std::string errMsg = "PARSEERR data does not conform to RESP SimpleString encoding";
//         DEBUG_LOG(errMsg);
//         respBufferIndex = respBuffer.length();
//         return serialize({errMsg}, RespType::SimpleError);
//         // DEBUG_LOG("PARSEERR data does not conform to RESP SimpleString encoding");
//         // return RespConstants.NULL_BULK_STRING;
//       }
//       size_t tempIndex = 1 + respBufferIndex;
//       respBufferIndex = crlfIndex + 2;
//       return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
//     }

//     std::string parseSimpleError() {
//       size_t crlfIndex = respBuffer.find(RespConstants.CRLF, respBufferIndex);
//       if (crlfIndex == std::string::npos) {
//         DEBUG_LOG("PARSEERR data does not conform to RESP SimpleError encoding");
//         respBufferIndex = respBuffer.length();
//         return RespConstants.NULL_BULK_STRING;
//       }
//       size_t tempIndex = 1 + respBufferIndex;
//       respBufferIndex = crlfIndex + 2;
//       return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
//     }

//     std::string parseInteger() {
//       /* 
//         returns interger in string format with the +/- sign
//         not returning integer because returning string is uniform with parsing other RespTypes
//       */
//       size_t crlfIndex = respBuffer.find(RespConstants.CRLF, respBufferIndex);
//       if (crlfIndex == std::string::npos) {
//         DEBUG_LOG("PARSEERR data does not conform to RESP Integer encoding");
//         respBufferIndex = respBuffer.length();
//         return RespConstants.NULL_BULK_STRING;
//       }
//       size_t tempIndex = 1 + respBufferIndex;
//       respBufferIndex = crlfIndex + 2;
//       return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
//     }
    
//     std::string parseBulkString() {
//       size_t crlfIndex = respBuffer.find(RespConstants.CRLF, respBufferIndex);
//       if (crlfIndex == std::string::npos) {
//         DEBUG_LOG("PARSEERR data does not conform to RESP BulkString encoding");
//         respBufferIndex = respBuffer.length();
//         return RespConstants.NULL_BULK_STRING;
//       }

//       ++respBufferIndex;
//       uint16_t length = std::stol(respBuffer.substr(respBufferIndex, crlfIndex - respBufferIndex));
//       respBufferIndex = crlfIndex + 2;
//       std::ostringstream ss;
//       for (size_t i = 0; i < length && (!isParsedRespBuffer()); i++) {
//         ss << respBuffer[respBufferIndex++];
//       }
//       return ss.str();
//       // crlfIndex = respBuffer.find(RespConstants.CRLF, respBufferIndex);
//       // if (crlfIndex == std::string::npos || (length != crlfIndex - respBufferIndex)) {
//       //   DEBUG_LOG("PARSEERR data does not conform to RESP BulkString encoding");
//       //   return RespConstants.NULL_BULK_STRING;
//       // }

//       // size_t tempIndex = respBufferIndex;
//       // respBufferIndex = crlfIndex + 2;
//       // return respBuffer.substr(tempIndex, crlfIndex - tempIndex);
//     }

//     std::string parseArray() {
//       size_t crlfIndex = respBuffer.find(RespConstants.CRLF, respBufferIndex);
//       if (crlfIndex == std::string::npos) {
//         DEBUG_LOG("PARSEERR data does not conform to RESP Array encoding");
//         respBufferIndex = respBuffer.length();
//         return RespConstants.NULL_BULK_STRING;
//       }

//       ++respBufferIndex;
//       size_t arrayLength = std::stol(respBuffer.substr(respBufferIndex, crlfIndex - respBufferIndex));
//       respBufferIndex = crlfIndex + 2;

//       std::ostringstream command;
//       command << parseNextRespTypeData();
//       for (size_t i = 1; i < arrayLength; i++) {
//         command << " " << parseNextRespTypeData();
//       }
//       return command.str();
//     }

//     // member variables
//     std::string respBuffer;
//     unsigned int respBufferIndex;
//   };
// };
// #endif  // RESPPARSER_HPP

  // class RespParser {
  // public:
  //   RespParser() {
  //     resetParser();
  //   }

  //   void resetParser() {
  //     lastTokenEndIndex = 0;
  //     tokens.clear();
  //   }

  //   void resetParser(const std::string& respStr) {
  //     resetParser();
  //     tokens = utility::split(respStr, "\r\n");
  //     isSplit = true;
  //   }

  //   bool isParsedRespBuffer() const {
  //     return lastTokenIndex == tokens.size();
  //   }

  //   std::string parseNextToken(/*const std::string& respStr=""*/) const {
  //     if (!isSplit) {
  //       // resetParser(respStr);
  //       return ""; // error
  //     }
  //     if (isParsedRespBuffer()) {
  //       return "";  // empty string is false
  //     }
  //     return tokens[lastTokenIndex++];
  //   }

  //   std::vector<std::string> deserialize(const std::string& respToken) {
  //     std::stringstream ss(respToken);
  //     int numBytesRead = respToken.length();
  //     ss << "deserialize : read " << numBytesRead << " bytes : ";
  //     for(int i = 0; i < numBytesRead; i++) {
  //     if (respToken[i] == '\r')
  //         ss << "\\r";
  //     else if (respToken[i] == '\n') 
  //         ss << "\\n";
  //     else
  //         ss << respToken[i];
  //     if (i == numBytesRead)
  //         break;
  //     }
  //     DEBUG_LOG(ss.str());

  //     std::vector<std::string> command;
  //     // DEBUG_LOG("\'" + respToken + "\' in deserialize, respToken[0] = " + respToken[0]);
  //     switch(respToken[0]) {
  //       case '+':
  //       command.push_back(parseSimpleString(respToken));
  //       break;

  //       case '-':
  //       command.push_back(respToken);
  //       break;

  //       case '$':
  //       command.push_back(parseBulkString(respToken));
  //       break;

  //       case '*':
  //       parse_array(command, respToken);
  //       break;

  //       default:
  //       std::string errStr = "invalid respToken : \'" + respToken + "\' (" + std::to_string(respToken.at(0)) + ")";
  //       DEBUG_LOG(errStr);
  //       throw std::runtime_error("runtimeError -> " + errStr);
  //       break;
  //     }
  //     return command;
  //   }

  //   static std::string serialize(const std::vector<std::string>& vec, const std::string& data_type) {
  //     const std::string DELIMETER = "\r\n";
  //     std::string result;

  //     if (data_type == "simple_string")
  //       result = "+" + vec[0] + DELIMETER;
  //     else if (data_type == "bulk_string")
  //       result = "$" + std::to_string(vec[0].length()) + DELIMETER + vec[0] + DELIMETER;
  //     else if (data_type == "array") {
  //       result = "*" + std::to_string(vec.size()) + DELIMETER;
  //       for(auto& element : vec) {
  //         result += "$" + std::to_string(element.length()) + DELIMETER + element + DELIMETER;
  //       }
  //     }
  //     else if (data_type == "error") {
  //       result = "-" + vec[0] + DELIMETER;
  //     }
  //     else {
  //       throw std::runtime_error("invalid data type, cannot serialize");
  //     }
  //     return result;
  //   }
  // private:
  //   std::string parseSimpleString(const std::string& simpleString) {
  //     /* 
  //      * Simple strings are encoded as a plus (+) character, followed by a string.
  //      * The string mustn't contain a CR (\r) or LF (\n) character and is terminated by CRLF (i.e., \r\n).
  //      * Example : +OK\r\n
  //      *
  //      * sample input : simpleString = "+OK"
  //      * return value : "OK"
  //      * here, we are returning the simpleString except for the first variable
  //      */
  //     return simpleString.substr(1, simpleString.length()-1);
  //   }

  //   std::string parseBulkString(const std::string& bulkString) {
  //     /* 
  //       A bulk string represents a single binary string. The string can be of any size, 
  //       but by default, Redis limits it to 512 MB (see the proto-max-bulk-len configuration directive).
        
  //       RESP encodes bulk strings in the following way:
        
  //       $<length>\r\n<data>\r\n
  //       The dollar sign ($) as the first byte.
  //       One or more decimal digits (0..9) as the string's length, in bytes, as an unsigned, base-10 value.
  //       The CRLF terminator.
  //       The data.
  //       A final CRLF.

  //       Test case : $5\r\nhello\r\n
  //       sample input : bulkString = "$5"
  //       output : "hello" (which is next token)
  //     */

  //     if (isParsedRespBuffer()) {
  //       throw std::runtime_error("error extracting bulk string: " + bulkString);
  //     }
  //     return parseNextToken();
  //   }

  //   std::vector<std::string> parse_array(std::vector<std::string>& command, const std::string& array_string) {
  //     /* 
  //       format : *<number-of-elements>\r\n<element-1>...<element-n>
  //         An asterisk (*) as the first byte.
  //         One or more decimal digits (0..9) as the number of elements in the array as an unsigned, base-10 value.
  //         The CRLF terminator.
  //         An additional RESP type for every element of the array.
  //       eg: *2\r\n$5\r\nhello\r\n$5\r\nworld\r\n
  //     */

  //     long long length = std::stol(array_string.substr(1, array_string.length()-1));
  //     // std::vector<std::string> command;
  //     for (long long i = 0; i < length; i++) {
  //       std::string next_token = parseNextToken();
  //       auto temp = deserialize(next_token);
  //       command.push_back(temp[0]);
  //     }
  //     return command;
  //   }

  //   // member variables
  //   long long lastTokenEndIndex;
  //   std::vector<std::string> tokens;
  // };
