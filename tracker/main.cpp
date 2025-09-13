#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include "headher.h"

using namespace std;

void handle_client(int client_sock, string client_ip, int client_port) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = read(client_sock, buffer, sizeof(buffer));
        if (bytes_received <= 0) {
            cout << "[*] Client disconnected: " << client_ip << ":" << client_port << endl;
            close(client_sock);
            break;
        }
        cout << "[Message from " << client_ip << ":" << client_port << "] " << buffer << endl;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./tracker <info.txt> tacker_number" << endl;
            return 1;
        }

    const string filepath = argv[1];
    int tracker_number = stoi(argv[2]);

    pair<string, int> ipPort = read_info(filepath, tracker_number);
    string ip = ipPort.first; int Port = ipPort.second;
    cout << "[*] Tracker starting at " << ip << ":" << Port << endl;

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    // set up before bind, we set IP type (IPV4 in our case), port number and ip address
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(Port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    // Bind socket to ip:port
    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }

    // Listen
    if (listen(server_fd, 10) < 0) {   // kernel will queue maximum 10 connections we call accept()
        perror("listen");
        return 1;
    }
    cout << "[*] Tracker listening..." << endl;

    // Accept clients
    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_fd, (sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        string client_ip = inet_ntoa(client_addr.sin_addr);
        int client_port = ntohs(client_addr.sin_port);
        cout << "[*] New connection from " << client_ip << ":" << client_port << endl;

        thread t(handle_client, client_sock, client_ip, client_port);
        t.detach(); // detach thread to handle client independently
    }

    close(server_fd);
    return 0;
}
