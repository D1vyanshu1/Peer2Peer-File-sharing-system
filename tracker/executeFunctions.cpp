#include <iostream>
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "theadher.h"
using namespace std;

class User {
    public:
    string username;
    string password;
    string ip;
    int port;
    bool isLoggedIn;
};

class FileInfo{
    public:
        string file_name;
        long long file_size;
        string sha1_full;
        vector<string> sha1_chunks;
        unordered_map<string, string> owners; // stores map containing owner and local path of file for that owner
};

class Group {
    public:
    string group_id;
    string owner;
    unordered_set<string> members;       // set of usernames
    unordered_set<string> pending_requests; // set of usernames who requested to join
    unordered_map<string, FileInfo> files;    // file_name -> FileInfo
};

// all users
unordered_map<string, User> user_list;
mutex user_mutex;

// all grps
unordered_map<string, Group> grp_list;
mutex grp_mutex;

// Helper 1: get username from IP and port
string get_username_from_ip_port(const string &ip, int port) {
    lock_guard<mutex> lock(user_mutex);
    for (auto &it : user_list) {
        if (it.second.ip == ip && it.second.port == port) {
            return it.second.username;
        }
    }
    return ""; // return empty if not found
}

// Helper 2: check if user is logged in
bool is_user_logged_in(const string &username) {
    lock_guard<mutex> lock(user_mutex);
    auto it = user_list.find(username);
    if (it != user_list.end()) {
        return it->second.isLoggedIn;
    }
    return false;
}

// unordered_map<string, FileInfo> files; // key = file_name

bool add_user(const string &user_name, const string &password, const string &ip, int port) {
    lock_guard<mutex> lock(user_mutex);

    if (user_list.find(user_name) != user_list.end()) {
        cout << "[!] User already exists: " << user_name << endl;
        return false;
    }

    user_list[user_name] = User{user_name, password, ip, port, false};

    cout << "user created listening at port: " << ip << ":" << port << endl;
    return true;
}

bool authenticate_user(const string &user, const string &password) {
    lock_guard<mutex> lock(user_mutex);

    if (user_list.find(user) == user_list.end()) {
        cout << "[!] No such user: " << user << endl;
        return false;
    }

    if (user_list[user].password == password) {
        user_list[user].isLoggedIn = true;
        cout << "[*] User logged in: " << user << endl;
        return true;
    } else {
        cout << "[!] Wrong password for user: " << user << endl;
        return false;
    }
}

// Used for syncing with peer tracker
void force_add_user(const string &user_name, const string &password, const string &ip, int port) {
    lock_guard<mutex> lock(user_mutex);

    cout << "entered here " << endl;
    user_list[user_name] = User{user_name, password, ip, port, false};
    cout << "[SYNC] User forced add/update: " << user_name << endl;
}


void force_login_user(const string &user) {
    lock_guard<mutex> lock(user_mutex);
    auto it = user_list.find(user);
    if (it != user_list.end()) {
        it->second.isLoggedIn = true;
    }
}


bool Create_group(const string &grp_id, const string &ip, int port){
    string user_name = get_username_from_ip_port(ip, port);

    if (user_name.empty()) {
        cout << "Create user first" << endl;
        return false;
    }

    if (!is_user_logged_in(user_name)) {
        cout << "Log in first before creating group" << endl;
        return false;
    }


    {
        lock_guard<mutex> grp_lock(grp_mutex);

        if(grp_list.find(grp_id) != grp_list.end()){
            cout << "Group already exists" << endl;
            return false;
        }
    
        Group g;
        g.group_id = grp_id;
        g.owner = user_name;
        g.members.insert(user_name);
        
        grp_list[grp_id] = g;
    }

    cout << user_name << " created group " << grp_id << " successfully" << endl;

    return true;
}

void force_add_Group(const string &grp_id, const string &ip, int port){
    string user_name = get_username_from_ip_port(ip, port);

    {
        lock_guard<mutex> lock(grp_mutex);

        Group g;
        g.group_id = grp_id;
        g.owner = user_name;
        g.members.insert(user_name);
        grp_list[grp_id] = g;
    
        cout << "[SYNC] Group forced created: " << grp_id << " by " << user_name << endl;
    }
}

void list_groups(int client_sock){
    for(auto it: grp_list){
        string msg = it.first + "\n";
        send(client_sock, msg.c_str(), msg.size(), 0);
    }
}

void join_request(const string &grpid, const string &ip, int port, int client_socket){
    string user_name = get_username_from_ip_port(ip, port);

    if (user_name.empty()) {
        cout << "Create user first" << endl;
        return;
    }

    if (!is_user_logged_in(user_name)) {
        cout << "Log in first before creating group" << endl;
        return;
    }

    // insert request
    {
        lock_guard<mutex> grp_lock(grp_mutex);      

        // check if requesting user is already in grp
        for(auto it: grp_list[grpid].members){
            if(it == user_name){
                string msg = "already member of grp";
                send(client_socket, msg.c_str(), msg.size(), 0);
                return;
            }
        }

        grp_list[grpid].pending_requests.insert(user_name);
        string msg = "group joining request sent by " + user_name + "\n";
        send(client_socket, msg.c_str(), msg.size(), 0);

        string sync_msg = "SYNC request_join " + grpid + " " + user_name;
        send_to_peer(sync_msg);
    }
}

