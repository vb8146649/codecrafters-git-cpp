#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h> // Required for decompression

using namespace std;

int main(int argc, char *argv[])
{
    // Flush after every cout / cerr
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

        string blob_sha = argv[3];
        
        // 1. Construct the path to the object file
        // Git stores objects in .git/objects/xx/yyyyyyyy...
        string dirName = blob_sha.substr(0, 2);
        string fileName = blob_sha.substr(2);
        filesystem::path filePath = ".git/objects/" + dirName + "/" + fileName;

        if (!filesystem::exists(filePath)) {
            cerr << "Object not found: " << blob_sha << endl;
            return EXIT_FAILURE;
        }

        // 2. Read the compressed file contents
        ifstream file(filePath, ios::binary);
        if (!file.is_open()) {
            cerr << "Failed to open object file." << endl;
            return EXIT_FAILURE;
        }
        
        // Read the entire file into a vector
        vector<char> compressedData((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        file.close();

        // 3. Decompress using Zlib
        // Initialize zlib stream
        z_stream zs;
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        zs.avail_in = compressedData.size();
        zs.next_in = reinterpret_cast<Bytef*>(compressedData.data());

        if (inflateInit(&zs) != Z_OK) {
            cerr << "Failed to initialize zlib." << endl;
            return EXIT_FAILURE;
        }

        // Buffer for decompressed data (we'll resize string dynamically)
        vector<char> buffer(8192);
        string decompressed;
        int ret;

        // Inflate loop
        do {
            zs.avail_out = buffer.size();
            zs.next_out = reinterpret_cast<Bytef*>(buffer.data());

            ret = inflate(&zs, Z_NO_FLUSH);

            if (decompressed.size() < zs.total_out) {
                decompressed.append(buffer.data(), zs.total_out - decompressed.size());
            }

        } while (ret != Z_STREAM_END);

        inflateEnd(&zs);

        // 4. Parse the header: "blob <size>\0<content>"
        // Find the first null byte which separates header from content
        size_t nullPos = decompressed.find('\0');
        if (nullPos == string::npos) {
            cerr << "Invalid blob format." << endl;
            return EXIT_FAILURE;
        }

        // Extract and print content
        // We use string view or substring. Note: print strictly to stdout.
        string content = decompressed.substr(nullPos + 1);
        cout << content;

    } else {
        cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}