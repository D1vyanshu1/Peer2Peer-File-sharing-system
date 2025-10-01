#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>

using namespace std;

struct PeerConfig {
    string ip;
    int sync_port;
};

unordered_map<string, string> dataStore; // key-value store
mutex dataMutex;

vector<PeerConfig> peers;

//------------------ Read info.txt ------------------
void readPeers(const string &filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to open " << filename << endl;
        return;
    }
    string ip;
    int port;
    while (file >> ip >> port) {
        peers.push_back({ip, port});
    }
    file.close();
}

//------------------ Sync to other trackers ------------------
void syncToPeers(const string &key, const string &value) {
    string message = key + "=" + value;
    for (auto &peer : peers) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(peer.sync_port);
        inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            write(sock, message.c_str(), message.size());
            cout << "[SyncClient] Sent to " << peer.ip << ":" << peer.sync_port << " -> " << message << endl;
        }
        close(sock);
    }
}

//------------------ Fetch all data from peers ------------------
string fetchAllFromPeers() {
    string combinedData;
    for (auto &peer : peers) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(peer.sync_port);
        inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            string req = "REQUEST_ALL";
            write(sock, req.c_str(), req.size());

            char buffer[4096];
            int n = read(sock, buffer, sizeof(buffer)-1);
            if (n > 0) {
                buffer[n] = 0;
                combinedData += string(buffer);
            }
        }
        close(sock);
    }
    return combinedData;
}

//------------------ Client Listener ------------------
void clientListener(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return; }
    if (listen(server_fd, 5) < 0) { perror("listen"); return; }

    cout << "[ClientListener] Listening on port " << port << endl;

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(server_fd, (struct sockaddr*)&clientAddr, &len);
        if (clientSock < 0) { perror("accept"); continue; }

        thread([clientSock]() {
            char buffer[4096];
            int n = read(clientSock, buffer, sizeof(buffer)-1);
            if (n > 0) {
                buffer[n] = 0;
                string msg(buffer);

                if (msg == "GET_ALL") {
                    string allData;
                    {
                        lock_guard<mutex> lock(dataMutex);
                        for (auto &[k,v] : dataStore)
                            allData += k + "=" + v + "\n";
                    }
                    allData += fetchAllFromPeers();
                    write(clientSock, allData.c_str(), allData.size());
                } else {
                    // Support key=value or key:value
                    size_t pos = msg.find('=');
                    if (pos == string::npos) pos = msg.find(':');
                    if (pos != string::npos) {
                        string key = msg.substr(0,pos);
                        string value = msg.substr(pos+1);
                        {
                            lock_guard<mutex> lock(dataMutex);
                            dataStore[key] = value;
                        }
                        cout << "[ClientListener] Stored: " << key << "=" << value << endl;
                        syncToPeers(key, value);
                    }
                }
            }
            close(clientSock);
        }).detach();
    }
}

//------------------ Sync Listener ------------------
void syncListener(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return; }
    if (listen(server_fd, 5) < 0) { perror("listen"); return; }

    cout << "[SyncListener] Listening on port " << port << endl;

    while (true) {
        sockaddr_in peerAddr{};
        socklen_t len = sizeof(peerAddr);
        int sock = accept(server_fd, (struct sockaddr*)&peerAddr, &len);
        if (sock < 0) { perror("accept"); continue; }

        thread([sock]() {
            char buffer[4096];
            int n = read(sock, buffer, sizeof(buffer)-1);
            if (n > 0) {
                buffer[n] = 0;
                string msg(buffer);

                if (msg == "REQUEST_ALL") {
                    string allData;
                    {
                        lock_guard<mutex> lock(dataMutex);
                        for (auto &[k,v] : dataStore)
                            allData += k + "=" + v + "\n";
                    }
                    write(sock, allData.c_str(), allData.size());
                } else {
                    // Normal sync update
                    size_t pos = msg.find('=');
                    if (pos == string::npos) pos = msg.find(':');
                    if (pos != string::npos) {
                        string key = msg.substr(0,pos);
                        string value = msg.substr(pos+1);
                        {
                            lock_guard<mutex> lock(dataMutex);
                            dataStore[key] = value;
                        }
                        cout << "[SyncListener] Synced: " << key << "=" << value << endl;
                    }
                }
            }
            close(sock);
        }).detach();
    }
}

//------------------ Main ------------------
int main(int argc, char* argv[]) {
    if (argc < 5) {
        cerr << "Usage: ./tracker info.txt client_port sync_port tracker_name\n";
        return 1;
    }

    string filename = argv[1];
    int client_port = stoi(argv[2]);
    int sync_port = stoi(argv[3]);
    string tracker_name = argv[4];

    readPeers(filename);

    thread clientThread(clientListener, client_port);
    thread syncThread(syncListener, sync_port);

    clientThread.join();
    syncThread.join();

    return 0;
}