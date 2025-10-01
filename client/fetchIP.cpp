#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>
using namespace std;

pair<string, int> read_info(const string &filename, int n) {
    // Open file
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    // Read contents into buffer
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("read");
        close(fd);
        exit(1);
    }
    close(fd);

    // Tokenize lines by "\n"
    char *line = strtok(buffer, "\n");
    // line = strtok(NULL, "\n"); // this overwrites second line, so only second ip:port pair is process here

    // below code will run till n, so at end line will contain ip:port pair at nth line, 
    // since we keep looping , all lines before it are overwritten, see connect_loop function, two calls are made with
    // n = 1 and n = 2 
    int i = 1;   
    while (line && i < n) {
        line = strtok(NULL, "\n");  // move to next line
        i++;
    }

    if (!line) {
        cerr << "Requested line not found in file" << endl;
        exit(1);
    }

    // Split line into ip and port using ':'
    char *ip_str = strtok(line, ":");
    char *port_str = strtok(NULL, ":");

    if (!ip_str || !port_str) {
        cerr << "Invalid format in info.txt" << endl;
        exit(1);
    }

    string ip = string(ip_str);
    int port = stoi(port_str);

    return {ip, port};
}
