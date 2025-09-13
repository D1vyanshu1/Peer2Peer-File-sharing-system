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
using namespace std;

pair<string, int> read_info(const string &filename, int n);
int connect_to_tracker(const string &ip, int port);
int connect_loop(const string &filepath);