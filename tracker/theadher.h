#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unordered_map>
using namespace std;

// --- existing code ---
pair<string, int> read_info(const string &filename, int n);
void callCommandsTracker(int client_sock, char* buffer);
bool add_user(const string &user, const string &password, const string &ip, int port);
bool authenticate_user(const string &user, const string &password);

// --- NEW for tracker-to-tracker sync ---
void init_peer_connection(const string filepath, int peer_index);
void send_to_peer(const string &message);   // send sync message to peer

void force_add_user(const string &user, const string &password, const string &ip, int port);
void force_login_user(const string &user);

// --- group commands ---
bool Create_group(const string &grp_id, const string &ip, int port);
void force_add_Group(const string &grp_id, const string &ip, int port);
void list_groups(int client_sock);
void join_request(const string &grpid, const string &ip, int port, int client_socket);
void force_request_join(const string &gid, const string &user_id);
void list_requests(const string &grpid, const string &ip, int port, int client_socket);
void accept_request(const string &gid, const string &user_id, const string &ip, int port, int client_socket);
void force_accept_request(const string &gid, const string &user_id);

// file upload
void upload_file(const string &gid, const string &fileName, const string &filePath,
                 const string &fullSha1, const string &Sha1_of_chunks,
                 const string &size_of_file, int client_socket, const string &ip, int port);


void force_upload_file(const string &gid, const string &fileName, const string &filePath,
                       const string &fullSha1, const string &Sha1_of_chunks,
                       const string &size_of_file, const string &owner_name);           
                       
void list_files(const string &grp_id, const string &ip, int port, int client_socket);

void download_file(string gid, string filename, string ip, int port, int client_socket);

// downlaod success
void download_success(string gid, string filename, string ip, int port, string filepath);
void force_download_success(string gid, string filename, string username, string filepath);