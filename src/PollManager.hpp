#ifndef SOCKETMANAGER_HPP
#define SOCKETMANAGER_HPP

#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include "logging_utility.hpp"


namespace pm {

    inline void printPollFD(const struct pollfd& pfd) {
        std::stringstream ss;
        ss << "\npfd.fd = " << pfd.fd; 
        ss << ", pfd.events = " << pfd.events;
        ss << ", pfd.revents = " << pfd.revents << std::endl;
        DEBUG_LOG(ss.str());
    }

    struct SocketSettings {
    public:
        SocketSettings() {
            resetSocketSettings();
        }

        int resetSocketSettings() {
            socketHostOrIP = "";  // valid inputs - localhost, www.example.com, 16.2.4.74
            socketPortOrService = "";  // valid inputs - any protocol (service) or portnumber eg : http, 443, etc
            socketDomain = AF_UNSPEC; // AF_INET - for any of IPv4 or IPv6  // default - IPv4
            socketType = SOCK_STREAM;  // default - TCP
            socketProtocol = 0;
            socketBacklogCount = 5;
            isSocketNonBlocking = true; // default - non blocking mode
            isReuseSocket = true;
            return 0;
        }

        std::string getSocketSettingsString() {
            std::stringstream out;
            out << "\nSocket settings :";
            out << "\nsocketHostOrIP : " << socketHostOrIP;
            out << "\nsocketPortOrService : " << socketPortOrService;
            out << "\nsocketDomain : " << socketDomain;
            out << "\nsocketType : " << socketType;
            out << "\nsocketProtocol : " << socketProtocol;
            out << "\nsocketBacklogCount : " << socketBacklogCount;
            out << "\nisSocketNonBlocking : " << isSocketNonBlocking;
            out << "\nisReuseSocket : " << isReuseSocket;
            return out.str();
        }

        std::string socketHostOrIP;  // valid inputs - localhost, www.example.com, 16.2.4.74
        std::string socketPortOrService;  // valid inputs - http, 80, 2353
        int socketDomain;  // AF_INET - IPv4, AF_INET6 - IPv6 or AF_UNSPEC if unspecified i.e. any one
        int socketType;  // SOCK_STREAM, SOCK_DGRAM, etc
        int socketProtocol;  // protocol socket must follow
        int socketBacklogCount;
        bool isSocketNonBlocking;
        bool isReuseSocket;
    };

    class PollManager {
    public:

        PollManager() : listenerSocketFD(-1), connectorSocketFD(-1),
                        pollfdArrCapacity(100), pollfdArrSize(0),
                        pollfdArr(NULL)
        {
            pollfdArr = static_cast<struct pollfd*>(calloc(pollfdArrCapacity, sizeof(struct pollfd)));
            if (pollfdArr == NULL) {
                DEBUG_LOG("calloc : memory allocation failed");
                exit(1);
            }
            DEBUG_LOG("\nPolling Manager Object created.");
        }

        ~PollManager() {
            for(int i = 0; i < pollfdArrSize; i++) {
                if(_isSocketOpen(pollfdArr[i].fd)) {
                    close(pollfdArr[i].fd);
                }
            }
            free(pollfdArr);
        }

