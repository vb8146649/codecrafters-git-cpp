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
#include <map>
#include <cstdlib> // Required for system()

using namespace std;
namespace fs = std::filesystem;



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

// --- Helper Functions ---

string typeToString(int type) {
    switch (type) {
        case 1: return "commit";
        case 2: return "tree";
        case 3: return "blob";
        case 4: return "tag";
        case 6: return "ofs_delta";
        case 7: return "ref_delta";
        default: return "unknown";
    }
}

void writeObjectWithSha(const string& content, const string& shaHex) {
    string dirName = shaHex.substr(0, 2);
    string fileName = shaHex.substr(2);
    fs::path dirPath = ".git/objects/" + dirName;
    if (!fs::exists(dirPath)) fs::create_directories(dirPath);

    uLongf compressedSize = compressBound(content.size());
    vector<Bytef> compressedData(compressedSize);
    if (compress(compressedData.data(), &compressedSize, (const Bytef*)content.data(), content.size()) != Z_OK) {
        throw runtime_error("Compression failed");
    }

    ofstream outFile(dirPath / fileName, ios::binary);
    outFile.write((const char*)compressedData.data(), compressedSize);
    outFile.close();
}

// --- DEBUG ENABLED NETWORKING ---

string httpGet(const string& url) {
    cerr << "[DEBUG] GET Request: " << url << endl;
    
    // -f fails on HTTP 404/403 errors explicitly
    string cmd = "curl -f -L -s \"" + url + "\" > response_output";
    int ret = system(cmd.c_str());
    
    if (ret != 0) {
        cerr << "[ERROR] HTTP GET failed. Command returned: " << ret << endl;
        throw runtime_error("HTTP GET failed");
    }
    
    ifstream file("response_output", ios::binary);
    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    cerr << "[DEBUG] Received " << content.size() << " bytes." << endl;
    return content;
}

string httpPost(const string& url, const string& data, const string& contentType) {
    cerr << "[DEBUG] POST Request: " << url << endl;
    
    ofstream reqFile("request_body", ios::binary);
    reqFile.write(data.data(), data.size());
    reqFile.close();

    string cmd = "curl -f -L -s -X POST --data-binary @request_body -H \"Content-Type: " + contentType + "\" \"" + url + "\" > response_output";
    int ret = system(cmd.c_str());

    if (ret != 0) {
        cerr << "[ERROR] HTTP POST failed. Command returned: " << ret << endl;
        throw runtime_error("HTTP POST failed");
    }

    ifstream file("response_output", ios::binary);
    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    cerr << "[DEBUG] Received " << content.size() << " bytes." << endl;
    return content;
}

// --- Packfile Helpers ---

string createPktLine(const string& data) {
    stringstream ss;
    ss << hex << setfill('0') << setw(4) << (data.size() + 4);
    return ss.str() + data;
}

pair<string, size_t> decompressZlibStream(const string& data, size_t offset) {
    z_stream zs = {};
    zs.avail_in = data.size() - offset;
    zs.next_in = (Bytef*)(data.data() + offset);
    if (inflateInit(&zs) != Z_OK) throw runtime_error("zlib init failed");

    char buffer[4096];
    string result;
    int ret;
    do {
        zs.avail_out = sizeof(buffer);
        zs.next_out = (Bytef*)buffer;
        ret = inflate(&zs, Z_NO_FLUSH);
        result.append(buffer, sizeof(buffer) - zs.avail_out);
    } while (ret != Z_STREAM_END && ret != Z_BUF_ERROR && ret != Z_DATA_ERROR);

    size_t consumed = zs.total_in;
    inflateEnd(&zs);
    return {result, consumed};
}

