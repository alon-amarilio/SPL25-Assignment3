#include "../include/StompProtocol.h"
#include "../include/event.h"
#include <iostream>
#include <sstream>

// Constructor: Initializer list order MUST match member declaration order in .h
StompProtocol::StompProtocol() :
    subId(0),
    receiptId(0),
    shouldTerminate(false),
    gamesToSubs(),
    pendingReceipts(),
    isConnected(false),
    mapMutex(),
    username(""),
    gameUpdates()
{
}

bool StompProtocol::shouldLogout() {
    return shouldTerminate;
}

bool StompProtocol::isUserConnected() {
    return isConnected;
}

void StompProtocol::setConnected(bool status) {
    isConnected = status;
}

// Helper function to split strings
std::vector<std::string> StompProtocol::split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

void StompProtocol::processInput(std::string line, ConnectionHandler& handler) {
    std::vector<std::string> tokens = split(line, ' ');
    if (tokens.empty()) return;

    std::string command = tokens[0];

    if (command == "login") {
        if (isConnected) {
            std::cout << "The client is already logged in, log out before trying again" << std::endl;
            return;
        }
        if (tokens.size() < 4) {
            std::cout << "Usage: login {host:port} {username} {password}" << std::endl;
            return;
        }
        username = tokens[2];
        std::string passcode = tokens[3];
        
        std::string frame = "CONNECT\n"
                            "accept-version:1.2\n"
                            "host:stomp.cs.bgu.ac.il\n"
                            "login:" + username + "\n"
                            "passcode:" + passcode + "\n"
                            "\n";
        handler.sendFrameAscii(frame, '\0');
    }
    else if (!isConnected) {
        std::cout << "Please login first" << std::endl;
        return;
    }
    else if (command == "join") {
        if (tokens.size() < 2) return;
        std::string gameName = tokens[1];
        
        int id = subId++;
        {
            std::lock_guard<std::mutex> lock(mapMutex);
            gamesToSubs[gameName] = id;
        }

        int receipt = receiptId++;
        {
             std::lock_guard<std::mutex> lock(mapMutex);
             pendingReceipts[receipt] = "Joined channel " + gameName;
        }

        std::string frame = "SUBSCRIBE\n"
                            "destination:" + gameName + "\n"
                            "id:" + std::to_string(id) + "\n"
                            "receipt:" + std::to_string(receipt) + "\n"
                            "\n";
        handler.sendFrameAscii(frame, '\0');
    }
    else if (command == "exit") {
        if (tokens.size() < 2) return;
        std::string gameName = tokens[1];
        
        int id = -1;
        {
            std::lock_guard<std::mutex> lock(mapMutex);
            if (gamesToSubs.count(gameName)) {
                id = gamesToSubs[gameName];
                gamesToSubs.erase(gameName); // Erase optimistically, or wait for receipt
            }
        }
        
        if (id == -1) return; // Not subscribed

        int receipt = receiptId++;
        {
             std::lock_guard<std::mutex> lock(mapMutex);
             pendingReceipts[receipt] = "Exited channel " + gameName;
        }

        std::string frame = "UNSUBSCRIBE\n"
                            "id:" + std::to_string(id) + "\n"
                            "receipt:" + std::to_string(receipt) + "\n"
                            "\n";
        handler.sendFrameAscii(frame, '\0');
    }
    else if (command == "logout") {
        int receipt = receiptId++;
        {
             std::lock_guard<std::mutex> lock(mapMutex);
             pendingReceipts[receipt] = "DISCONNECT";
        }
        
        std::string frame = "DISCONNECT\n"
                            "receipt:" + std::to_string(receipt) + "\n"
                            "\n";
        handler.sendFrameAscii(frame, '\0');
    }
    else if (command == "report") {
        if (tokens.size() < 2) return;
        std::string filename = tokens[1];
        names_and_events parsed = parseEventsFile(filename); // From event.h

        // Send events one by one
        for (const Event& event : parsed.events) {
             std::string gameName = parsed.team_a_name + "_" + parsed.team_b_name;
             // Here we usually should update our local gameUpdates map
             // And send the frame
             std::string body = "user:" + username + "\n" +
                                "team a:" + parsed.team_a_name + "\n" +
                                "team b:" + parsed.team_b_name + "\n" +
                                "event name:" + event.get_name() + "\n" +
                                "time:" + std::to_string(event.get_time()) + "\n" +
                                "general game updates:\n";
                                
             for (auto const& [key, val] : event.get_game_updates()) {
                 body += key + ":" + val + "\n";
             }
             body += "team a updates:\n";
             for (auto const& [key, val] : event.get_team_a_updates()) {
                 body += key + ":" + val + "\n";
             }
             body += "team b updates:\n";
             for (auto const& [key, val] : event.get_team_b_updates()) {
                 body += key + ":" + val + "\n";
             }
             body += "description:\n" + event.get_discription();

             std::string frame = "SEND\n"
                                 "destination:" + gameName + "\n"
                                 "\n" + 
                                 body + "\n";
             
             handler.sendFrameAscii(frame, '\0');
        }
    }
    else if (command == "summary") {
         // Implement summary logic if needed
         std::cout << "Summary command logic goes here" << std::endl;
    }
}

bool StompProtocol::processServerResponse(std::string frame) {
    std::vector<std::string> lines = split(frame, '\n');
    if (lines.empty()) return true;

    std::string command = lines[0];

    if (command == "CONNECTED") {
        isConnected = true;
        std::cout << "Login successful" << std::endl;
    }
    else if (command == "ERROR") {
        std::cout << frame << std::endl; // Print full error
        shouldTerminate = true;
        isConnected = false;
        return false;
    }
    else if (command == "MESSAGE") {
        std::string dest = "";
        std::string body = "";
        bool bodyStarted = false;
        
        for (size_t i = 1; i < lines.size(); i++) {
            if (!bodyStarted && lines[i].find("destination:") == 0) {
                dest = lines[i].substr(12);
            }
            else if (!bodyStarted && lines[i].empty()) {
                bodyStarted = true;
            }
            else if (bodyStarted) {
                body += lines[i] + "\n";
            }
        }
        std::cout << "Displaying update from: " << dest << "\n" << body << std::endl;
        // Here you would parse the body back to Event object and update gameUpdates map
    }
    else if (command == "RECEIPT") {
        std::string receiptIdStr = "";
        for (size_t i = 1; i < lines.size(); i++) {
             if (lines[i].find("receipt-id:") == 0) {
                 receiptIdStr = lines[i].substr(11);
                 break;
             }
        }
        
        if (!receiptIdStr.empty()) {
            int rId = std::stoi(receiptIdStr);
            std::lock_guard<std::mutex> lock(mapMutex);
            if (pendingReceipts.count(rId)) {
                std::string action = pendingReceipts[rId];
                if (action == "DISCONNECT") {
                    shouldTerminate = true;
                    isConnected = false;
                    return false;
                }
                std::cout << action << std::endl;
                pendingReceipts.erase(rId);
            }
        }
    }
    return true;
}