        int pollSockets(int timeout_ms, std::vector<struct pollfd>& readyFDsVec) {
            std::stringstream ss;  
            if (poll(pollfdArr, pollfdArrSize, timeout_ms) == -1) {
                DEBUG_LOG("poll failed");
                return -1;
            }

            ss << "\npoll call done, listenerSocketFD = " << listenerSocketFD << ", connectorSocketFD = " << connectorSocketFD << ", pollfdArrSize = " << pollfdArrSize;
            DEBUG_LOG(ss.str());
            for(int i = 0; i < pollfdArrSize; i++)
                printPollFD(pollfdArr[i]);
            
            for(int i = 0; i < pollfdArrSize; i++) {
                if (pollfdArr[i].revents & (POLLIN | POllOUT)) {
                    if (pollfdArr[i].fd != listenerSocketFD) {
                        readyFDsVec.push_back(pollfdArr[i]);
                    }
                    else if (/*(pollfdArr[i].fd == listenerSocketFD) &&*/ (pollfdArr[i].revents & POLLIN)) {
                        // if listener is ready to read, it means a new client connection
                        struct sockaddr_storage remoteAddr; // Client address
                        socklen_t addrLen = sizeof(remoteAddr);
                        int newSocketFD = accept(listenerSocketFD, (struct sockaddr*)&remoteAddr, &addrLen);
                        if (newSocketFD == -1) {
                            DEBUG_LOG("Error accepting client connection request");
                            continue;  // continue to next socketFD in pollfdArr
                        }
                        // add the new socket to polling array 
                        if (_addToPollfdArr(newSocketFD, POLLIN /*| POLLINOUT*/) != 0) {
                            DEBUG_LOG("failed to add newSocketFD to pollfdArr");
                            continue;
                        }
                        // print client address
                        char remoteIP[INET6_ADDRSTRLEN];
                        if (NULL == inet_ntop(remoteAddr.ss_family, _getInAddr((struct sockaddr*)&remoteAddr), remoteIP, INET6_ADDRSTRLEN)) {
                            DEBUG_LOG("failed to convert address to human readable form");
                        }
                        ss.clear();
                        ss << "pollserver: new connection from \""<< remoteIP;
                        ss << "\" on socketFD = " << newSocketFD;
                        DEBUG_LOG(ss.str());
                    }  // if it is listener socket with POLLIN
                }  // if an element of pollfdArr has some event
            }  // for loop iterating pollfdArr
            return 0;
        }

        int createConnectorSocket(const struct SocketSettings& socketSetting) {
            // returns connectorSocketFD (negative means failed, >0 means created successfully)
            if (0 != _createConnectorSocket(socketSetting)) {
                DEBUG_LOG("failed to create listener socket");
                return -1;
            }
            std::stringstream ss;  
            ss << "\nSuccessfully created connectorSocketFD = " << connectorSocketFD;
            DEBUG_LOG(ss.str());
            return connectorSocketFD;
        }

        int createListenerSocket(const struct SocketSettings& socketSetting) {
            // returns listenerSocketFD (negative means failed, >0 means created successfully)
            if (0 != _createListenerSocket(socketSetting)) {
                DEBUG_LOG("failed to create listener socket");
                return -1;
            }
            std::stringstream ss;  
            ss << "\nSuccessfully created listenerSocketFD = " << listenerSocketFD;
            DEBUG_LOG(ss.str());
            return listenerSocketFD;
        }

    private:
        // private member functions
        int _createConnectorSocket(const struct SocketSettings& socketSetting) {
            // returns connectorSocketFD (negative means failed, >0 means successfully created socket)
            struct addrinfo hints, *servinfo, *p;

            memset(&hints, 0, sizeof hints);
            hints.ai_family = socketSetting.socketDomain;  // IPv4 / IPv6
            hints.ai_socktype = socketSetting.socketType;  // TCP

            int rv = getaddrinfo(socketSetting.socketHostOrIP.c_str(), socketSetting.socketPortOrService.c_str(), &hints, &servinfo);
            if (rv != 0) {
                const int bufSize = 256;
                char charBuf[bufSize];
                snprintf(charBuf, bufSize, "pollserver: %s\n", gai_strerror(rv));
                DEBUG_LOG(charBuf);
                return -1;
            }
            
            for(p = servinfo; p != NULL; p = p->ai_next) {
                connectorSocketFD = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (-1 == connectorSocketFD) {
                    DEBUG_LOG("connectorSocketFD creation failed...");
                    continue;
                }

                if (socketSetting.isSocketNonBlocking) {
                    int flags = fcntl(connectorSocketFD, F_GETFL, 0);
                    if (flags < 0) {
                        DEBUG_LOG("fcntl(connectorSocketFD, F_GETFL, 0) failed");
                        flags = 0;
                    }
                    if (fcntl(connectorSocketFD, F_SETFL, flags | O_NONBLOCK) < 0) {
                        DEBUG_LOG("fcntl(connectorSocketFD, F_SETFL, flags | O_NONBLOCK) failed");
                    }
                }

                if (connect(connectorSocketFD, p->ai_addr, p->ai_addrlen) == -1) {
                    DEBUG_LOG("client: connect failed");
                    close(connectorSocketFD);
                    connectorSocketFD = -1;
                    continue;
                }

                break;  // socket creation was successful
            }

            char remoteIP[INET6_ADDRSTRLEN];
            if (NULL == inet_ntop(p->ai_family, _getInAddr((struct sockaddr*)p->ai_addr), remoteIP, INET6_ADDRSTRLEN)) {
                DEBUG_LOG("failed to convert address to human readable form");
            }
            std::stringstream ss;  
            ss << "client: connecting to : " << remoteIP;
            DEBUG_LOG(ss.str());

            if (p == NULL) {
                return -1;
            }
            servinfo = p = NULL;
            freeaddrinfo(servinfo); // All done with this
            
            if (_addToPollfdArr(connectorSocketFD, POLLIN | POLLINOUT) != 0) {
                DEBUG_LOG("failed to add connectorSocketFD to pollfdArr");
                return -1;
            }
            return connectorSocketFD;
        }

