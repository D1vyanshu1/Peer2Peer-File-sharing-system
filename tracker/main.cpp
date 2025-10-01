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
#include <unordered_map>
#include "theadher.h"

using namespace std;

extern int peer_sock; // declare the global

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

        callCommandsTracker(client_sock, buffer);
    }
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./tracker <info.txt> tacker_number" << endl;
            return 1;
        }

    const string filepath = argv[1];
    int Port_no = stoi(argv[2]);

    pair<string, int> ipPort = read_info(filepath, Port_no);
    string ip = ipPort.first; int Port = ipPort.second;

    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        return 1;
    }

    // set up before bind, we set IP type (IPV4 in our case), port number and ip address
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(Port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr); // Converts human-readable IP to binary (inet_pton).

    // Bind socket to ip:port
    if (bind(server_socket, (sockaddr *)&server_addr, sizeof(server_addr)) < 0) {  // bind() attaches the socket to ip:port.
        perror("bind");
        return 1;
    }
    else cout << "[*] Tracker started at " << ip << ":" << Port << endl;

    // first tracker to start won't connect, second one will establish connection
    int peer_index = (Port_no == 1) ? 2 : 1;
    init_peer_connection(filepath, peer_index);

    // Listen
    if (listen(server_socket, 10) < 0) {   // kernel will queue maximum 10 connections we call accept()
        perror("listen");
        return 1;
    }
    cout << "[*] Tracker listening..." << endl;

    // Accept clients
   while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_socket, (sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        string client_ip = inet_ntoa(client_addr.sin_addr);
        int client_port = ntohs(client_addr.sin_port);

        // --- check first message for handshake
        char handshake[64];
        memset(handshake, 0, sizeof(handshake));
        int n = recv(client_sock, handshake, sizeof(handshake), MSG_PEEK); 
        // MSG_PEEK lets us look at the data without removing it from the socket buffer

        if (n > 0 && string(handshake).find("TRACKER_HELLO") == 0) {
            cout << "[*] Peer tracker connected from " << client_ip << ":" << client_port << endl;

            peer_sock = client_sock; // save this connection as peer

            // remove the handshake from the buffer
            recv(client_sock, handshake, sizeof(handshake), 0);

            // start a peer reader thread
            thread t([=]() {
                char buffer[1024];
                while (true) {
                    memset(buffer, 0, sizeof(buffer));
                    int bytes_received = read(client_sock, buffer, sizeof(buffer));
                    if (bytes_received <= 0) {
                        cout << "[*] Peer disconnected" << endl;
                        close(peer_sock);
                        peer_sock = -1;
                        break;
                    }
                    callCommandsTracker(client_sock, buffer);
                }
            });
            t.detach();
        } else {
            cout << "[*] New client connection from " << client_ip << ":" << client_port << endl;
            thread t(handle_client, client_sock, client_ip, client_port);
            t.detach();
        }
    }

    close(server_socket);
    return 0;
}

// tracker listens on socket with fd 1 suppose, but when it connects to any client with help of
// accept, then it establishes that connection through a new socket and not with fd 1
// that socket with fd 1 is still used to keep listening for any new connection requests

// working while loop:
// while (true) { sockaddr_in client_addr; socklen_t client_len = sizeof(client_addr); int client_sock = accept(server_socket, (sockaddr *)&client_addr, &client_len); if (client_sock < 0) { perror("accept"); continue; } string client_ip = inet_ntoa(client_addr.sin_addr); int client_port = ntohs(client_addr.sin_port); cout << "[*] New connection from " << client_ip << ":" << client_port << endl; thread t(handle_client, client_sock, client_ip, client_port); t.detach(); // detach thread to handle client independently }