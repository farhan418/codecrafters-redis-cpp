#ifndef SOCKETMANAGER_HPP
#define SOCKETMANAGER_HPP

#include <iostream>
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
    struct SocketSettings {
    public:
        SocketSettings(const std::string& listeningPortOrService) {
            resetSocketSettings(listeningPortOrService);
        }

        int resetSocketSettings(const std::string& listeningPortOrService) {
            this->listeningPortOrService = listeningPortOrService;
            socketDomain = AF_INET;  // default - IPv4
            socketType = SOCK_STREAM;  // default - TCP
            socketProtocol = 0;
            isSocketNonBlocking = true; // default - non blocking mode
            isReuseSocket = true;
        }

    // private:
        // std::string serverName;
        std::string listeningPortOrService;
        int socketDomain;  // AF_INET - IPv4, AF_INET6 - IPv6 or AF_UNSPEC if unspecified i.e. any one
        int socketType;  // SOCK_STREAM, SOCK_DGRAM, etc
        int socketProtocol;  // protocol socket must follow
        bool isSocketNonBlocking;
        bool isReuseSocket;
    };

    class PollManager {
    public:

        PollManager(const struct SocketSettings& socketSettings) :
            ss(socketSettings),
            listenerSocketFD(-1),
            pollfdArrCapacity(100),
            pollfdArrSize(0),
            pollfdArr(NULL)
        {
            pollfdArr = static_cast<struct pollfd*>(calloc(pollfdArrCapacity, sizeof(struct pollfd)));
            if (pollfdArr == NULL) {
                DEBUG_LOG("calloc memory allocation failed");
                // fprintf(stderr, "calloc memory allocation failed");
                exit(1);
            }
            if (0 != _createListenerSocket()) {
                DEBUG_LOG("failed to create listener socket");
                exit(1);
            }
        }

        ~PollManager() {
            for(int i = 0; i < pollfdArrSize; i++) {
                if(_isSocketOpen(pollfdArr[i].fd)) {
                    close(pollfdArr[i].fd);
                }
            }
            free(pollfdArr);
        }

        int pollSockets(int timeout_ms, const std::vector<struct pollfd>& readyFDsVec) {

            if (poll(pollfdArr, pollfdArrSize, timeout_ms) == -1) {
                DEBUG_LOG("poll failed");
                // exit(1);
                return -1;
            }

            if (pollfdArr[0].fd == listenerSocketFD && (pollfdArr[0].revents & POLLIN)) {
                // if listener is ready to read, it means a new client connection
                struct sockaddr_storage remoteAddr; // Client address
                socklen_t addrLen = sizeof(remoteAddr);
                int newSocketFD = accept(listenerSocketFD, static_cast<struct sockaddr*>(&remoteAddr), &addrLen);
                if (newSocketFD == -1) {
                    DEBUG_LOG("Error accepting client request");
                }
                else {
                    // add the new socket to polling array 
                    if (_addToPollfdArr(newSocketFD, POLLIN /*| POLLINOUT*/) != 0) {
                        DEBUG_LOG("failed to add newSocketFD to pollfdArr");
                        // return 1;
                    }

                    char remoteIP[INET6_ADDRSTRLEN];
                    if (NULL == inet_ntop(remoteAddr.ss_family, get_in_addr((struct sockaddr*)&remoteAddr), remoteIP, INET6_ADDRSTRLEN)) {
                        DEBUG_LOG("failed to convert address to human readable form");
                    }
                    std::stringstream strstream;
                    strstream << "pollserver: new connection from ";
                    strstream << remoteIP;
                    strstream << " on socket " << newSocketFD;
                    DEBUG_LOG(strstream.c_str());

                    // polling again to include newSocketFD
                    if (poll(pollfdArr, pollfdArrSize, timeout_ms) == -1) {
                        DEBUG_LOG("poll failed");
                        // exit(1);
                        return -1;
                    }
                }
            }
            
            for(int i = 1 /*skipping listenerSocketFD*/; i < pollfdArrSize; i++) {
                if (pollfdArr[i].revents & (POLLIN /*| POllOUT*/)) {
                    readyFDsVec.push_back(pollfdArr[i]);
                }
            }
            return 0;
        }

    private:
        // private member functions
        int _createListenerSocket() {
            struct addrinfo hints, *ai, *p;

            memset(&hints, 0, sizeof hints);
            hints.ai_family = socketDomain;  // IPv4 / IPv6
            hints.ai_socktype = socketType;  // TCP
            hints.ai_flags = AI_PASSIVE;  // set IP address

            if ((rv = getaddrinfo(NULL, listeningPortOrService.c_str(), &hints, &ai)) != 0) {
                const charBufSize = 256;
                char charBuf[charBufSize];
                snprintf(charBuf, charBufSize, "pollserver: %s\n", gai_strerror(rv));
                DEBUG_LOG(charBuf);
                // fprintf(stderr, "pollserver: %s\n", gai_strerror(rv));
                exit(1);
            }
            
            for(p = ai; p != NULL; p = p->ai_next) {

                listenerSocketFD = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (listenerSocketFD < 0) { 
                    continue;
                }

                if (isSocketNonBlocking) {
                    int flags = fcntl(listenerSocketFD, F_GETFL, 0);
                    if (flags < 0) {
                        fprinf(stderr, "fcntl(listenerSocketFD, F_GETFL, 0) failed");
                    }
                    if (fcntl(listenerSocketFD, F_SETFL, flags | O_NONBLOCK) < 0) {
                        DEBUG_LOG("fcntl(listenerSocketFD, F_SETFL, flags | O_NONBLOCK) failed");
                        // fprintf(stderr, "fcntl(listenerSocketFD, F_SETFL, flags | O_NONBLOCK) failed");
                    }
                }

                if (isReuseSocket) {
                    int reuse = static_cast<int>(isReuseSocket);
                    if (setsockopt(listenerSocketFD, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
                        DEBUG_LOG("setsockopt failed\n");
                        continue;
                }

                if (bind(listenerSocketFD, p->ai_addr, p->ai_addrlen) < 0) {
                    close(listenerSocketFD);
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

            if (listen(listenerSocketFD, socketBacklogCount) != 0) {
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
            if (result == -1 && errno = EBADF) {
                return false;  // socket is closed
            }
            return true;  // socket is open
        }

        int _addToPollfdArr(int newSocketFD, int events)
        {
            // If we don't have room, add more space in the pfds array
            if (pollfdArrCapacity == pollfdArrSize) {
                pollfdArrCapacity += 100;
                pollfdArr = static_cast<struct pollfd*>(realloc(pollfdArr, (sizeof(struct pollfd) * pollfdArrCapacity)));
                if (pollfdArr == NULL) {
                    DEBUG_LOG("encountered error while resizing pollfdArr");
                    // exit(-1);
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
            pollfdArr[index] = pollfdArr[pollfdArrSize-1];
            pollfdArrSize--;
            return 0;
        }

        // private member variables
        SocketSettings ss;  // settings for listener socket
        int listenerSocketFD;
        int pollfdArrCapacity;  // pollfdArr capacity (total space occupied in memory)
        int pollfdArrSize;  // current number of fds to track
        struct pollfd *pollfdArr;  // array of FDs to monitor using poll

        
    };
}

#endif  // SOCKETMANAGER_HPP

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