        int _createListenerSocket(const struct SocketSettings& socketSetting) {
            // returns 0 on success and else on failure
            struct addrinfo hints, *ai, *p;

            memset(&hints, 0, sizeof hints);
            hints.ai_family = socketSetting.socketDomain;  // IPv4 / IPv6
            hints.ai_socktype = socketSetting.socketType;  // TCP
            hints.ai_flags = AI_PASSIVE;  // set IP address

            int rv = getaddrinfo(NULL, socketSetting.socketPortOrService.c_str(), &hints, &ai);
            if (rv != 0) {
                const int bufSize = 256;
                char charBuf[bufSize];
                snprintf(charBuf, bufSize, "pollserver: %s\n", gai_strerror(rv));
                DEBUG_LOG(charBuf);
                return -1;
            }
            
            for(p = ai; p != NULL; p = p->ai_next) {
                listenerSocketFD = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (listenerSocketFD < 0) {
                    continue;
                }

                if (socketSetting.isSocketNonBlocking) {
                    int flags = fcntl(listenerSocketFD, F_GETFL, 0);
                    if (flags < 0) {
                        DEBUG_LOG("fcntl(listenerSocketFD, F_GETFL, 0) failed");
                        flags = 0;
                    }
                    if (fcntl(listenerSocketFD, F_SETFL, flags | O_NONBLOCK) < 0) {
                        DEBUG_LOG("fcntl(listenerSocketFD, F_SETFL, flags | O_NONBLOCK) failed");
                    }
                }

                if (socketSetting.isReuseSocket) {
                    int reuse = static_cast<int>(socketSetting.isReuseSocket);
                    if (setsockopt(listenerSocketFD, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
                        DEBUG_LOG("setsockopt failed\n");
                        close(listenerSocketFD);
                        listenerSocketFD = -1;
                        continue;
                    }
                }

                if (bind(listenerSocketFD, p->ai_addr, p->ai_addrlen) < 0) {
                    close(listenerSocketFD);
                    listenerSocketFD = -1;
                    continue;
                }

                break;  // socket creation was successful and binding to adddress was successful
            }

            freeaddrinfo(ai); // All done with this

            // If we got here, it means we didn't get bound
            if (p == NULL) {
                return -1;
            }
            ai = p = NULL;

            if (listen(listenerSocketFD, socketSetting.socketBacklogCount) != 0) {
                DEBUG_LOG("listen failed\n");
                return 1;
            }
            
            if (_addToPollfdArr(listenerSocketFD, POLLIN /*| POLLINOUT*/) != 0) {
                DEBUG_LOG("failed to add listenerSocketFD to pollfdArr");
                return 1;
            }

            return 0;
        }

        bool _isSocketOpen(int socketFD) {
            // returns true if socket is open else returns false
            if (socketFD == -1)
                return false;  // socket is invalid or closed

            char buffer;
            size_t result = recv(socketFD, &buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT);
            if ( (result == -1) && (errno == EBADF)) {
                return false;  // socket is closed
            }
            return true;  // socket is open
        }

        int _addToPollfdArr(int newSocketFD, int events) {
            // If we don't have room, add more space in the pfds array
            if (pollfdArrCapacity == pollfdArrSize) {
                pollfdArrCapacity += 100;
                pollfdArr = static_cast<struct pollfd*>(realloc(pollfdArr, (sizeof(struct pollfd) * pollfdArrCapacity)));
                if (pollfdArr == NULL) {
                    DEBUG_LOG("realloc : encountered error while resizing pollfdArr");
                    return -1;
                }
            }

            pollfdArr[pollfdArrSize].fd = newSocketFD;
            pollfdArr[pollfdArrSize].events = events;
            pollfdArrSize++;
            return 0;
        }

        int _deleteFromPollfdArr(int index) {
            if (pollfdArrSize <= 0) {
                return -1;
            }
            if (_isSocketOpen(pollfdArr[index].fd)) {
                close(pollfdArr[index].fd);
            }
            pollfdArr[index] = pollfdArr[pollfdArrSize-1];
            pollfdArrSize--;

            int temp = (pollfdArrCapacity-pollfdArrSize)/100;
            if (temp >= 2) {
                temp--;
                pollfdArrCapacity = pollfdArrCapacity - (100*temp);
                pollfdArr = static_cast<struct pollfd*>(realloc(pollfdArr, (sizeof(struct pollfd) * pollfdArrCapacity)));
                DEBUG_LOG("updted pollfdArr and pollfdArrCapacity = " + std::to_string(pollfdArrCapacity));
            }
            return 0;
        }

        // Get sockaddr, IPv4 or IPv6:
        void* _getInAddr(struct sockaddr *sa) {
            if (sa->sa_family == AF_INET) {
                return &(((struct sockaddr_in*)sa)->sin_addr);
            }
            return &(((struct sockaddr_in6*)sa)->sin6_addr);
        }

        // private member variables
        int listenerSocketFD;
        int connectorSocketFD;
        int pollfdArrCapacity;  // pollfdArr capacity (total space occupied in memory)
        int pollfdArrSize;  // current number of fds to track
        struct pollfd *pollfdArr;  // array of FDs to monitor using poll
    };
}

#endif  // SOCKETMANAGER_HPP

// struct SocketSettings {
//     public:
//         SocketSettings(const std::string& socketPortOrService) {
//             resetSocketSettings(socketPortOrService);
//         }

//         int resetSocketSettings(const std::string& socketPortOrService) {
//             socketHostOrIP = "";  // valid inputs - localhost, www.example.com, 16.2.4.74
//             this->socketPortOrService = socketPortOrService;
//             socketDomain = AF_UNSPEC; // AF_INET - for any of IPv4 or IPv6  // default - IPv4
//             socketType = SOCK_STREAM;  // default - TCP
//             socketProtocol = 0;
//             socketBacklogCount = 5;
//             isSocketNonBlocking = true; // default - non blocking mode
//             isReuseSocket = true;
//             return 0;
//         }

//         std::string getSocketSettingsString() {
//             std::stringstream out;
//             out << "\nSocket settings :";
//             out << "\nsocketHostOrIP : " << socketHostOrIP;
//             out << "\nsocketPortOrService : " << socketPortOrService;
//             out << "\nsocketDomain : " << socketDomain;
//             out << "\nsocketType : " << socketType;
//             out << "\nsocketProtocol : " << socketProtocol;
//             out << "\nsocketBacklogCount : " << socketBacklogCount;
//             out << "\nisSocketNonBlocking : " << isSocketNonBlocking;
//             out << "\nisReuseSocket : " << isReuseSocket;
//             return out.str();
//         }

//         std::string socketHostOrIP;  // valid inputs - localhost, www.example.com, 16.2.4.74
//         std::string socketPortOrService;  // valid inputs - http, 80, 2353
//         int socketDomain;  // AF_INET - IPv4, AF_INET6 - IPv6 or AF_UNSPEC if unspecified i.e. any one
//         int socketType;  // SOCK_STREAM, SOCK_DGRAM, etc
//         int socketProtocol;  // protocol socket must follow
//         int socketBacklogCount;
//         bool isSocketNonBlocking;
//         bool isReuseSocket;
//     };

        // int setServerName(const std::string& newServerName) {
        //     if (newServerName.length() == 0)
        //         return -1;
        //     this->serverName = newServerName;
        //     return 0;
        // }

        // std::string getServerName() {
        //     return serverName;
        // }

        // int setListeningPort(uint16_t listeningPort) {
        //     this->listeningPort = listeningPort;
        //     return 0;  // success
        // }

        // uint16_t getListeningPort() {
        //     return listeningPort;
        // }

                        // polling again to include newSocketFD
                        // if (poll(pollfdArr, pollfdArrSize, timeout_ms) == -1) {
                        //     DEBUG_LOG("poll failed");
                        // }

//  {
            //     ss << "\npollfdArr[" << i << "].fd = " << pollfdArr[i].fd;
            //     ss << ", pollfdArr[" << i << "].events = " << pollfdArr[i].events;
            //     ss << ", pollfdArr[" << i << "].revents = " << pollfdArr[i].revents;
            // }
            // DEBUG_LOG(ss.str());