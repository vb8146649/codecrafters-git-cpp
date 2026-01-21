# Build Your Own Git (C++)

A robust, from-scratch implementation of Git written in C++. This project creates a functional Git client capable of reading low-level Git objects, creating commits, and even cloning repositories from remote servers using the Git Smart HTTP protocol.

Built as part of the [CodeCrafters](https://codecrafters.io) "Build Your Own Git" challenge.

## 🚀 Features

This client implements the core "plumbing" commands of Git:

* **`init`**: Initializes a new `.git` directory structure.
* **`cat-file -p <sha>`**: Reads and decompresses Git objects (blobs) and prints their content.
* **`hash-object -w <file>`**: Hashes a file, compresses it, and stores it as a blob in `.git/objects`.
* **`ls-tree --name-only <sha>`**: Parses a Tree object and lists the file names contained within.
* **`write-tree`**: Recursively scans the current directory and creates a Tree object (snapshot).
* **`commit-tree`**: Creates a Commit object pointing to a Tree and a Parent Commit.
* **`clone <url> <dir>`**: Clones a public repository. This includes:
    * Performing the HTTP handshake (Discovery & Negotiation).
    * Downloading the binary **Packfile**.
    * Parsing variable-length integers and binary headers.
    * **Delta Resolution**: Reconstructing files from `OBJ_REF_DELTA` and `OBJ_OFS_DELTA` diffs.

## 🛠️ Prerequisites & Dependencies

To build and run this project, you need:

1.  **C++ Compiler** (g++ or clang++) supporting C++17 via `<filesystem>`.
2.  **Zlib**: For compressing/decompressing Git objects (`-lz`).
3.  **OpenSSL**: For SHA-1 hashing (`-lcrypto` or `-lssl`).
4.  **Curl (CLI)**: The program uses `system("curl ...")` for network requests. Ensure `curl` is installed and in your PATH.

## ⚙️ Building

Compile the project using `g++`. You must link against `zlib` and `libcrypto`.

```bash
g++ -o git main.cpp -lz -lcrypto
```
## 📖 Usage
### 1. Initialize a Repository
```Bash
./git init
```
### 2. Hash & Write a File
Stores a file in the Git database and returns its SHA-1 hash.

```Bash
./git hash-object -w <filename>
```
### 3. Read an Object
Prints the contents of a blob object.

```Bash
./git cat-file -p <blob_sha>
```
### 4. Write a Tree (Snapshot)
Captures the current directory structure as a Git Tree object.

```Bash
./git write-tree
```
### 5. Create a Commit
Creates a commit using a Tree SHA and a Parent Commit SHA.

```Bash
./git commit-tree <tree_sha> -p <parent_sha> -m "Commit message"
```
### 6. Clone a Repository
Downloads a repository from a remote URL into a target directory.

```Bash
./git clone <url> <target_directory>
```
### 🧩 Architecture Notes

#### The Clone Implementation
The `clone` command is the most complex part of this project. Instead of relying on high-level libraries like `libgit2`, this solution manually implements the **Git Smart HTTP Protocol** to interact directly with the remote server.

1.  **Discovery (Handshake)**
    * Sends a `GET` request to `/info/refs?service=git-upload-pack`.
    * Parses the response to find the SHA-1 hash of `HEAD` (the current state of the remote repository).

2.  **Negotiation**
    * Constructs a custom "want" packet requesting the specific `HEAD` commit.
    * Sends a `POST` request to `/git-upload-pack`.
    * *Note:* Capabilities like `no-progress` are excluded to keep the implementation minimal and compliant with simple server responses.

3.  **Packfile Parsing**
    * Reads the binary **Packfile** stream from the response.
    * Validates the 12-byte header: `PACK` signature, version number, and object count.
    * Decompresses the stream of Git objects using Zlib.

4.  **Delta Patching**
    * Git optimizes bandwidth by sending "deltas" (binary diffs) for similar files instead of full copies.
    * **Strategy**:
        * Buffers all received objects in memory.
        * Iteratively resolves `OBJ_REF_DELTA` and `OBJ_OFS_DELTA` objects.
        * Applies binary patch instructions (Copy/Insert) against base objects until every file is fully reconstructed and ready for checkout.