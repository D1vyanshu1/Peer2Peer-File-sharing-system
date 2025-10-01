#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <cerrno>
#include "cheadher.h"  // contains read_info()

using namespace std;

int connect_to_tracker(const string &ip, int tracker_port) {

    // first create a socket
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(client_sock < 0) {
        perror("socket");
        return -1;
    }

// use struct to store ip addr, port and ip type
    sockaddr_in tracker_addr, client_addr;

    // // Bind socket to given client ip:port
    // memset(&client_addr, 0, sizeof(client_addr));
    // client_addr.sin_family = AF_INET;
    // client_addr.sin_port = htons(client_port);

    // if (inet_pton(AF_INET, client_ip.c_str(), &client_addr.sin_addr) <= 0) {
    //     cerr << "Invalid client IP address\n";
    //     close(client_sock);
    //     return -1;
    // }

    // if (bind(client_sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
    //     perror("Client bind failed");
    //     close(client_sock);
    //     return -1;
    // }

    memset(&tracker_addr, 0, sizeof(tracker_addr));
    tracker_addr.sin_family = AF_INET;
    tracker_addr.sin_port = htons(tracker_port);

    if(inet_pton(AF_INET, ip.c_str(), &tracker_addr.sin_addr) <= 0) {
        cerr << "Invalid IP address: " << ip << endl;
        close(client_sock);
        return -1;
    }
    
    // trying to establish tcp connection, if fail then return error after closing socket of client we opened
    if(connect(client_sock, (sockaddr*)&tracker_addr, sizeof(tracker_addr)) < 0) {
        close(client_sock);
        return -1;  // fail
    }

    return client_sock; // success
}


int connect_loop(const string &filepath) {
    int sock = -1;
    while(true){
        // Try Tracker 1
        auto [ip1, port1] = read_info(filepath, 1);
        sock = connect_to_tracker(ip1, port1);
        if (sock >= 0) {
            cout << "[*] Connected to Tracker 1 at " << ip1 << ":" << port1 << endl;
            return sock;
        }

        // Try Tracker 2
        auto [ip2, port2] = read_info(filepath, 2);
        sock = connect_to_tracker(ip2, port2);
        if(sock >= 0) {
            cout << "[*] Connected to Tracker 2 at " << ip2 << ":" << port2 << endl;
            return sock;
        }

        cout << "[!] Both trackers unavailable, retrying again in 10 seconds..." << endl;
        sleep(10);
    }
}