string applyDelta(const string& base, const string& delta) {
    size_t pos = 0;
    size_t srcSize = 0, shift = 0;
    while (pos < delta.size()) {
        unsigned char b = delta[pos++];
        srcSize |= (b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80)) break;
    }
    size_t targetSize = 0; shift = 0;
    while (pos < delta.size()) {
        unsigned char b = delta[pos++];
        targetSize |= (b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80)) break;
    }

    string result;
    result.reserve(targetSize);
    while (pos < delta.size()) {
        unsigned char cmd = delta[pos++];
        if (cmd & 0x80) { // Copy
            size_t copyOff = 0, copySize = 0;
            if (cmd & 0x01) copyOff |= (unsigned char)delta[pos++];
            if (cmd & 0x02) copyOff |= (unsigned char)delta[pos++] << 8;
            if (cmd & 0x04) copyOff |= (unsigned char)delta[pos++] << 16;
            if (cmd & 0x08) copyOff |= (unsigned char)delta[pos++] << 24;
            if (cmd & 0x10) copySize |= (unsigned char)delta[pos++];
            if (cmd & 0x20) copySize |= (unsigned char)delta[pos++] << 8;
            if (cmd & 0x40) copySize |= (unsigned char)delta[pos++] << 16;
            if (copySize == 0) copySize = 0x10000;
            result.append(base.substr(copyOff, copySize));
        } else if (cmd > 0) { // Insert
            result.append(delta.substr(pos, cmd));
            pos += cmd;
        }
    }
    return result;
}

struct PackObject {
    int type;
    string data, sha, baseSha;
    size_t offset, baseOffset = 0;
};

void checkoutRecursive(const string& treeSha, const fs::path& dir) {
    string content = readObject(treeSha);
    size_t nullPos = content.find('\0');
    string body = content.substr(nullPos + 1);

    size_t i = 0;
    while (i < body.size()) {
        size_t spacePos = body.find(' ', i);
        size_t nullPos = body.find('\0', spacePos);
        string mode = body.substr(i, spacePos - i);
        string name = body.substr(spacePos + 1, nullPos - (spacePos + 1));
        string shaRaw = body.substr(nullPos + 1, 20);
        i = nullPos + 1 + 20;

        string entrySha = shaToHex(shaRaw);
        fs::path entryPath = dir / name;

        if (mode == "40000") {
            fs::create_directories(entryPath);
            checkoutRecursive(entrySha, entryPath);
        } else {
            string blobFull = readObject(entrySha);
            string blobData = blobFull.substr(blobFull.find('\0') + 1);
            ofstream out(entryPath, ios::binary);
            out << blobData;
        }
    }
}

