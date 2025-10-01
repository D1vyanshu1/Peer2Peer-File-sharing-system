#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <cstdlib>       // rand()
#include <ctime>         // time()
#include <unistd.h>      // close, lseek, ftruncate
#include <arpa/inet.h>   // socket, connect, inet_pton
#include <netinet/in.h>  // sockaddr_in
#include <sys/socket.h>  // socket APIs
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>       // open()
#include <cstring>       // memset
#include <errno.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

using namespace std;

// SHA1 for chunk
string compute_sha1(const char *data, int len) {
    unsigned char hash[SHA_DIGEST_LENGTH]; // SHA1 = 20 bytes
    SHA1(reinterpret_cast<const unsigned char*>(data), len, hash);

    // Convert to hex string
    stringstream ss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

bool request_file(const string &filename, const string &save_path, long long filesize,
                  const vector<string> &sha_chunks, const vector<string> &owners){

    const int chunk_size = 512 * 1024;
    int num_chunks = (filesize + chunk_size - 1) / chunk_size;

    // --- Preallocate file ---
    int fd = open(save_path.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("open");
        return false;
    }
    if (ftruncate(fd, filesize) < 0) {
        perror("ftruncate");
        close(fd);
        return false;
    }

    // --- Track chunks to download ---
    unordered_set<int> chunks;
    for (int i = 0; i < num_chunks; i++) chunks.insert(i);

    srand(time(nullptr)); // for randomness

    while (!chunks.empty()) {
        // Randomly pick a chunk
        int chunk_no = *next(chunks.begin(), rand() % chunks.size());
        // Randomly pick an owner
        string owner = owners[rand() % owners.size()];
        string ip = owner.substr(0, owner.find(":"));
        int port = stoi(owner.substr(owner.find(":") + 1));

        // --- Connect to peer ---
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            continue;
        }

        sockaddr_in peer_addr{};
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(port);
        if(inet_pton(AF_INET, ip.c_str(), &peer_addr.sin_addr) <= 0){
            perror("inet_pton");
            close(sock);
            continue;
        }

        if(connect(sock, (sockaddr*)&peer_addr, sizeof(peer_addr)) < 0){
            cout << "[!] Could not connect to " << ip << ":" << port << endl;
            close(sock);
            continue;
        }

        // --- Send request ---
        string req = "get_chunk " + filename + " " + to_string(chunk_no);
        if (send(sock, req.c_str(), req.size(), 0) < 0) {
            perror("send");
            close(sock);
            continue;
        }

        // --- Receive chunk ---
        vector<char> buffer(chunk_size);
        int received = 0;
        while (received < chunk_size) {
            int n = recv(sock, buffer.data() + received, chunk_size - received, 0);
            if (n <= 0) break; // connection closed or error
            received += n;
        }
        close(sock);

        if (received <= 0) {
            cout << "[!] Failed to receive chunk " << chunk_no << endl;
            continue;
        }

        // --- Verify SHA1 ---
        string sha = compute_sha1(buffer.data(), received);
        if (sha != sha_chunks[chunk_no]) {
            cerr << "[!] SHA1 mismatch for chunk " << chunk_no << endl;
            continue;
        }

        // --- Write chunk to file ---
        if (lseek(fd, (off_t)chunk_no * chunk_size, SEEK_SET) < 0) {
            perror("lseek");
            continue;
        }
        if (write(fd, buffer.data(), received) < 0) {
            perror("write");
            continue;
        }

        // Mark chunk as downloaded
        chunks.erase(chunk_no);
        cout << "[+] Downloaded chunk " << chunk_no
             << " (" << received << " bytes) from client: " + owner<< endl;
    }

    close(fd);
    cout << "[*] File download completed: " << save_path << endl;

    return true;
}
