#include "theadher.h"
#include <sstream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
using namespace std;

void callCommandsTracker(int client_sock, char *buffer) {
    string command(buffer);
    istringstream iss(command);
    string cmd;
    iss >> cmd;

    if (cmd == "create_user") {
        cout << command << endl;
        istringstream iss2(command);
        string cmd, user, pass, ipPortCombo;
        iss2 >> cmd >> user >> pass >> ipPortCombo;

        int colon = ipPortCombo.find(':');
        string client_ip = ipPortCombo.substr(0, colon);
        int client_port = stoi(ipPortCombo.substr(colon+1));

        if (add_user(user, pass, client_ip, client_port)) {
            string msg = "User created successfully\n";
            send(client_sock, msg.c_str(), msg.size(), 0);

            // Sync with peer
            string sync_msg = "SYNC create_user " + user + " " + pass + " " + ipPortCombo;
            send_to_peer(sync_msg);
        } else {
            string msg = "User already exists\n";
            send(client_sock, msg.c_str(), msg.size(), 0);
        }
    }

    else if (cmd == "login") {
        istringstream iss2(command); 
        string cmd, user, pass;
        iss2 >> cmd >> user >> pass;

        if (authenticate_user(user, pass)) {
            string msg = "Login successful\n";
            send(client_sock, msg.c_str(), msg.size(), 0);

            // Sync with peer
            string sync_msg = "SYNC login " + user;
            send_to_peer(sync_msg);
        } else {
            std::string msg = "Invalid credentials\n";
            send(client_sock, msg.c_str(), msg.size(), 0);
        }
    }

    else if(cmd == "create_group"){
        istringstream iss2(command); 
        string cmd, grpId, ip, port;
        iss2 >> cmd >> grpId >> ip >> port;

        if(Create_group(grpId, ip, stoi(port))){
            string msg = "Group created successfully\n";
            send(client_sock, msg.c_str(), msg.size(), 0);

            // Sync with peer
            string sync_msg = "SYNC create_group " + grpId + " " + ip + " " + port;
            send_to_peer(sync_msg);
        }
    }

    else if(cmd == "list_groups"){
        list_groups(client_sock);
    }

    else if(cmd == "join_group"){
        istringstream iss2(command); 
        string cmd, grpid, client_ip, client_port;
        iss2 >> cmd >> grpid >> client_ip >> client_port;

        join_request(grpid, client_ip, stoi(client_port), client_sock);
    }

    else if(cmd == "list_requests"){
        istringstream iss2(command);
        string cmd, gid, ip, port;
        iss2 >> cmd >> gid >> ip >> port; 
        
        list_requests(gid, ip, stoi(port), client_sock);
    }

    else if(cmd == "accept_request"){
        istringstream iss2(command);
        string cmd, gid, user, ip, port;
        iss2 >> cmd >> gid >> user >> ip >> port;

        accept_request(gid, user, ip, stoi(port), client_sock);
    }

    else if(cmd == "upload_file"){
        istringstream iss2(command);
        string cmd, gid, filename, filePath, fullsha1, chunksSHA1, ip, port, size;
        iss2 >> cmd >> gid >> filename >> filePath >> fullsha1 >> chunksSHA1 >> size >> ip >> port;

        upload_file(gid, filename, filePath, fullsha1, chunksSHA1, size, client_sock, ip, stoi(port));
    }

    else if(cmd == "list_files"){
        istringstream iss2(command);
        string cmd, gid, ip, port;
        iss2 >> cmd >> gid >> ip >> port;
        list_files(gid, ip, stoi(port), client_sock);
    }

    else if(cmd == "download_file"){
        istringstream iss2(command);
        string cmd, gid, filename, filepath, ip, port;
        iss2 >> cmd >> gid >> filename >> filepath >> ip >> port;
        
        download_file(gid, filename, ip, stoi(port), client_sock);
    }

    else if(cmd == "File_downloaded_succesfully"){
        istringstream iss2(command);
        string cmd, filename, gid, ip, port, localpath;
        iss2 >> cmd >> filename >> gid >> ip >> port >> localpath;

        download_success(gid, filename, ip, stoi(port), localpath);
    }

    // tracker-to-tracker sync handler
    else if (cmd == "SYNC") {
        istringstream iss2(command);
        string sync, subcmd;
        iss2 >> sync >> subcmd;

        if (subcmd == "create_user") {
            string user, pass, ipPortCombo;
            iss2 >> user >> pass >> ipPortCombo;
            int colon = ipPortCombo.find(':');
            string client_ip = ipPortCombo.substr(0, colon);
            int client_port = stoi(ipPortCombo.substr(colon+1));
            force_add_user(user, pass, client_ip, client_port);

        }
        else if (subcmd == "login") {
            string user;
            iss2 >> user;
            force_login_user(user);
        }
        else if(subcmd == "create_group"){
            string grp_id, ip, port;
            iss2 >> grp_id >> ip >> port;
            force_add_Group(grp_id, ip, stoi(port));
        }
        else if(subcmd == "request_join"){
            string grp_id, user_id;
            iss2 >> grp_id >> user_id;
            force_request_join(grp_id, user_id);
        }
        else if(subcmd == "accept_request"){
            string grp_id, user_id;
            iss2 >> grp_id >> user_id;
            force_accept_request(grp_id, user_id);
        }
        else if(subcmd == "upload_file"){
            string gid, filename, filepath, fullsha1, chunksha1, size, username;
            iss2 >> gid >> filename >> filepath >> fullsha1 >> chunksha1 >> size >> username;
            force_upload_file(gid, filename, filepath, fullsha1, chunksha1, size, username);
        }
        else if(subcmd == "download_file"){
            string gid, filename, username, filepath;
            iss2 >> gid >> filename >> username >> filepath;

            
        }
    }

    else {
        string msg = "Invalid command entered\n";
        send(client_sock, msg.c_str(), msg.size(), 0);
    }
}