// //

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

        } else if (command == "clone") {
            if (argc < 4) return EXIT_FAILURE;
            string url = argv[2], dir = argv[3];

            fs::create_directories(dir);
            fs::current_path(dir);
            fs::create_directories(".git/objects");
            fs::create_directories(".git/refs");

            // 1. Discovery
            cerr << "[DEBUG] Step 1: Fetching Refs..." << endl;
            string refs = httpGet(url + "/info/refs?service=git-upload-pack");
            string line, headSha;
            stringstream ss(refs);
            while(getline(ss, line)) {
                // Debug print capability lines
                // cerr << "[DEBUG] Ref Line: " << line << endl;
                if (line.size() > 44 && (line.find("refs/heads/master") != string::npos || line.find("HEAD") != string::npos)) {
                    // Check if the line starts with the "0000" flush packet
                    if (line.substr(0, 4) == "0000") {
                        headSha = line.substr(8, 40); // Skip 0000 + length (4 bytes)
                    } else {
                        headSha = line.substr(4, 40); // standard packet line, skip length only
                    }
                    
                    if (line.find("refs/heads/master") != string::npos) break;
                }
            }
            
            if(headSha.empty()) {
                cerr << "[FATAL] No HEAD found! Dumping raw refs response:" << endl;
                cerr << refs << endl;
                return EXIT_FAILURE;
            }
            cerr << "[DEBUG] HEAD is at: " << headSha << endl;
            if (headSha.length() > 40) headSha = headSha.substr(0, 40);
            ofstream(".git/HEAD") << "ref: refs/heads/master\n";

            // 2. Request Pack
            cerr << "[DEBUG] Step 2: Requesting Packfile..." << endl;
            // Removed " no-progress" to match the working logic from previous stages
            string req = createPktLine("want " + headSha + " no-progress\n") + "0000" + createPktLine("done\n");
            string packData = httpPost(url + "/git-upload-pack", req, "application/x-git-upload-pack-request");

            size_t pStart = packData.find("PACK");
            if (pStart == string::npos) {
                cerr << "[FATAL] Invalid pack response (No 'PACK' signature)." << endl;
                cerr << "[DEBUG] First 200 bytes of response:" << endl;
                cerr << packData.substr(0, 200) << endl;
                return EXIT_FAILURE;
            }
            packData = packData.substr(pStart);
            cerr << "[DEBUG] Packfile found. Size: " << packData.size() << endl;

            // 3. Parse Pack
            // Correct: Reads Count (bytes 8-11), starts parsing at 12
            size_t pos = 12; // Header is 12 bytes long
            uint32_t numObjs = (unsigned char)packData[8] << 24 | (unsigned char)packData[9] << 16 | (unsigned char)packData[10] << 8 | (unsigned char)packData[11];
            cerr << "[DEBUG] Number of objects: " << numObjs << endl;
            
            vector<PackObject> tempObjs;
            for(uint32_t i=0; i<numObjs; ++i) {
                PackObject obj;
                obj.offset = pos;
                unsigned char b = packData[pos++];
                obj.type = (b >> 4) & 7;
                size_t size = b & 15;
                int shift = 4;
                while(b & 0x80) {
                    b = packData[pos++];
                    size |= (b & 0x7F) << shift;
                    shift += 7;
                }
                
                if (obj.type == 6) { // OFS_DELTA
                    b = packData[pos++];
                    size_t neg = b & 0x7F;
                    while(b & 0x80) { b = packData[pos++]; neg = ((neg+1) << 7) | (b & 0x7F); }
                    obj.baseOffset = obj.offset - neg;
                } else if (obj.type == 7) { // REF_DELTA
                    obj.baseSha = shaToHex(string(packData.data()+pos, 20));
                    pos += 20;
                }
                
                auto [dec, cons] = decompressZlibStream(packData, pos);
                obj.data = dec;
                pos += cons;
                tempObjs.push_back(obj);
            }

            // 4. Resolve Deltas
            cerr << "[DEBUG] Resolving Deltas..." << endl;
            map<string, PackObject> objects;
            map<size_t, string> offToSha;

            for(auto& obj : tempObjs) {
                if(obj.type < 6) {
                    string tStr = typeToString(obj.type);
                    string full = tStr + " " + to_string(obj.data.size()) + '\0' + obj.data;
                    
                    unsigned char hash[20];
                    SHA1((const unsigned char*)full.data(), full.size(), hash);
                    obj.sha = shaToHex(string((char*)hash, 20));

                    writeObjectWithSha(full, obj.sha);
                    objects[obj.sha] = obj;
                    offToSha[obj.offset] = obj.sha;
                }
            }
            
            bool progress = true;
            while(progress) {
                progress = false;
                for(auto& obj : tempObjs) {
                    if(!obj.sha.empty()) continue;
                    
                    string baseSha;
                    if(obj.type == 6 && offToSha.count(obj.baseOffset)) baseSha = offToSha[obj.baseOffset];
                    else if(obj.type == 7) baseSha = obj.baseSha;
                    
                    if(!baseSha.empty() && objects.count(baseSha)) {
                        obj.data = applyDelta(objects[baseSha].data, obj.data);
                        obj.type = objects[baseSha].type;
                        
                        string tStr = typeToString(obj.type);
                        string full = tStr + " " + to_string(obj.data.size()) + '\0' + obj.data;
                        
                        unsigned char hash[20];
                        SHA1((const unsigned char*)full.data(), full.size(), hash);
                        obj.sha = shaToHex(string((char*)hash, 20));

                        writeObjectWithSha(full, obj.sha);
                        objects[obj.sha] = obj;
                        offToSha[obj.offset] = obj.sha;
                        progress = true;
                    }
                }
            }

            // 5. Checkout
            cerr << "[DEBUG] Checking out files..." << endl;
            
            // A. Read the HEAD Commit Object
            string commitObj = readObject(headSha);
            
            // B. Strip the header (commit <size>\0)
            size_t nullPos = commitObj.find('\0');
            string commitContent = commitObj.substr(nullPos + 1);
            
            // C. Parse the commit content to find "tree <SHA>"
            stringstream ss1(commitContent);
            string line1, treeSha;
            while(getline(ss1, line1)) {
                if (line1.substr(0, 5) == "tree ") {
                    treeSha = line1.substr(5, 40); // Extract the SHA hex
                    break;
                }
            }

            // D. Checkout using the specific Tree SHA
            if (!treeSha.empty()) {
                checkoutRecursive(treeSha, ".");
            } else {
                cerr << "[FATAL] Could not find tree in HEAD commit." << endl;
                return EXIT_FAILURE;
            }
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