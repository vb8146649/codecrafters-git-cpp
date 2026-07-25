# Secure-Git: Custom Version Control System in C++

A high-performance, from-scratch implementation of Git written in C++. This project builds a functional VCS client capable of low-level object manipulation, parsing binary packfiles, resolving delta-compressed objects, and executing a custom cryptographic signing layer to ensure commit authenticity and tamper-proofing.

---

## 🚀 Key Features

* **Low-Level Object Storage**: Implements blob creation, tree serialization, and commit generation, using SHA-1 hashing and Zlib compression.
* **Smart HTTP Client**: Connects directly to Git remotes using the Git Smart HTTP Protocol to perform repository cloning.
* **Packfile Parsing**: Parses binary packfiles, decoding variable-length integers and resolving delta-compressed objects (`OBJ_REF_DELTA` and `OBJ_OFS_DELTA`).
* **Cryptographic Security Layer**: Custom RSA public-key cryptography integration to sign commits and verify history integrity.

---

## 🔒 The Cryptographic Security Layer (Tamper-Proof VCS)

To prevent attackers from rewriting repository history and computing new SHAs, this VCS implements an asymmetric cryptographic signing and verification flow:

1. **Key Generation**: Generates 2048-bit RSA public/private key pairs and stores them securely in PEM format.
2. **State Serialization**: Gathers commit metadata (tree, parent, author, committer, and message) into a canonical text representation.
3. **Commit Hashing**: Hashes the serialized state using **SHA-256** to generate a tamper-proof commit fingerprint.
4. **Asymmetric Signing**: Encrypts the SHA-256 fingerprint using the user's RSA private key. The resulting digital signature is appended directly to the commit object under a custom `gpgsig` header.
5. **The Verification Loop**: 
   - Reads the commit object, extracts the `gpgsig` signature, and reconstructs the unsigned metadata.
   - Re-computes the SHA-256 hash of the metadata.
   - Decrypts the signature using the author's public key and verifies that the decrypted fingerprint matches the computed hash. Any alteration to the parent commits, files, author info, or commit message fails verification immediately.

---

## 🛠️ Architecture Notes

### Binary Packfile & Delta Patching
Bandwidth-efficient cloning is achieved by manually parsing the Git binary packfile format:
* Extracts pack headers containing object counts.
* Sequentially decompresses compressed objects using Zlib.
* Resolves delta dependencies by applying binary patching instructions (Copy/Insert commands) against base objects dynamically.

---

## ⚙️ Build Instructions

### Prerequisites
* **C++ Compiler** supporting C++23.
* **Zlib** for compression (`-lz`).
* **OpenSSL (v3.0+)** for cryptographic signing and hashing (`-lcrypto`).

### Compilation
Build using CMake:
```bash
cmake -B build -S .
cmake --build ./build
```

---

## 📖 Usage Guide

### 1. Initialize a Repository
Initializes a new directory structure for VCS objects and references.
```bash
./build/git init
```

### 2. Generate RSA Key Pair
Generates private and public key files at `.git/private_key.pem` and `.git/public_key.pem` for signing and verification.
```bash
./build/git keygen
```

### 3. Write Objects (Blobs and Trees)
Hash a file and store it in the database:
```bash
./build/git hash-object -w <file_path>
```
Recursively write the current directory to a Git tree object:
```bash
./build/git write-tree
```

### 4. Create and Sign a Commit
If `.git/private_key.pem` is found, the commit will be automatically signed. Alternatively, specify the path to a private key:
```bash
./build/git commit-tree <tree_sha> -m "My commit message" [--sign <private_key_path>]
```

### 5. Verify Commit Integrity
Validates that the commit signature matches the public key and that the commit has not been altered:
```bash
./build/git verify-commit <commit_sha> [--key <public_key_path>]
```

### 6. Clone a Repository
Downloads a repository from a remote URL over HTTP:
```bash
./build/git clone <url> <target_directory>
```