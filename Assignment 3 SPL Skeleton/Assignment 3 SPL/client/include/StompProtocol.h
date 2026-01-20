#pragma once

#include "../include/ConnectionHandler.h"
#include "../include/event.h"
#include <string>
#include <vector>
#include <map>

// TODO: implement the STOMP protocol
class StompProtocol
{
    
private:

    int subId;
    int receiptId;
    bool shouldTerminate;
    std::map<std::string, int> gamesToSubs;
    std::map<int, std::string> pendingReceipts;
    bool isConnected;
    std::mutex mapMutex;
    std::string username;
    std::map<std::string, std::map<std::string, std::vector<Event>>> gameUpdates;

    std::vector<std::string> split(const std::string& s, char delimiter);

public:
    StompProtocol::StompProtocol();
    
    bool StompProtocol::shouldLogout();
    bool StompProtocol::isUserConnected();
    void StompProtocol::setConnected(bool status);
    void processInput(std::string line, ConnectionHandler& handler);
    bool processServerResponse(std::string frame);
};
