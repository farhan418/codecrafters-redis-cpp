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

    enum class Colour : uint8_t {
        RED = 31,
        GREEN = 32,
        YELLOW = 33,
        BLUE = 34
    };

    namespace cc {  // cc - console colours
        const std::string RED = "\033[31m";
        const std::string GREEN = "\033[32m";
        const std::string YELLOW = "\033[33m";
        const std::string BLUE = "\033[34m";
        const std::string RESET = "\033[0m";
    };

    std::string colourize(std::string str, std::string colour) {
        // if (colour == Colour::RED) {
        //     str = cc::RED + str + cc::RESET;
        // }
        // else if (colour == Colour::GREEN) {
        //     str = cc::GREEN + str + cc::RESET;
        // }
        // else if (colour == Colour::YELLOW) {
        //     str = cc::YELLOW + str + cc::RESET;
        // }
        // else if (colour == Colour::BLUE) {
        //     str = cc::BLUE + str + cc::RESET;
        // }
        return colour + str + cc::RESET;
    }

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
        ss << "strNumBytes=" << sampleRespStr.length() << ", \"";
        for(auto& c : sampleRespStr) {
            if (std::isprint(c)) {
                ss << c;
            }
            else {
                switch(c) {
                    case '\r' : ss << "\\r"; break;
                    case '\n' : ss << "\\n"; break;
                    case '\t' : ss << "\\t"; break;
                    default : ss << "\\x" << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(c);
                }
            }
        }
        ss << "\"";
        return ss.str();
    }


    // std::string printExact(const std::string& sampleRespStr) {
    //     std::ostringstream ss;
    //     ss << "strLength=" << sampleRespStr.length() << ", \"";
    //     for(auto& c : sampleRespStr) {
    //         if (c == '\r') {
    //             ss << "\\r";
    //         }
    //         else if (c == '\n') {
    //             ss << "\\n";
    //         }
    //         else
    //             ss << c;
    //     }
    //     ss << "\"";
    //     return ss.str();
    // }
    // *3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$4\r\n6380\r\n

    int readFromSocketFD(int& sockFD, char* buffer, const int& bufferSize, const int& retryCount) {
        // reads from a socket fd and returns number of bytes read

        bool isSuccessfullyRead = false;
        int counter = 0;
        int numBytesRead;

        while (counter < retryCount) {
            memset(buffer, 0, bufferSize);  // bzero is also deprecated POSIX function
            numBytesRead = read(sockFD, buffer, bufferSize);

            // if (numBytesRead <= 0) {
            //     DEBUG_LOG("connection closed (if 0 returned) or Error reading from socket: " + std::to_string(sockFD) + " (if -ve returned.)\n");
            //     return numBytesRead;
            // }
            if (numBytesRead == 0) {
                DEBUG_LOG(utility::colourize("error while reading from socket " + std::to_string(sockFD) + " : connection closed (0 returned)", utility::cc::RED));
                break;
            }
            else if (numBytesRead < 0) {
                DEBUG_LOG(utility::colourize("Error reading from socket: " + std::to_string(sockFD) + " (return value is -ve)", utility::cc::RED));
            }
            else {
                isSuccessfullyRead = true;
                break;
            }
            counter++;
        }

        if (isSuccessfullyRead) {
            for (size_t i = numBytesRead; i < bufferSize; i++)
                buffer[i] = '\0';
            
            std::stringstream ss;
            ss << "read from socket " << sockFD << ", " << numBytesRead << " bytes : " << utility::printExact(buffer);
            DEBUG_LOG(utility::colourize(ss.str(), utility::cc::GREEN));
            
            std::string s(buffer);
            if (numBytesRead != s.length()) {
                std::ostringstream temp;
                temp << "numBytesRead=" << numBytesRead << ", s.length()=" << s.length() << ", s = " << utility::printExact(s);
                for (int i = numBytesRead; i < s.length(); i++) {
                    uint8_t byte = buffer[i];
                    temp << ", buffer[" << i << "] = " << buffer[i] << ", byte = " << static_cast<int>(byte) << "| ";
                    // temp << "buffer[" << i << "] = " << buffer[i] << ", byte = " << byte;
                }
                DEBUG_LOG(utility::colourize(temp.str(), utility::cc::RED));
            }
        }
        return numBytesRead;
    }

    int readFromSocketFD(int& sockFD, char* buffer, const int& bufferSize) {
        // reads from a socket fd and returns number of bytes read

        memset(buffer, 0, bufferSize);  // bzero is also deprecated POSIX function
        int numBytesRead = read(sockFD, buffer, bufferSize);

        if (numBytesRead <= 0) {
            DEBUG_LOG(utility::colourize("numBytesRead=" + std::to_string(numBytesRead) + ", connection closed (if 0 returned) or Error reading from socket: " + std::to_string(sockFD) + " (if -ve returned.)", utility::cc::RED));
            return numBytesRead;
        }

        for (size_t i = numBytesRead; i < bufferSize; i++)
            buffer[i] = '\0';

        // if buffer has garbage value after index numBytes
        std::string s(buffer);
        if (numBytesRead != s.length()) {
            std::ostringstream temp;
            for (int i = numBytesRead; i < s.length(); i++) {
                uint8_t byte = buffer[i];
                temp << "buffer[" << i << "] = " << buffer[i] << ", byte = " << static_cast<int>(byte) << "| ";
            }
            DEBUG_LOG(temp.str());
        }
        else {
            DEBUG_LOG(utility::colourize("numBytesRead=" + std::to_string(numBytesRead) + ", buffer length = " + std::to_string(s.length()), utility::cc::YELLOW));
        }

        std::stringstream ss;
        ss << "read from socket " << sockFD << ", " << numBytesRead << " bytes : " << utility::printExact(buffer);
        DEBUG_LOG(utility::colourize(ss.str(), utility::cc::YELLOW));

        return numBytesRead;
    }

    int writeToSocketFD(int& sockFD, char* buffer, const int& bufferSize, const std::string& content, const int& retryCount) {
        // writes to a socket fd and returns number of bytes sent
        
        if (content.length() > bufferSize) {
            DEBUG_LOG(utility::colourize("content.length() > bufferSize : use sendall()", utility::cc::RED));
            return -1;
        }

        bool isSuccessfullyWritten = false;
        int numBytesWritten;
        int counter = 0;

        while (counter < retryCount) {
            memset(buffer, 0, bufferSize);
            memcpy(buffer, content.c_str(), content.length());
            numBytesWritten = write(sockFD, buffer, content.length());
            if (numBytesWritten == 0) {
                DEBUG_LOG(utility::colourize("error while writing to socket " + std::to_string(sockFD) + " : connection closed (0 returned)", utility::cc::RED);
                break;
            }
            else if (numBytesWritten < 0) {
                DEBUG_LOG(utility::colourize("Error writing to socket: " + std::to_string(sockFD) + " (return value is -ve)\n", utility::cc::RED));
            }
            else {
                isSuccessfullyWritten = true;
                break;
            }
            counter++;
        }

        if (isSuccessfullyWritten) {
            std::stringstream ss;
            ss << "sent to socket " << sockFD << ", " << numBytesWritten << " bytes : " << utility::printExact(buffer);
            DEBUG_LOG(utility::colourize(ss.str(), utility::cc::YELLOW));
        }
        return numBytesWritten;
    }

    int writeToSocketFD(int& sockFD, char* buffer, const int& bufferSize, const std::string& content) {
        // writes to a socket fd and returns number of bytes sent
        
        if (content.length() > bufferSize) {
            DEBUG_LOG(utility::colourize("content.length() > bufferSize : use sendall()", utility::cc::RED));
            return -1;
        }

        memset(buffer, 0, sizeof(buffer));
        memcpy(buffer, content.c_str(), content.length());
        int numBytesWritten = write(sockFD, buffer, content.length());
        if (numBytesWritten <= 0) {
            DEBUG_LOG(utility::colourize("connection closed (if 0 returned) or Error writing to socket: " + std::to_string(sockFD) + " (if -ve returned)", utility::cc::RED));
            return numBytesWritten;
        }

        std::stringstream ss;
        ss << "sent to socket " << sockFD << ", " << numBytesWritten << " bytes : " << utility::printExact(buffer);
        DEBUG_LOG(utility::colourize(ss.str(), utility::cc::YELLOW));
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