void force_request_join(const string &gid, const string &user_id){
    lock_guard<mutex> grp_lock(grp_mutex);
    auto it = grp_list.find(gid);
    if (it == grp_list.end()) return;

    it->second.pending_requests.insert(user_id);
    cout << "[SYNC] Pending request from " << user_id << " for group " << gid << endl;
}

void list_requests(const string &grpid, const string &ip, int port, int client_socket){
    string user_name = "", gid = "";
    // cout << "entered in list requests \n";

    {
        // lock_guard<mutex> user_lock(user_mutex);
        user_name = get_username_from_ip_port(ip, port);

        cout << "user name is : " << user_name << "\n";

        for(auto it: grp_list){
            if(it.second.owner == user_name){
                gid = it.first;
            }
        }

        cout << "grp id is : " << gid << "\n";
        
        if(gid == ""){
            string msg = "not owner of any grp";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        for(auto it: grp_list[gid].pending_requests){
            string msg = it + "\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
        }
    }
}

void accept_request(const string &gid, const string &user_id, const string &ip, int port, int client_socket){
    string owner_user = get_username_from_ip_port(ip, port);

    if (owner_user.empty()) {
        cout << "Create user first" << endl;
        return;
    }

    if (!is_user_logged_in(owner_user)) {
        cout << "Log in first before creating group" << endl;
        return;
    }

    // step 2: check if group exists
    {
        lock_guard<mutex> grp_lock(grp_mutex);
        auto grp_it = grp_list.find(gid);
        if (grp_it == grp_list.end()) {
            string msg = "Group " + gid + " does not exist\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        Group &g = grp_it->second;

        // step 3: check if this user is the owner
        if (g.owner != owner_user) {
            string msg = "You are not the owner of group " + gid + "\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        // step 4: check if requested user is in pending_requests
        if (g.pending_requests.find(user_id) == g.pending_requests.end()) {
            string msg = "No join request from " + user_id + " for group " + gid + "\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        // step 5: accept request
        g.pending_requests.erase(user_id);
        g.members.insert(user_id);

        string msg = "User " + user_id + " added to group " + gid + "\n";
        send(client_socket, msg.c_str(), msg.size(), 0);

        // ---- SYNC BROADCAST ----
        string sync_msg = "SYNC accept_request " + gid + " " + user_id;
        send_to_peer(sync_msg);
    }
}


void force_accept_request(const string &gid, const string &user_id){
    lock_guard<mutex> grp_lock(grp_mutex);

    auto it = grp_list.find(gid);
    if (it == grp_list.end()) return; // group not found

    Group &g = it->second;

    // remove from pending if exists, then add to members
    g.pending_requests.erase(user_id);
    g.members.insert(user_id);

    cout << "[SYNC] User " << user_id << " added to group " << gid << endl;
}

// file uploading part
void upload_file(const string &gid, const string &fileName, const string &filePath,
                 const string &fullSha1, const string &Sha1_of_chunks,
                 const string &size_of_file, int client_socket, const string &ip, int port) {

    // Step 1: find the username for this ip:port and ensure logged in
    string user_name = get_username_from_ip_port(ip, port);

    if (user_name.empty()) {
        cout << "Create user first" << endl;
        return;
    }

    if (!is_user_logged_in(user_name)) {
        cout << "Log in first before uploading group" << endl;
        return;
    }

    // Step 2: parse file size
    long long fileSize = 0;
    try {
        fileSize = stoll(size_of_file);
    } catch (const exception &e) {
        string msg = string("Invalid file size: ") + size_of_file + "\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    // Step 3: parse semicolon-separated chunk SHA1s
    vector<string> chunkSha1s;
    {
        stringstream ss(Sha1_of_chunks);
        string token;
        while (getline(ss, token, ';')) {
            if (!token.empty()) chunkSha1s.push_back(token);
        }
    }

    // Step 4: store metadata under group -> files
    {
        lock_guard<mutex> grp_lock(grp_mutex);

        auto git = grp_list.find(gid);
        if (git == grp_list.end()) {
            string msg = "Group does not exist\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        Group &grp = git->second;

        if(grp.files.find(fileName) != grp.files.end()){
            string msg = "file metadata already uploaded\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        // Create or update FileInfo for this filename
        FileInfo &f = grp.files[fileName]; // default-constructs if missing

        // If an existing file has different full SHA1 or size, you might want to warn/handle.
        // Here we will overwrite metadata with the new info (and add owner).
        f.file_name = fileName;
        f.file_size = fileSize;
        f.sha1_full = fullSha1;
        f.sha1_chunks = chunkSha1s;

        // Add owner -> local path
        f.owners[user_name] = filePath;

        // Send acknowledgement to client
        string msg = "Done\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
    }

    string sync_msg = "SYNC upload_file " + gid + " " + fileName + " " + filePath + " " + fullSha1 + " " + Sha1_of_chunks + " " + size_of_file + " " + user_name;
    send_to_peer(sync_msg);
}


// Example: force handler for syncing upload from peer (applies metadata without ownership IP/port checks)
void force_upload_file(const string &gid, const string &fileName, const string &filePath,
                       const string &fullSha1, const string &Sha1_of_chunks,
                       const string &size_of_file, const string &owner_name) {
    long long fileSize = 0;
    try { fileSize = stoll(size_of_file); } catch(...) { return; }

    vector<string> chunkSha1s;
    {
        stringstream ss(Sha1_of_chunks);
        string token;
        while (getline(ss, token, ';')) {
            if (!token.empty()) chunkSha1s.push_back(token);
        }
    }

    lock_guard<mutex> grp_lock(grp_mutex);
    auto git = grp_list.find(gid);
    if (git == grp_list.end()) return;
    Group &grp = git->second;

    FileInfo &f = grp.files[fileName];
    f.file_name = fileName;
    f.file_size = fileSize;
    f.sha1_full = fullSha1;
    f.sha1_chunks = chunkSha1s;
    if (!owner_name.empty()) f.owners[owner_name] = filePath;

    cout << "[SYNC] File metadata for " << fileName << " added in group " << gid << " by " << owner_name << endl;
}


void list_files(const string &grp_id, const string &ip, int port, int client_socket) {
    string user_name = get_username_from_ip_port(ip, port);

    lock_guard<mutex> grp_lock(grp_mutex);
    auto it = grp_list.find(grp_id);
    if (it == grp_list.end()) {
        string msg = "Group " + grp_id + " does not exist\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    Group &g = it->second;

    // check membership
    if (g.members.find(user_name) == g.members.end()) {
        string msg = "You are not part of group " + grp_id + "\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    // no files
    if (g.files.empty()) {
        string msg = "No files available in group " + grp_id + "\n";
        send(client_socket, msg.c_str(), msg.size(), 0);
        return;
    }

    // list files
    for (auto &file_it : g.files) {
        const FileInfo &f = file_it.second;

        string msg = f.file_name + "\n";

        send(client_socket, msg.c_str(), msg.size(), 0);
    }
}


void download_file(string gid, string filename, string ip, int port, int client_socket){
    string user_name = get_username_from_ip_port(ip, port);

    if(user_name.empty()){
        cout << "Create user first" << endl;
        return;
    }

    if(!is_user_logged_in(user_name)){
        cout << "Log in first before uploading group" << endl;
        return;
    }
    
    // step 2: check if group exists, user is member, file is present or not
    {
        lock_guard<mutex> grp_lock(grp_mutex);
        auto grp_it = grp_list.find(gid);
        if (grp_it == grp_list.end()) {
            string msg = "Group " + gid + " does not exist\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        Group &grp = grp_it->second;

        if(grp.members.find(user_name) == grp.members.end()){
            string msg = "you are not member of group, so can't request file\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }

        if(grp.files.find(filename) == grp.files.end()){
            string msg = "file not present in the group\n";
            send(client_socket, msg.c_str(), msg.size(), 0);
            return;
        }
    }

    {
        lock_guard<mutex> grp_lock(grp_mutex);
        Group &grp = grp_list[gid];
        FileInfo &file = grp.files[filename];

        // build metadata string
        string msg = "Meta_data_recieved\n";

        // 1. file size
        msg += to_string(file.file_size) + "\n";

        // 2. full sha1
        msg += file.sha1_full + "\n";

        // 3. sha1 of chunks (semicolon separated)
        for (size_t i = 0; i < file.sha1_chunks.size(); i++) {
            msg += file.sha1_chunks[i];
            if (i != file.sha1_chunks.size() - 1)
                msg += ";";
        }
        msg += "\n";

        // 4. all owners in format ip:port|ip:port|...
        string owners_str;
        for (auto &it : file.owners) {
            const string &owner_username = it.first;
            if (user_list.find(owner_username) != user_list.end()) {
                User &owner = user_list[owner_username];
                owners_str += owner.ip + ":" + to_string(owner.port) + "|";
            }
        }

        if (!owners_str.empty() && owners_str.back() == '|'){
            owners_str.pop_back();
        }

        msg += owners_str + "\n";

        msg += file.file_name + "\n";

        // send metadata
        send(client_socket, msg.c_str(), msg.size(), 0);
    }

}

void download_success(string gid, string filename, string ip, int port, string filepath){
    string user_name = get_username_from_ip_port(ip, port);

    grp_list[gid].files[filename].owners[user_name] = filepath;

    string sync_msg = "SYNC download_success" + gid + " " + filename + " " + user_name + " " + filepath;

    send_to_peer(sync_msg);
}

void force_download_success(string gid, string filename, string username, string filepath){
    grp_list[gid].files[filename].owners[username] = filepath;
}

// checking files: upload_file A1 /home/divyanshu-jain/Downloads/DSA_half_notes.pdf