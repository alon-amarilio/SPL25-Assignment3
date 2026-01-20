#include <stdlib.h>
#include "../include/ConnectionHandler.h"
#include "../include/StompProtocol.h"
#include <thread>
#include <vector>


int main(int argc, char *argv[]) {
	// TODO: implement the STOMP client
	while (true) {
        const short bufsize = 1024;
        char buf[bufsize];
        std::cin.getline(buf, bufsize);
        std::string line(buf);
        
        std::string cmd;
        size_t spacePos = line.find(' ');
        if (spacePos != std::string::npos) cmd = line.substr(0, spacePos);
        else cmd = line;

        if (cmd == "login") {
            StompProtocol tempProtocol;
            std::vector<std::string> tokens;
            std::string token;
            std::istringstream tokenStream(line);
            while (std::getline(tokenStream, token, ' ')) {
                tokens.push_back(token);
            }

            if (tokens.size() < 4) {
                std::cout << "Error: Invalid login arguments" << std::endl;
                continue;
            }

            std::string hostPort = tokens[1];
            std::string host = hostPort.substr(0, hostPort.find(':'));
            short port = std::stoi(hostPort.substr(hostPort.find(':') + 1));

            ConnectionHandler* handler = new ConnectionHandler(host, port);
            if (!handler->connect()) {
                std::cout << "Could not connect to server" << std::endl;
                delete handler;
                continue;
            }

            StompProtocol protocol;
            
            protocol.processInput(line, *handler);

            std::thread th([&handler, &protocol]() {
                while (!protocol.shouldLogout()) {
                    std::string answer;
                    
                    if (!handler->getLine(answer)) {
                        std::cout << "Disconnected from server." << std::endl;
                        break;
                    }

                    if (answer.length() > 0 && answer[answer.length() - 1] == '\n') {
                        answer.resize(answer.length() - 1);
                    }

                    if (!protocol.processServerResponse(answer)) {
                        break; 
                    }
                }
            });

            while (!protocol.shouldLogout()) {
                char buf2[bufsize];
                std::cin.getline(buf2, bufsize);
                std::string input(buf2);
                
                protocol.processInput(input, *handler);
            }

            th.join();
            delete handler;
            
            std::cout << "Client disconnected. Ready to login again." << std::endl;
        } 
        else {
            std::cout << "Error: Please login first" << std::endl;
        }
    }
    return 0;

}