#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <algorithm> // for find

using namespace std;

// Helper: Decompress a git object from .git/objects given its SHA
string readObject(const string& sha) {
    string dirName = sha.substr(0, 2);
    string fileName = sha.substr(2);
    filesystem::path filePath = ".git/objects/" + dirName + "/" + fileName;

    if (!filesystem::exists(filePath)) {
        throw runtime_error("Object not found: " + sha);
    }

    ifstream file(filePath, ios::binary);
    if (!file.is_open()) {
        throw runtime_error("Failed to open object file: " + sha);
    }

    vector<char> compressedData((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    file.close();

    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = compressedData.size();
    zs.next_in = reinterpret_cast<Bytef*>(compressedData.data());

    if (inflateInit(&zs) != Z_OK) {
        throw runtime_error("Failed to initialize zlib.");
    }

    vector<char> buffer(8192);
    string decompressed;
    int ret;

    do {
        zs.avail_out = buffer.size();
        zs.next_out = reinterpret_cast<Bytef*>(buffer.data());

        ret = inflate(&zs, Z_NO_FLUSH);

        if (decompressed.size() < zs.total_out) {
            decompressed.append(buffer.data(), zs.total_out - decompressed.size());
        }

    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    return decompressed;
}

int main(int argc, char *argv[])
{
    cout << unitbuf;
    cerr << unitbuf;

    if (argc < 2) {
        cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    string command = argv[1];
    
    if (command == "init") {
        try {
            filesystem::create_directory(".git");
            filesystem::create_directory(".git/objects");
            filesystem::create_directory(".git/refs");
    
            ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }
    
            cout << "Initialized git directory\n";
        } catch (const filesystem::filesystem_error& e) {
            cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "cat-file") {
        if (argc < 4 || string(argv[2]) != "-p") {
            cerr << "Usage: cat-file -p <blob_sha>\n";
            return EXIT_FAILURE;
        }

        try {
            string content = readObject(argv[3]);
            // Find null byte separating header from content
            size_t nullPos = content.find('\0');
            if (nullPos == string::npos) {
                cerr << "Invalid object format." << endl;
                return EXIT_FAILURE;
            }
            cout << content.substr(nullPos + 1);
        } catch (const exception& e) {
            cerr << e.what() << endl;
            return EXIT_FAILURE;
        }

    } else if (command == "hash-object") {
        // ... (Your hash-object code remains exactly the same) ...
        if (argc < 4 || string(argv[2]) != "-w") {
            cerr << "Usage: hash-object -w <file_path>\n";
            return EXIT_FAILURE;
        }
        string file_path = argv[3];
        ifstream file(file_path, ios::binary);
        if (!file.is_open()) { cerr << "Failed to open file." << endl; return EXIT_FAILURE; }
        vector<char> fileData((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        file.close();

        string header = "blob " + to_string(fileData.size()) + '\0';
        string fullData = header + string(fileData.begin(), fileData.end());

        unsigned char hash[20];
        SHA1(reinterpret_cast<const unsigned char*>(fullData.data()), fullData.size(), hash);

        stringstream ss;
        for (int i = 0; i < 20; ++i) ss << hex << setw(2) << setfill('0') << (int)hash[i];
        string sha1 = ss.str();

        uLongf compressedSize = compressBound(fullData.size());
        vector<Bytef> compressedData(compressedSize);
        if (compress(compressedData.data(), &compressedSize, reinterpret_cast<const Bytef*>(fullData.data()), fullData.size()) != Z_OK) {
             cerr << "Compression failed." << endl; return EXIT_FAILURE; 
        }

        string dirName = sha1.substr(0, 2);
        string fileName = sha1.substr(2);
        filesystem::create_directories(".git/objects/" + dirName);
        ofstream objectFile(".git/objects/" + dirName + "/" + fileName, ios::binary);
        objectFile.write(reinterpret_cast<const char*>(compressedData.data()), compressedSize);
        objectFile.close();
        cout << sha1 << endl;

    } else if (command == "ls-tree") {
        if (argc < 4 || string(argv[2]) != "--name-only") {
            cerr << "Usage: ls-tree --name-only <tree_sha>\n";
            return EXIT_FAILURE;
        }

        string tree_sha = argv[3];
        try {
            string content = readObject(tree_sha);
            
            // 1. Skip the header "tree <size>\0"
            size_t headerEnd = content.find('\0');
            if (headerEnd == string::npos) return EXIT_FAILURE;
            
            // i points to the start of the first entry
            size_t i = headerEnd + 1; 

            // Loop until we run out of data
            while (i < content.size()) {
                // Format: <mode> <name>\0<20_byte_sha>
                
                // 2. Find the space after mode (we ignore mode for now)
                size_t spacePos = content.find(' ', i);
                if (spacePos == string::npos) break;

                // 3. Find the null byte after name
                size_t nullPos = content.find('\0', spacePos);
                if (nullPos == string::npos) break;

                // Extract name
                string name = content.substr(spacePos + 1, nullPos - (spacePos + 1));
                cout << name << endl;

                // 4. Advance past the null byte AND the 20-byte SHA
                // The SHA is exactly 20 bytes long
                i = nullPos + 1 + 20;
            }
        } catch (const exception& e) {
            cerr << e.what() << endl;
            return EXIT_FAILURE;
        }

    } else {
        cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}