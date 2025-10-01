#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <cerrno>
#include <thread>
#include "cheadher.h"  // contains read_info()

using namespace std;

unordered_map<string, string> files;

void handle_peer(int peer_sock, string peer_ip, int peer_port) {
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    int n = recv(peer_sock, buffer, sizeof(buffer)-1, 0);
    if (n <= 0) {
        cerr << "[!] Empty/failed request from: " << peer_ip << ":" << peer_port << endl;
        close(peer_sock);
        return;
    }

    string request(buffer);
    istringstream iss(request);
    string cmd, filename, chunk_number;
    iss >> cmd >> filename >> chunk_number;
    int chunk_no = stoi(chunk_number);


    if (cmd == "get_chunk") {
        if (files.find(filename) == files.end()) {
            cerr << "[!] File not found locally: " << filename << endl;
        } else {
            string filepath = files[filename];
            int fd = open(filepath.c_str(), O_RDONLY);
            if (fd < 0) { perror("open"); close(peer_sock); return; }

            int chunk_size = 512 * 1024;
            off_t offset = (off_t)chunk_no * chunk_size;
            lseek(fd, offset, SEEK_SET);

            vector<char> chunk(chunk_size);
            int bytes = read(fd, chunk.data(), chunk_size);
            close(fd);

            send(peer_sock, chunk.data(), bytes, 0);
        }
    }

    close(peer_sock);
}


void start_peer_server(string ip, int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) { perror("socket"); exit(1); }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (bind(server_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_sock, 10) < 0) {
        perror("listen");
        exit(1);
    }
    cout << "[*] Peer listening at " << ip << ":" << port << endl;

    while (true) {
        sockaddr_in peer_addr;
        socklen_t len = sizeof(peer_addr);
        int peer_sock = accept(server_sock, (sockaddr*)&peer_addr, &len);
        if (peer_sock < 0){ 
            perror("accept"); 
            continue; 
        }

        string peer_ip = inet_ntoa(peer_addr.sin_addr);
        int peer_port = ntohs(peer_addr.sin_port);

        // if any client requests then connect to it on seperate thread
        thread t(handle_peer, peer_sock, peer_ip, peer_port);
        t.detach();
    }
}


