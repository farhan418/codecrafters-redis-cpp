#ifndef POLLMANAGER_HPP
#define POLLMANAGER_HPP

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
#include "utility.hpp"


namespace pm {

    inline void printPollFD(const struct pollfd& pfd) {
        std::stringstream ss;
        ss << "pfd.fd = " << pfd.fd; 
        ss << ", pfd.events = " << pfd.events;
        ss << ", pfd.revents = " << pfd.revents << std::endl;
        DEBUG_LOG(ss.str());
    }

    struct SocketSetting {
    public:
        SocketSetting() {
            resetSocketSettings();
        }

        int resetSocketSettings() {
            socketHostOrIP = "localhost";  // valid inputs - localhost, www.example.com, 16.2.4.74
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
                DEBUG_LOG(utility::colourize("calloc : memory allocation failed", utility::cc::RED));
                exit(1);
            }
            DEBUG_LOG(utility::colourize("Polling Manager Object created.", utility::cc::YELLOW));
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
            static long long pollCount = 0;
            if (poll(pollfdArr, pollfdArrSize, timeout_ms) == -1) {
                DEBUG_LOG(utility::colourize("poll failed", utility::cc::RED));
                return -1;
            }
             
            // ss << "poll call done, pollCount=" << pollCount++ << ", listenerSocketFD = " << listenerSocketFD << ", connectorSocketFD = " << connectorSocketFD << ", pollfdArrSize = " << pollfdArrSize;
            // DEBUG_LOG(ss.str());
            // for(int i = 0; i < pollfdArrSize; i++)
            //     printPollFD(pollfdArr[i]);
            
            for(int i = 0; i < pollfdArrSize; i++) {
                if (pollfdArr[i].revents & (POLLIN | POLLOUT)) {
                    if (pollfdArr[i].fd != listenerSocketFD) {
                        // DEBUG_LOG("a socketFD is ready : " + std::to_string(pollfdArr[i].fd));
                        if (_isSocketOpen(pollfdArr[i].fd)) {
                            readyFDsVec.push_back(pollfdArr[i]);  // if the socket can be read from, push to the ready list
                        }
                        else {
                            _deleteSocketFDFromPollfdArr(pollfdArr[i].fd);  // delete if the socket cannot be read from
                        }
                    }
                    else if (/*(pollfdArr[i].fd == listenerSocketFD) &&*/ (pollfdArr[i].revents & POLLIN)) {
                        // if listener is ready to read, it means a new client connection
                        struct sockaddr_storage remoteAddr; // Client address
                        socklen_t addrLen = sizeof(remoteAddr);
                        int newSocketFD = accept(listenerSocketFD, (struct sockaddr*)&remoteAddr, &addrLen);
                        if (newSocketFD == -1) {
                            DEBUG_LOG(utility::colourize("Error accepting client connection request", utility::cc::RED));
                            continue;  // continue to next socketFD in pollfdArr
                        }
                        // add the new socket to polling array 
                        if (_addSocketFDToPollfdArr(newSocketFD, POLLIN /*| POLLINOUT*/) != 0) {
                            DEBUG_LOG(utility::colourize("failed to add newSocketFD to pollfdArr", utility::cc::RED));
                            continue;
                        }
                        // print client address
                        char remoteIP[INET6_ADDRSTRLEN];
                        if (NULL == inet_ntop(remoteAddr.ss_family, _getInAddr((struct sockaddr*)&remoteAddr), remoteIP, INET6_ADDRSTRLEN)) {
                            DEBUG_LOG(utility::colourize("failed to convert address to human readable form", utility::cc::RED));
                        }
                        std::stringstream ss;  
                        ss << "pollserver: new connection from \""<< remoteIP;
                        ss << "\" on socketFD = " << newSocketFD;
                        DEBUG_LOG(utility::colourize(ss.str(), utility::cc::GREEN));
                    }  // if it is listener socket with POLLIN
                }  // if an element of pollfdArr has some event
            }  // for loop iterating pollfdArr
            return 0;
        }

        int createConnectorSocket(const struct SocketSetting& socketSetting) {
            // returns connectorSocketFD (negative means failed, >0 means created successfully)
            // DEBUG_LOG("in public createConnectorSocket");
            if (0 != _createConnectorSocket(socketSetting)) {
                DEBUG_LOG(utility::colourize("failed to create connector socket", utility::cc::RED));
                return -1;
            }
            std::stringstream ss;  
            ss << "Successfully created connectorSocketFD = " << connectorSocketFD;
            DEBUG_LOG(utility::colourize(ss.str(), utility::cc::GREEN));
            return connectorSocketFD;
        }

