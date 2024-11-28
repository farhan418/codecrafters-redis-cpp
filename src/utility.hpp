#ifndef UTILITY_HPP
#define UTILITY_HPP

#include <iostream>
#include <chrono>
#include <ctime>
#include <regex>
#include <sys/socket.h>


// #ifndef DEBUG_LOG
// #define DEBUG_LOG(msg)
// auto now = std::chrono::system_clock::now();\
// std::time_t now_time = std::chrono::system_clock::to_time_t(now);\
// std::cerr << "[" << now_time << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
// #endif

#ifndef DEBUG_LOG
#define DEBUG_LOG(msg) \
std::cerr << std::unitbuf;\
std::cerr << "\n[" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "] [" << __FILE__ << ":" << __LINE__ << "] " << __func__ << "() : " << (msg) << std::endl;
#endif

namespace utility {

    std::vector<std::string> split(const std::string& str, const std::string& delimeter = "\r\n") {
        std::vector<std::string> result;
        std::regex re(delimeter);
        std::sregex_token_iterator first(str.begin(), str.end(), re, -1);
        std::sregex_token_iterator last;
        result.assign(first, last);
        return result;
    }

    bool compareCaseInsensitive(const std::string& str1, const std::string& str2) {
        std::string str1_lower = str1;
        std::string str2_lower = str2;

        std::transform(str1_lower.begin(), str1_lower.end(), str1_lower.begin(), ::tolower);
        std::transform(str2_lower.begin(), str2_lower.end(), str2_lower.begin(), ::tolower);

        return str1_lower.compare(str2_lower) == 0;
    }

    uint8_t convertHexCharToByte(char hexChar4bits) {
        uint8_t byte = 0;
        if (hexChar4bits >= '0' && hexChar4bits <= '9') {
            byte = hexChar4bits - '0';
        }
        else if (hexChar4bits >= 'A' && hexChar4bits <= 'F') {
            byte = (hexChar4bits - 'A') + 10;
        }
        else if (hexChar4bits >= 'a' && hexChar4bits <= 'f') {
            byte = (hexChar4bits - 'a') + 10;
        }
        return byte;
    }

    std::string printExact(const std::string& sampleRespStr) {
        std::ostringstream ss;
        ss << "strLength=" << sampleRespStr.length() << ", \"";
        for(auto& c : sampleRespStr) {
            if (c == '\r') {
                ss << "\\r";
            }
            else if (c == '\n') {
                ss << "\\n";
            }
            else
                ss << c;
        }
        ss << "\"";
        return ss.str();
    }
    // *3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$4\r\n6380\r\n

    int readFromSocketFD(int& sockFD, char* buffer, const int& bufferSize, const int& retryCount) {
        // reads from a socket fd and returns number of bytes read

        bool isSuccessfullyRead = false;
        int counter = 0;
        int numBytesRead;

        while (counter < retryCount) {
            memset(buffer, 0, sizeof(buffer));  // bzero is also deprecated POSIX function
            numBytesRead = read(sockFD, buffer, bufferSize);

            // if (numBytesRead <= 0) {
            //     DEBUG_LOG("connection closed (if 0 returned) or Error reading from socket: " + std::to_string(sockFD) + " (if -ve returned.)\n");
            //     return numBytesRead;
            // }
            if (numBytesRead == 0) {
                DEBUG_LOG("error while reading from socket " + std::to_string(sockFD) + " : connection closed (0 returned)");
                break;
            }
            else if (numBytesRead < 0) {
                DEBUG_LOG("Error reading from socket: " + std::to_string(sockFD) + " (return value is -ve)\n");
            }
            else {
                isSuccessfullyRead = true;
                break;
            }
            counter++;
        }

        if (isSuccessfullyRead) {
            std::stringstream ss;
            ss << "read " << numBytesRead << " bytes : ";
            for(int i = 0; i < numBytesRead; i++) {
            if (buffer[i] == '\r')
                ss << "\\r";
            else if (buffer[i] == '\n') 
                ss << "\\n";
            else
                ss << buffer[i];
            if (i == numBytesRead)
                break;
            }
            DEBUG_LOG(ss.str());
        }
        return numBytesRead;
    }