int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN); // prevent client from dying if tracker is killed

    if (argc != 3) {
        cerr << "Usage: ./client ip:port <info.txt>" << endl;
        return 1;
    }

    string inputIpPort = string(argv[1]);
    int colon = inputIpPort.find(':');
    if(colon == string::npos){
        cerr << "enter valid address:port";
        return 1;
    }
    string client_ip = inputIpPort.substr(0, colon);
    string Port_in_string = inputIpPort.substr(colon+1);
    int client_port = stoi(Port_in_string);

    // Start listening for other clients on seperate thread
    thread peerThread(start_peer_server, client_ip, client_port);
    peerThread.detach();

    const string filepath = argv[2];
    int sock = connect_loop(filepath); // connect initially

    string msg;
    while (true) 
    {
        cout << "Enter message (or 'quit' to exit): ";
        getline(cin, msg);

        if (msg == "quit") {
            cout << "[*] Closing connection." << endl;
            break;
        }

        if(msg.rfind("create_user", 0) == 0){  
            // while creating account i send listening ip:port of user
            msg += " " + inputIpPort;
        }

        if(msg.rfind("create_group", 0) == 0){
            msg += " " + client_ip + " " + Port_in_string;
        }

        if(msg.rfind("list_requests", 0) == 0){
            msg += " " + client_ip + " " + Port_in_string;
        }

        if(msg.rfind("join_group", 0) == 0){
            msg += " " + client_ip + " " + Port_in_string;
        }

        if(msg.rfind("accept_request", 0) == 0){
            msg += " " + client_ip + " " + Port_in_string;
        }

        if(msg.rfind("upload_file", 0) == 0){
            // Expected: upload_file <group_id> <file_path>
            istringstream iss(msg);
            string cmd, group_id, file_path;
            iss >> cmd >> group_id >> file_path;

            string full_sha1;
            long long file_size;
            vector<string> chunk_sha1s;

            try {
                chunk_sha1s = compute_file_chunks_sha1(file_path, full_sha1, file_size);
            } catch(exception &e) {
                cerr << "[!] Error reading file: " << e.what() << endl;
                continue;
            }

            // Build semicolon-separated string for chunk hashes
            stringstream ss;
            for(size_t i = 0; i < chunk_sha1s.size(); i++){
                ss << chunk_sha1s[i];
                if(i != chunk_sha1s.size()-1) ss << ";";
            }

            // Extract file name from path
            size_t pos = file_path.find_last_of("/\\");
            string file_name = (pos == string::npos) ? file_path : file_path.substr(pos+1);
            cout << full_sha1 << endl;

            // Final message to tracker
            msg = "upload_file " + group_id + " " + file_name + " " + file_path + " " + full_sha1 + " " + ss.str() + " " + to_string(file_size) + " " + client_ip + " " + Port_in_string;
        }

        if(msg.rfind("list_files", 0) == 0){
            msg += " " + client_ip + " " + Port_in_string;
        }

        if(msg.rfind("download_file", 0) == 0){
            msg += " " + client_ip + " " + Port_in_string;
        }

        // cout << msg << endl;

        if (send(sock, msg.c_str(), msg.size(), MSG_NOSIGNAL) < 0) {
            perror("send");
            cout << "[!] Lost connection to tracker. Reconnecting..." << endl;
            sock = connect_loop(filepath);
        } 
        else 
        {
            // Wait for tracker to acknowledge
            char response[4096];
            memset(response, 0, sizeof(response));
            int n = recv(sock, response, sizeof(response)-1, 0);
            if (n <= 0) {
                cout << "[!] No response from tracker, reconnecting..." << endl;
                close(sock);
                sock = connect_loop(filepath);
            }

            else
            {
                cout << response << endl;

                if( string(response) == "Done\n"){
                    istringstream iss(msg);
                    string cmd, gid, filename, filepath;
                    iss >> cmd >> gid >> filename >> filepath;

                    files[filename] = filepath;
                    cout << "File metadata uploaded successfully\n";
                    cout << "file path stored at locally at: " << files[filename] << endl;
                }

                string resp = string(response);
                istringstream iss(resp);
                string first_token;
                iss >> first_token;  

                if (first_token == "Meta_data_recieved") {
                    cout << first_token << endl;

                    // discard the leftover newline
                    iss.ignore(numeric_limits<streamsize>::max(), '\n');

                    string filesize, sha1full, sha1_of_chunks, owners, filename;
                    // read line by line after the first token
                    getline(iss, filesize);   // file size
                    getline(iss, sha1full);   // full sha1
                    getline(iss, sha1_of_chunks); // chunk sha1 list
                    getline(iss, owners);     // owners list
                    getline(iss, filename);   // filename

                    // trim leading spaces if any
                    auto trim = [](string &s) {
                        if (!s.empty() && s[0] == ' ')
                            s.erase(0, 1);
                    };
                    trim(filesize); trim(sha1full); trim(sha1_of_chunks);
                    trim(owners); trim(filename);

                    vector<string> chunk_sha1s, owners_ip_port;

                    // --- parse sha1_of_chunks ---
                    {
                        string token;
                        istringstream ss(sha1_of_chunks);
                        while (getline(ss, token, ';')) {
                            if (!token.empty())
                                chunk_sha1s.push_back(token);
                        }
                    }

                    // --- parse owners ---
                    {
                        string token;
                        istringstream ss(owners);
                        while (getline(ss, token, '|')) {
                            if (!token.empty())
                                owners_ip_port.push_back(token);
                        }
                    }


                    // Now parse original command (to get save path)
                    istringstream iss2(msg);
                    string cmd, group_id, fname, save_path;
                    iss2 >> cmd >> group_id >> fname >> save_path;
                    save_path += "/" + filename; // append filename to save path

                    if(request_file(filename, save_path, stol(filesize), chunk_sha1s, owners_ip_port)){
                        files[filename] = save_path;
                        cout << "[*] File downloaded and saved to: " << save_path << endl;

                        string msg = "File_downloaded_succesfully " + filename + " " + group_id + " " + client_ip + " " + Port_in_string + " " + save_path;
                        send(sock, msg.c_str(), msg.size(), MSG_NOSIGNAL);
                    } else {
                        cout << "[!] File download failed for: " << filename << endl;
                    };
                }
   
            }
        }
    }

    close(sock);
    return 0;
}

// 1) signal(...)    &&     2) MSG_NOSIGNAL (used as argument in send syscall)
// Why both are used together => both are used to handle graceful handling in case of socket break
// both are seperate entity, and don't need each other to work, it's just both do same task ie stop client
// from getting killed when socket connection breaks:

// signal(SIGPIPE, SIG_IGN); → covers the entire program (global safety net).
// MSG_NOSIGNAL → local protection for each send().

// It’s a defensive programming style:
// Even if you forget MSG_NOSIGNAL in one place, SIGPIPE won’t kill you (because of SIG_IGN).
// Even if someone removes signal(SIGPIPE, SIG_IGN), the MSG_NOSIGNAL here still makes this send() safe.