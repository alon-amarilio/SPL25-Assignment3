#include "../include/StompProtocol.h"
#include "../include/event.h"
#include <iostream>
#include <sstream>
#include <fstream> 
#include <algorithm> 

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
        if (tokens.size() < 4) {
            std::cout << "Usage: summary {gameName} {user} {file}" << std::endl;
            return;
        }
        std::string gameName = tokens[1];
        std::string user = tokens[2];
        std::string fileName = tokens[3];

        std::lock_guard<std::mutex> lock(mapMutex);
        
        if (gameUpdates.count(gameName) && gameUpdates[gameName].count(user)) {
            std::ofstream outFile(fileName); 
            
            if (outFile.is_open()) {
                Event& firstEvent = gameUpdates[gameName][user][0];
                outFile << firstEvent.get_team_a_name() << " vs " << firstEvent.get_team_b_name() << "\n";
                outFile << "Game event reports:\n";
                
                for (const Event& e : gameUpdates[gameName][user]) {
                    outFile << e.get_time() << " - " << e.get_name() << ":\n\n";
                    outFile << e.get_discription() << "\n\n"; 
                }
                
                outFile.close();
                std::cout << "Summary created in " << fileName << std::endl;
            } else {
                std::cout << "Error: Could not open file " << fileName << std::endl;
            }
        } else {
            std::cout << "No reports found for " << user << " in game " << gameName << std::endl;
        }
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
        std::cout << frame << std::endl; 
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
                if (!dest.empty() && dest.back() == '\r') dest.pop_back();
            }
            else if (!bodyStarted && lines[i].empty()) {
                bodyStarted = true;
            }
            else if (bodyStarted) {
                body += lines[i] + "\n";
            }
        }

        std::string sender = "", team_a = "", team_b = "", event_name = "";
        int event_time = 0;
        std::vector<std::string> bodyLines = split(body, '\n');
        
        for (std::string& line : bodyLines) {
            if (!line.empty() && line.back() == '\r') line.pop_back(); 
            
            if (line.find("user:") == 0) sender = line.substr(5);
            else if (line.find("team a:") == 0) team_a = line.substr(7);
            else if (line.find("team b:") == 0) team_b = line.substr(7);
            else if (line.find("event name:") == 0) event_name = line.substr(11);
            else if (line.find("time:") == 0) event_time = std::stoi(line.substr(5));
        }

        if (!sender.empty()) {
            std::lock_guard<std::mutex> lock(mapMutex);
            std::map<std::string, std::string> empty_map;
            Event newEvent(team_a, team_b, event_name, event_time, empty_map, empty_map, empty_map, body);
            gameUpdates[dest][sender].push_back(newEvent);
        }
        std::cout << "Displaying update from: " << dest << "\n" << body << std::endl;
    }
    else if (command == "RECEIPT") {
        std::string receiptId = "";
        if (lines.size() > 1) {
                size_t colonPos = lines[1].find(':');
                if (colonPos != std::string::npos) {
                    receiptId = lines[1].substr(colonPos + 1); 
                }
            }
        
            if (!receiptId.empty()) {
                try {
                    int rId = std::stoi(receiptId);
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
                catch (const std::exception& e){}
            }
    }
    return true;
}