    int readFromSocketFD(int& sockFD, char* buffer, const int& bufferSize) {
        // reads from a socket fd and returns number of bytes read

        memset(buffer, 0, sizeof(buffer));  // bzero is also deprecated POSIX function
        int numBytesRead = read(sockFD, buffer, bufferSize);

        if (numBytesRead <= 0) {
            DEBUG_LOG("connection closed (if 0 returned) or Error reading from socket: " + std::to_string(sockFD) + " (if -ve returned.)\n");
            return numBytesRead;
        }

        std::stringstream ss;
        ss << "read " << numBytesRead << " bytes : ";
        for(int i = 0; i < numBytesRead; i++) {
          if (buffer[i] == '\r')
            ss << "\\r";
          else if (buffer[i] == '\n') 
            ss << "\\n";
          else
            ss << buffer[i];
          if (i == numBytesRead)
            break;
        }
        DEBUG_LOG(ss.str());
        return numBytesRead;
    }

    int writeToSocketFD(int& sockFD, char* buffer, const int& bufferSize, const std::string& content, const int& retryCount) {
        // writes to a socket fd and returns number of bytes sent
        
        if (content.length() > bufferSize) {
            DEBUG_LOG("content.length() > bufferSize : use sendall()");
            return -1;
        }

        bool isSuccessfullyWritten = false;
        int numBytesWritten;
        int counter = 0;

        while (counter < retryCount) {
            memset(buffer, 0, sizeof(buffer));
            memcpy(buffer, content.c_str(), content.length());
            numBytesWritten = write(sockFD, buffer, content.length());
            if (numBytesWritten == 0) {
                DEBUG_LOG("error while writing to socket " + std::to_string(sockFD) + " : connection closed (0 returned)");
                break;
            }
            else if (numBytesWritten < 0) {
                DEBUG_LOG("Error writing to socket: " + std::to_string(sockFD) + " (return value is -ve)\n");
            }
            else {
                isSuccessfullyWritten = true;
                break;
            }
            counter++;
        }

        if (isSuccessfullyWritten) {
            std::stringstream ss;
            ss << "sent " << numBytesWritten << " bytes : " << buffer;
            DEBUG_LOG(ss.str());
        }
        return numBytesWritten;
    }

    int writeToSocketFD(int& sockFD, char* buffer, const int& bufferSize, const std::string& content) {
        // writes to a socket fd and returns number of bytes sent
        
        if (content.length() > bufferSize) {
            DEBUG_LOG("content.length() > bufferSize : use sendall()");
            return -1;
        }

        memset(buffer, 0, sizeof(buffer));
        memcpy(buffer, content.c_str(), content.length());
        int numBytesWritten = write(sockFD, buffer, content.length());
        if (numBytesWritten <= 0) {
            DEBUG_LOG("connection closed (if 0 returned) or Error writing to socket: " + std::to_string(sockFD) + " (if -ve returned)\n");
            return numBytesWritten;
        }

        std::stringstream ss;
        ss << "sent " << numBytesWritten << " bytes : " << buffer;
        DEBUG_LOG(ss.str());
        return numBytesWritten;
    }

};

// static void DEBUG_LOG(std::string msg) {
//   auto now = std::chrono::system_clock::now();
//   std::time_t now_time = std::chrono::system_clock::to_time_t(now);
//   std::cerr << "[" << now_time << "] [" << __FILE__ << ":" << __LINE__ << "] " << (msg) << std::endl;
// }

#endif  // UTILITY_HPP

// int main() {
//     resp::RespParser respParser;
//     std::string sampleRespStr = "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n"
//     respParser.reset(sampleRespStr);

//     std::vector<std::string> commandVec;
//     respParser.parseCommands(commandVec);

//     for (auto& command : commandVec) {
//         std::cout << command << std::endl;
//     }
    
//     return 0;
// }
// #endif  // RESPPARSER_HPP
