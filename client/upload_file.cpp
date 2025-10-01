#include "cheadher.h"

using namespace std;

string sha1_to_string(unsigned char hash[SHA_DIGEST_LENGTH]) {
    stringstream ss;
    for(int i = 0; i < SHA_DIGEST_LENGTH; i++)
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    return ss.str();
}

vector<string> compute_file_chunks_sha1(const string &file_path, string &full_sha1, long long &file_size) {
    ifstream file(file_path, ios::binary);
    if(!file) throw runtime_error("Cannot open file: " + file_path);

    const size_t CHUNK_SIZE = 512 * 1024; // 512KB
    vector<string> chunk_hashes;
    vector<unsigned char> buffer(CHUNK_SIZE);
    SHA_CTX sha_ctx;
    SHA1_Init(&sha_ctx);

    file.seekg(0, ios::end);
    file_size = file.tellg();
    file.seekg(0, ios::beg);

    while(file) {
        file.read(reinterpret_cast<char*>(buffer.data()), CHUNK_SIZE);
        size_t read_bytes = file.gcount();
        if(read_bytes == 0) break;

        // SHA1 for this chunk
        unsigned char chunk_hash[SHA_DIGEST_LENGTH];
        SHA1(buffer.data(), read_bytes, chunk_hash);
        chunk_hashes.push_back(sha1_to_string(chunk_hash));

        // Update full file SHA1
        SHA1_Update(&sha_ctx, buffer.data(), read_bytes);
    }

    unsigned char full_hash[SHA_DIGEST_LENGTH];
    SHA1_Final(full_hash, &sha_ctx);
    full_sha1 = sha1_to_string(full_hash);

    return chunk_hashes;
}