        int createListenerSocket(const struct SocketSetting& socketSetting) {
            // returns listenerSocketFD (negative means failed, >0 means created successfully)
            if (0 != _createListenerSocket(socketSetting)) {
                DEBUG_LOG(utility::colourize("failed to create listener socket", utility::cc::RED));
                return -1;
            }
            std::stringstream ss;  
            ss << "Successfully created listenerSocketFD = " << listenerSocketFD;
            DEBUG_LOG(utility::colourize(ss.str(), utility::cc::GREEN));
            return listenerSocketFD;
        }

        int deleteSocketFDFromPollfdArr(int socketFD) {
            return _deleteSocketFDFromPollfdArr(socketFD);
        }

    private:
        // private member functions
        int _createConnectorSocket(const struct SocketSetting& socketSetting) {
            // returns connectorSocketFD (negative means failed, >0 means successfully created socket)
            struct addrinfo hints, *servinfo, *p;

            memset(&hints, 0, sizeof hints);
            hints.ai_family = socketSetting.socketDomain;  // IPv4 / IPv6
            hints.ai_socktype = socketSetting.socketType;  // TCP

            // DEBUG_LOG("in private _createConnectorSocket");

            int rv = getaddrinfo(socketSetting.socketHostOrIP.c_str(), socketSetting.socketPortOrService.c_str(), &hints, &servinfo);
            if (rv != 0) {
                const int bufSize = 256;
                char charBuf[bufSize];
                snprintf(charBuf, bufSize, "pollserver: %s\n", gai_strerror(rv));
                DEBUG_LOG(utility::colourize(charBuf, utility::cc::RED));
                return -1;
            }
            DEBUG_LOG(utility::colourize("done getaddrinfo, rv = " + std::to_string(rv), utility::cc::YELLOW));
            for(p = servinfo; p != NULL; p = p->ai_next) {
                std::stringstream ss1;
                ss1 << "p->ai_family=" << p->ai_family << ", p->ai_socktype=" << p->ai_socktype << ", p->ai_protocol=" << p->ai_protocol;
                DEBUG_LOG(utility::colourize(ss1.str(), utility::cc::YELLOW));
            }
            
            for(p = servinfo; p != NULL; p = p->ai_next) {
                connectorSocketFD = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (-1 == connectorSocketFD) {
                    DEBUG_LOG(utility::colourize("connectorSocketFD creation failed...", utility::cc::RED));
                    continue;
                }
                DEBUG_LOG(utility::colourize("connector socket creation in progress...", utility::cc::YELLOW));

                if (socketSetting.isSocketNonBlocking) {
                    int flags = fcntl(connectorSocketFD, F_GETFL, 0);
                    if (flags < 0) {
                        DEBUG_LOG(utility::colourize("fcntl(connectorSocketFD, F_GETFL, 0) failed", utility::cc::RED));
                        flags = 0;
                    }
                    if (fcntl(connectorSocketFD, F_SETFL, flags | O_NONBLOCK) < 0) {
                        DEBUG_LOG(utility::colourize("fcntl(connectorSocketFD, F_SETFL, flags | O_NONBLOCK) failed", utility::cc::RED));
                    }
                    DEBUG_LOG(utility::colourize("made connector socket Non blocking...", utility::cc::YELLOW));
                }
                else {
                    DEBUG_LOG(utility::colourize("creating connector socket in blocking mode", utility::cc::YELLOW));
                }

                if (connect(connectorSocketFD, p->ai_addr, p->ai_addrlen) == -1) {
                    DEBUG_LOG(utility::colourize("client: connect failed", utility::cc::RED));
                    close(connectorSocketFD);
                    connectorSocketFD = -1;
                    continue;
                }

                break;  // socket creation was successful
            }
            
            if (p == NULL) {
                return -1;
            }

            char remoteIP[INET6_ADDRSTRLEN];
            if (NULL == inet_ntop(p->ai_family, _getInAddr((struct sockaddr*)p->ai_addr), remoteIP, INET6_ADDRSTRLEN)) {
                DEBUG_LOG(utility::colourize("failed to convert address to human readable form", utility::cc::RED));
            }
            std::stringstream ss;  
            ss << "client: connecting to : " << remoteIP;
            DEBUG_LOG(utility::colourize(ss.str(), utility::cc::YELLOW));

            servinfo = p = NULL;
            freeaddrinfo(servinfo); // All done with this
            
            if (_addSocketFDToPollfdArr(connectorSocketFD, POLLIN | POLLOUT) != 0) {
                DEBUG_LOG(utility::colourize("failed to add connectorSocketFD=" + std::to_string(connectorSocketFD) + " to pollfdArr", utility::cc::RED));
                return -1;
            }
            return 0;
        }

