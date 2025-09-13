#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <cerrno>
#include "headher.h"  // contains read_info()

using namespace std;

int connect_to_tracker(const string &ip, int port) {

    // first create a socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

// use struct to store ip addr, port and ip type
    sockaddr_in tracker_addr;
    memset(&tracker_addr, 0, sizeof(tracker_addr));
    tracker_addr.sin_family = AF_INET;
    tracker_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &tracker_addr.sin_addr) <= 0) {
        cerr << "Invalid IP address: " << ip << endl;
        close(sock);
        return -1;
    }

    if (connect(sock, (sockaddr*)&tracker_addr, sizeof(tracker_addr)) < 0) {
        close(sock);
        return -1;  // fail
    }

    return sock; // success
}


int connect_loop(const string &filepath) {
    int sock = -1;
    while (true) {
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
        if (sock >= 0) {
            cout << "[*] Connected to Tracker 2 at " << ip2 << ":" << port2 << endl;
            return sock;
        }

        cout << "[!] Both trackers unavailable, retrying in 2 seconds..." << endl;
        sleep(2);
    }
}