#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>
#include <signal.h> 
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <vector>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <csignal>

using namespace std;

// Global user storage (RAM only)
extern unordered_map<string, string> users;
extern mutex user_mutex;
pair<string, int> read_info(const string &filename, int n);
int connect_to_tracker(const string &ip, int tracker_port);
int connect_loop(const string &filepath);

string sha1_to_string(unsigned char hash[SHA_DIGEST_LENGTH]);
vector<string> compute_file_chunks_sha1(const string &file_path, string &full_sha1, long long &file_size);

bool request_file(const string &filename, const string &save_path, long long filesize,
                  const vector<string> &sha_chunks, const vector<string> &owners);