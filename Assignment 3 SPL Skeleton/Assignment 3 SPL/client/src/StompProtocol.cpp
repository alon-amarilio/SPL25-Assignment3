#include "../include/StompProtocol.h"
#include <fstream>


    StompProtocol::StompProtocol() : 
        subId(0), 
        receiptId(0), 
        isConnected(false),
        shouldTerminate(false),
        username("")
    {}

    bool StompProtocol::shouldLogout() {
        return shouldTerminate;
    }

    bool StompProtocol::isUserConnected() {
        return isConnected;
    }

    void StompProtocol::setConnected(bool status) {
        isConnected = status;
    }   

    std::vector<std::string> split(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);

        
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }

        return tokens;
    }

    void StompProtocol::processInput(std::string line, ConnectionHandler& handler){
        std::vector<std::string> tokens = split(line, ' ');
        if (tokens.empty()) return;

        std::string command = tokens[0];

        
        if(command == "login"){
            if (isConnected) {
                std::cout << "The client is already logged in, log out before trying again" << std::endl;
                return;
            }
            if (tokens.size() < 4) {
                std::cout << "Error: Invalid login arguments" << std::endl;
                return;
            }   
            
            username = tokens[2];
            std::string passcode = tokens[3];

            std::string frame = "CONNECT\n";
            frame += "accept-version:1.2\n";
            frame += "host:stomp.cs.bgu.ac.il\n";
            frame += "login:" + username + "\n";
            frame += "passcode:" + passcode + "\n";
            frame += "\n";

            handler.sendFrameAscii(frame, '\0');
        }

        else if(command == "join"){
                if (tokens.size() < 2) {
                    std::cout << "Error: Missing game name" << std::endl;
                return;
            }
            std::string gameName = tokens[1];

            std::lock_guard<std::mutex> lock(mapMutex);

            gamesToSubs[gameName] = subId;

            std::string frame = "SUBSCRIBE\n";
            frame += "destination:/" + gameName + "\n";
            frame += "id:" + std::to_string(subId) + "\n";
            frame += "receipt:" + std::to_string(receiptId) + "\n";
            frame += "\n"; 

            pendingReceipts[receiptId] = "Joined channel " + gameName;

            handler.sendFrameAscii(frame, '\0');
            subId++;
            receiptId++;
        }


        else if(command == "exit"){
            if (tokens.size() < 2) {
                std::cout << "Error: Missing game name" << std::endl;
                return;
            }
            std::string gameName = tokens[1];

            std::lock_guard<std::mutex> lock(mapMutex);
            if (gamesToSubs.find(gameName) != gamesToSubs.end()) {
                int id = gamesToSubs[gameName];
                pendingReceipts[receiptId] = "Exited channel " + gameName;

                std::string frame = "UNSUBSCRIBE\n";
                frame += "id:" + std::to_string(id) + "\n";
                frame += "receipt:" + std::to_string(receiptId) + "\n";
                frame += "\n";

                handler.sendFrameAscii(frame, '\0');
                gamesToSubs.erase(gameName);
                receiptId++;
            }    
            else {
                std::cout << "Error: You are not subscribed to " << gameName << std::endl;
            }
        }

        else if(command == "report"){
            if (tokens.size() < 2) {
                std::cout << "Error: Missing file" << std::endl;
                return;
            }

            
            
            names_and_events namesAndEvents = parseEventsFile(line.substr(line.find(' ') + 1));
            std::string teamAName = namesAndEvents.team_a_name;
            std::string teamBName = namesAndEvents.team_b_name;
            std::vector<Event> events = namesAndEvents.events;
            std::string gameName = teamAName + "_" + teamBName;

            std::lock_guard<std::mutex> lock(mapMutex);
            if (gamesToSubs.find(gameName) == gamesToSubs.end()) {
                std::cout << "Error: You are not subscribed to " << gameName << std::endl;
                return;
            }

            for (const Event & event : namesAndEvents.events){
                std::string frame = "SEND\n";
                frame += "destination:/" + teamAName + "_" + teamBName;
                frame += "\n";
                frame += "user: " + username + "\n";
                frame += "team a: " + teamAName + "\n";
                frame += "team b: " + teamBName + "\n";
                frame += "event name: " + event.get_name() + "\n";
                frame += "time: " + std::to_string(event.get_time()) + "\n";
                frame += "general game updates:\n";

                for (const auto & pair : event.get_game_updates()) {
                    frame += pair.first + ": " + pair.second + "\n";
                }

                frame += "team a updates:\n";
                for (const auto & pair : event.get_team_a_updates()) {
                    frame += pair.first + ": " + pair.second + "\n";
                }

                frame += "team b updates:\n";
                for (const auto & pair : event.get_team_b_updates()) {
                    frame += pair.first + ": " + pair.second + "\n";
                }

                frame += "description: " + event.get_discription() + "\n";

                handler.sendFrameAscii(frame, '\0');

                {
                    std::lock_guard<std::mutex> lock(mapMutex);
                    gameUpdates[gameName][username].push_back(event);
                }
            }
        }

        else if(command == "summary"){
            if (tokens.size() < 4) {
                std::cout << "Error: Missing arguments for summary" << std::endl;
                return;
            }

            std::string game_name = tokens[1];
            std::string user_name = tokens[2];
            std::string file_path = tokens[3];

            std::lock_guard<std::mutex> lock(mapMutex);
            if (gameUpdates.find(game_name) == gameUpdates.end() || 
                gameUpdates[game_name].find(user_name) == gameUpdates[game_name].end()) {
                std::cout << "Error: No updates found for " << user_name << " in game " << game_name << std::endl;
                return;
            }

            const std::vector<Event>& events = gameUpdates[game_name][user_name];
            
            if (events.empty()) return;

            std::map<std::string, std::string> general_stats;
            std::map<std::string, std::string> team_a_stats;
            std::map<std::string, std::string> team_b_stats;

            for (const auto& event : events) {
                for (const auto& pair : event.get_game_updates()) {
                    general_stats[pair.first] = pair.second;
                }
                for (const auto& pair : event.get_team_a_updates()) {
                    team_a_stats[pair.first] = pair.second;
                }
                for (const auto& pair : event.get_team_b_updates()) {
                    team_b_stats[pair.first] = pair.second;
                }
            }

            std::string team_a_name = game_name.substr(0, game_name.find('_'));
            std::string team_b_name = game_name.substr(game_name.find('_') + 1);

            std::ofstream outFile(file_path);
            if (!outFile.is_open()) {
                std::cout << "Error: Could not open file " << file_path << std::endl;
                return;
            }

            outFile << team_a_name << " vs " << team_b_name << "\n";
            outFile << "Game stats:\n";

            outFile << "General stats:\n";
            for (const auto& pair : general_stats) {
                outFile << pair.first << ": " << pair.second << "\n";
            }

            outFile << team_a_name << " stats:\n";
            for (const auto& pair : team_a_stats) {
                outFile << pair.first << ": " << pair.second << "\n";
            }

            outFile << team_b_name << " stats:\n";
            for (const auto& pair : team_b_stats) {
                outFile << pair.first << ": " << pair.second << "\n";
            }

            outFile << "Game event reports:\n";
            for (const auto& event : events) {
                outFile << event.get_time() << " - " << event.get_name() << ":\n\n";
                outFile << event.get_discription() << "\n\n";
            }

            outFile.close();
            std::cout << "Summary created in " << file_path << std::endl;
        }

        else if(command == "logout"){
            if (!isConnected) {
                std::cout << "The client is not logged in." << std::endl;
                return;
            }  
            
            std::lock_guard<std::mutex> lock(mapMutex);
            
            pendingReceipts[receiptId] = "DISCONNECT_ACTION";

            std::string frame = "DISCONNECT\n";
            frame += "receipt:" + std::to_string(receiptId) + "\n";
            frame += "\n";

            handler.sendFrameAscii(frame, '\0');
            
            receiptId++;
        }
    }

    bool StompProtocol::processServerResponse(std::string frame) {
        std::stringstream ss(frame);
        std::string command;
        std::getline(ss, command);

        if (!command.empty() && command.back() == '\r') command.pop_back();

        if (command == "CONNECTED") {
            isConnected = true;
            std::cout << "Login successful" << std::endl;
            return true;
        }

        else if (command == "ERROR") {
            std::string line, errorMessage, messageHeader;
            bool inBody = false;

            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) { inBody = true; continue; }
                if (!inBody) {
                    if (line.find("message:") == 0) messageHeader = line.substr(8);
                } else {
                    errorMessage += line + "\n";
                }
            }

            if (!messageHeader.empty()) std::cout << messageHeader << std::endl;
            else if (!errorMessage.empty()) std::cout << errorMessage << std::endl;
            else std::cout << frame << std::endl;

            shouldTerminate = true;
            isConnected = false;
            return false;
        }

        else if (command == "MESSAGE") {
            std::string gameName, line, body, sendingUser, event_name, description;
            int time = 0;
            bool inBody = false;

            while (std::getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) { inBody = true; continue; }
                if (!inBody) {
                    if (line.find("destination:") == 0) {
                        std::string dest = line.substr(12);
                        size_t slashPos = dest.rfind('/');
                        gameName = (slashPos != std::string::npos) ? dest.substr(slashPos + 1) : dest;
                    }
                } else {
                    body += line + "\n";
                }
            }

            std::cout << "Received Message:\n" << frame << std::endl;

            if (!gameName.empty()) {
                std::stringstream bodyStream(body);
                std::string bodyLine;

                while(std::getline(bodyStream, bodyLine)) {
                    if (!bodyLine.empty() && bodyLine.back() == '\r') bodyLine.pop_back();
                    
                    if (bodyLine.find("user:") == 0) sendingUser = bodyLine.substr(5);
                    else if (bodyLine.find("event name:") == 0) event_name = bodyLine.substr(11);
                    else if (bodyLine.find("time:") == 0) {
                        try { time = std::stoi(bodyLine.substr(5)); } catch(...) {}
                    }
                    else if (bodyLine.find("description:") == 0) description = bodyLine.substr(12);
                }

                if (!sendingUser.empty()) {
                    std::string team_a = gameName.substr(0, gameName.find('_'));
                    std::string team_b = gameName.substr(gameName.find('_') + 1);
                    
                    std::map<std::string, std::string> emptyMap;

                    Event parsedEvent(team_a, team_b, event_name, time, emptyMap, emptyMap, emptyMap, description);
                    
                    std::lock_guard<std::mutex> lock(mapMutex);
                    gameUpdates[gameName][sendingUser].push_back(parsedEvent);
                }
            }
            return true;
        }

        else if (command == "RECEIPT") {
            std::string line, receiptIdStr;
            while (std::getline(ss, line) && !line.empty()) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find("receipt-id:") == 0) receiptIdStr = line.substr(11);
            }

            if (!receiptIdStr.empty()) {
                int rId = std::stoi(receiptIdStr);
                std::lock_guard<std::mutex> lock(mapMutex);

                if (pendingReceipts.count(rId)) {
                    std::string msg = pendingReceipts[rId];
                    if (msg == "DISCONNECT_ACTION") {
                        shouldTerminate = true;
                        isConnected = false;
                        return false;
                    } else {
                        std::cout << msg << std::endl;
                    }
                    pendingReceipts.erase(rId);
                }
            }
            return true;
        }

        return true;
    }