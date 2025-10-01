#include "theadher.h"

int peer_sock = -1;

void init_peer_connection(const string filepath, int peer_index) {
    if (peer_sock != -1) return; // already connected

    pair<string, int> ipPort = read_info(filepath, peer_index);
    string ip = ipPort.first; int Port = ipPort.second;

    peer_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (peer_sock < 0) {
        perror("socket");
        peer_sock = -1;
        return;
    }

    sockaddr_in peer_addr{};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port   = htons(Port);

    if (inet_pton(AF_INET, ip.c_str(), &peer_addr.sin_addr) <= 0) {
        cerr << "Invalid peer IP" << endl;
        close(peer_sock);
        peer_sock = -1;
        return;
    }

    if (connect(peer_sock, (sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
        perror("connect");
        close(peer_sock);
        peer_sock = -1;
        return;
    }

    cout << "[*] Connected to peer tracker at " << ip << ":" << Port << endl;

    // ---- send handshake
    string hello = "TRACKER_HELLO";
    send(peer_sock, hello.c_str(), hello.size(), 0);

    // ---- start reader thread
    thread t([&]() {
        char buffer[1024];
        while (true) {
            memset(buffer, 0, sizeof(buffer));
            int bytes_received = read(peer_sock, buffer, sizeof(buffer));
            if (bytes_received <= 0) {
                cout << "[*] Peer disconnected" << endl;
                close(peer_sock);
                peer_sock = -1;
                break;
            }
            callCommandsTracker(peer_sock, buffer);
        }
    });
    t.detach();
}

void send_to_peer(const string &msg) {
    if (peer_sock == -1) return;
    if (send(peer_sock, msg.c_str(), msg.size(), MSG_NOSIGNAL) < 0) {
        perror("send to peer");
        close(peer_sock);
        peer_sock = -1;
    }
}
