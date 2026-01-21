#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <algorithm> // Required for sorting

using namespace std;
namespace fs = std::filesystem;

// --- Helper Functions ---

// Convert raw 20-byte SHA string to 40-char Hex string
string shaToHex(const string& rawSha) {
    stringstream ss;
    for (unsigned char c : rawSha) {
        ss << hex << setw(2) << setfill('0') << (int)c;
    }
    return ss.str();
}

// Write a git object (Blob or Tree or Commit) to .git/objects
// Returns the raw 20-byte SHA-1
string writeObject(const string& type, const string& content) {
    // 1. Prepare Header: "type <size>\0"
    string header = type + " " + to_string(content.size()) + '\0';
    string store = header + content;

    // 2. Compute SHA-1
    unsigned char hash[20];
    SHA1(reinterpret_cast<const unsigned char*>(store.data()), store.size(), hash);
    string sha1Raw((char*)hash, 20);
    string sha1Hex = shaToHex(sha1Raw);

    // 3. Compress using Zlib
    uLongf compressedSize = compressBound(store.size());
    vector<Bytef> compressedData(compressedSize);
    if (compress(compressedData.data(), &compressedSize, reinterpret_cast<const Bytef*>(store.data()), store.size()) != Z_OK) {
        throw runtime_error("Compression failed");
    }

    // 4. Write to Disk
    string dirName = sha1Hex.substr(0, 2);
    string fileName = sha1Hex.substr(2);
    fs::path dirPath = ".git/objects/" + dirName;
    
    if (!fs::exists(dirPath)) {
        fs::create_directories(dirPath);
    }

    ofstream outFile(dirPath / fileName, ios::binary);
    if (!outFile.is_open()) throw runtime_error("Failed to write object file");
    outFile.write(reinterpret_cast<const char*>(compressedData.data()), compressedSize);
    outFile.close();

    return sha1Raw;
}

// Read and decompress an object (used by cat-file and ls-tree)
string readObject(const string& sha) {
    string dirName = sha.substr(0, 2);
    string fileName = sha.substr(2);
    fs::path filePath = ".git/objects/" + dirName + "/" + fileName;

    if (!fs::exists(filePath)) throw runtime_error("Object not found: " + sha);

    ifstream file(filePath, ios::binary);
    if (!file.is_open()) throw runtime_error("Failed to open object file");

    vector<char> compressed((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    file.close();

    z_stream zs = {0}; // Zero-init
    if (inflateInit(&zs) != Z_OK) throw runtime_error("Failed to initialize zlib");

    zs.avail_in = compressed.size();
    zs.next_in = reinterpret_cast<Bytef*>(compressed.data());

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

// Struct to hold tree entries for sorting
struct TreeEntry {
    string name;
    string mode;     // "100644" or "40000"
    string shaRaw;   // 20-byte raw SHA

    // Sorting operator required by Git (sort by name)
    bool operator<(const TreeEntry& other) const {
        return name < other.name;
    }
};

// Recursive function to build trees
string writeTree(const fs::path& directory) {
    vector<TreeEntry> entries;

    for (const auto& entry : fs::directory_iterator(directory)) {
        string name = entry.path().filename().string();
        
        // Ignore .git directory
        if (name == ".git") continue;

        TreeEntry te;
        te.name = name;

        if (entry.is_directory()) {
            te.mode = "40000";
            // Recursively write the subdirectory tree
            te.shaRaw = writeTree(entry.path());
        } else {
            te.mode = "100644"; // Assuming regular file
            
            // Read file content
            ifstream file(entry.path(), ios::binary);
            string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            
            // Write blob and get SHA
            te.shaRaw = writeObject("blob", content);
        }
        entries.push_back(te);
    }

    // Sort entries alphabetically
    sort(entries.begin(), entries.end());

    // Construct the Tree Object content
    string treeContent;
    for (const auto& e : entries) {
        // Format: <mode> <name>\0<20_byte_sha>
        treeContent += e.mode + " " + e.name + '\0' + e.shaRaw;
    }

    // Write the tree object itself
    return writeObject("tree", treeContent);
}

// --- Main ---

int main(int argc, char *argv[])
{
    cout << unitbuf;
    cerr << unitbuf;

    if (argc < 2) {
        cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    string command = argv[1];
    
    try {
        if (command == "init") {
            fs::create_directories(".git/objects");
            fs::create_directories(".git/refs");
            ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            }
            cout << "Initialized git directory\n";

        } else if (command == "cat-file") {
            if (argc < 4 || string(argv[2]) != "-p") return EXIT_FAILURE;
            string content = readObject(argv[3]);
            size_t nullPos = content.find('\0');
            cout << content.substr(nullPos + 1);

        } else if (command == "hash-object") {
            if (argc < 4 || string(argv[2]) != "-w") return EXIT_FAILURE;
            ifstream file(argv[3], ios::binary);
            string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            string rawSha = writeObject("blob", content);
            cout << shaToHex(rawSha) << endl;

        } else if (command == "ls-tree") {
            if (argc < 4 || string(argv[2]) != "--name-only") return EXIT_FAILURE;
            string content = readObject(argv[3]);
            size_t i = content.find('\0') + 1; // Skip header
            while (i < content.size()) {
                size_t spacePos = content.find(' ', i);
                size_t nullPos = content.find('\0', spacePos);
                cout << content.substr(spacePos + 1, nullPos - (spacePos + 1)) << endl;
                i = nullPos + 1 + 20; // Skip SHA
            }

        } else if (command == "write-tree") {
            // Write tree of current directory "."
            string rawSha = writeTree(".");
            cout << shaToHex(rawSha) << endl;

        } else if (command == "commit-tree") {
            // Usage: commit-tree <tree_sha> -p <parent_sha> -m <message>
            // Note: -p <parent_sha> is optional or variable position
            
            if (argc < 4) {
                cerr << "Usage: commit-tree <tree_sha> -m <message> [-p <parent_sha>]\n";
                return EXIT_FAILURE;
            }

            string tree_sha = argv[2];
            string parent_sha;
            string message;

            // Simple argument parsing loop to handle flags (-p, -m)
            for (int i = 3; i < argc; ++i) {
                string arg = argv[i];
                if (arg == "-p" && i + 1 < argc) {
                    parent_sha = argv[++i];
                } else if (arg == "-m" && i + 1 < argc) {
                    message = argv[++i];
                }
            }

            stringstream ss;
            // 1. Tree SHA
            ss << "tree " << tree_sha << "\n";
            
            // 2. Parent SHA (if present)
            if (!parent_sha.empty()) {
                ss << "parent " << parent_sha << "\n";
            }
            
            // 3. Author & Committer
            // Hardcoded as permitted by the challenge requirements
            string authorLine = "author Code Crafter <code@crafters.io> 1700000000 +0000";
            string committerLine = "committer Code Crafter <code@crafters.io> 1700000000 +0000";

            ss << authorLine << "\n";
            ss << committerLine << "\n";
            ss << "\n"; // Blank line is required between header and message
            
            // 4. Message
            ss << message << "\n";

            // Write the commit object using the generic helper
            string rawSha = writeObject("commit", ss.str());
            cout << shaToHex(rawSha) << endl;

        } else {
            cerr << "Unknown command " << command << '\n';
            return EXIT_FAILURE;
        }
    } catch (const exception& e) {
        cerr << e.what() << endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}