        int _createListenerSocket(const struct SocketSetting& socketSetting) {
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
                DEBUG_LOG(utility::colourize(charBuf, utility::cc::RED));
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
                        DEBUG_LOG(utility::colourize("fcntl(listenerSocketFD, F_GETFL, 0) failed", utility::cc::RED));
                        flags = 0;
                    }
                    if (fcntl(listenerSocketFD, F_SETFL, flags | O_NONBLOCK) < 0) {
                        DEBUG_LOG(utility::colourize("fcntl(listenerSocketFD, F_SETFL, flags | O_NONBLOCK) failed", utility::cc::RED));
                    }
                }

                if (socketSetting.isReuseSocket) {
                    int reuse = static_cast<int>(socketSetting.isReuseSocket);
                    if (setsockopt(listenerSocketFD, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
                        DEBUG_LOG(utility::colourize("setsockopt failed", utility::cc::RED));
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
                DEBUG_LOG(utility::colourize("listen failed", utility::cc::RED));
                return 1;
            }
            
            if (_addSocketFDToPollfdArr(listenerSocketFD, POLLIN /*| POLLINOUT*/) != 0) {
                DEBUG_LOG(utility::colourize("failed to add listenerSocketFD=" + std::to_string(listenerSocketFD) + " to pollfdArr", utility::cc::RED));
                return 1;
            }
            return 0;
        }

        bool _isSocketOpen(int socketFD) {
            // returns true if socket is open else returns false
            if (socketFD < 0)
                return false;  // socket is invalid or closed

            char buffer;
            size_t result = recv(socketFD, &buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT);
            if ( (result == -1) && (errno == EBADF)) {
                return false;  // socket is closed
            }
            return true;  // socket is open
        }

        int _addSocketFDToPollfdArr(int newSocketFD, int events) {
            // If we don't have room, add more space in the pfds array
            if (pollfdArrCapacity == pollfdArrSize) {
                pollfdArrCapacity += 100;
                pollfdArr = static_cast<struct pollfd*>(realloc(pollfdArr, (sizeof(struct pollfd) * pollfdArrCapacity)));
                if (pollfdArr == NULL) {
                    DEBUG_LOG(utility::colourize("realloc : encountered error while resizing pollfdArr", utility::cc::RED));
                    return -1;
                }
            }

            pollfdArr[pollfdArrSize].fd = newSocketFD;
            pollfdArr[pollfdArrSize].events = events;
            pollfdArrSize++;
            DEBUG_LOG("added socketFD=" + std::to_string(newSocketFD) + " to pollfdArr, pollfdArrSize=" + std::to_string(pollfdArrSize));
            return 0;
        }

        int _deleteSocketFDFromPollfdArr(int socketFD) {
            if (pollfdArrSize <= 0 || socketFD == listenerSocketFD || socketFD == connectorSocketFD)
                return -1;

            DEBUG_LOG(utility::colourize("NOT DELETING SOCKET=" + std::to_string(socketFD), utility::cc::BLUE));
            // int index = 0;
            // while(index < pollfdArrSize) {
            //     if (pollfdArr[index].fd == socketFD)
            //         break;
            //     index++;
            // }

            // if ((index != pollfdArrSize) && _isSocketOpen(pollfdArr[index].fd)) {
            //     close(pollfdArr[index].fd);
            // }

            // pollfdArr[index] = pollfdArr[pollfdArrSize-1];
            // pollfdArrSize--;

            // int temp = (pollfdArrCapacity-pollfdArrSize)/100;
            // if (temp >= 2) {
            //     temp--;
            //     pollfdArrCapacity = pollfdArrCapacity - (100*temp);
            //     pollfdArr = static_cast<struct pollfd*>(realloc(pollfdArr, (sizeof(struct pollfd) * pollfdArrCapacity)));
            //     DEBUG_LOG("updted pollfdArr and pollfdArrCapacity = " + std::to_string(pollfdArrCapacity));
            // }
            // DEBUG_LOG(utility::colourize("deleted socketFD=" + std::to_string(socketFD) + ", pollfdArrSize=" + std::to_string(pollfdArrSize), utility::cc::RED));
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
#endif  // POLLMANAGER_HPP

// struct SocketSetting {
//     public:
//         SocketSetting(const std::string& socketPortOrService) {
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

                    // int _deleteSocketFDFromPollfdArr(int index) {
        //     if (pollfdArrSize <= 0) {
        //         return -1;
        //     }
        //     if (_isSocketOpen(pollfdArr[index].fd)) {
        //         close(pollfdArr[index].fd);
        //     }
        //     pollfdArr[index] = pollfdArr[pollfdArrSize-1];
        //     pollfdArrSize--;

        //     int temp = (pollfdArrCapacity-pollfdArrSize)/100;
        //     if (temp >= 2) {
        //         temp--;
        //         pollfdArrCapacity = pollfdArrCapacity - (100*temp);
        //         pollfdArr = static_cast<struct pollfd*>(realloc(pollfdArr, (sizeof(struct pollfd) * pollfdArrCapacity)));
        //         DEBUG_LOG("updted pollfdArr and pollfdArrCapacity = " + std::to_string(pollfdArrCapacity));
        //     }
        //     return 0;
        // }