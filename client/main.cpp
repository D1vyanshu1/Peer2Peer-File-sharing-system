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

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN); // prevent client from dying if tracker is killed

    if (argc != 2) {
        cerr << "Usage: ./client <info.txt>" << endl;
        return 1;
    }

    const string filepath = argv[1];
    int sock = connect_loop(filepath); // connect initially

    string msg;
    while (true) {
        cout << "Enter message (or 'quit' to exit): ";
        getline(cin, msg);

        if (msg == "quit") {
            cout << "[*] Closing connection." << endl;
            break;
        }

        if (send(sock, msg.c_str(), msg.size(), MSG_NOSIGNAL) < 0) {  // use MSG_NOSIGNAL
            perror("send");
            cout << "[!] Lost connection to tracker. Reconnecting..." << endl;
            sock = connect_loop(filepath);
        }
    }

    close(sock);
    return 0;
}
