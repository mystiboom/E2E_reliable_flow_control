# End-to-End Reliable Flow Control (KTP Protocol)

A custom Reliable Data Transfer protocol built on top of UDP using a sliding window mechanism. This project implements strict sequence ordering, cumulative acknowledgments, and flow control using POSIX Shared Memory (IPC) and multithreading in C.

## 🏗️ Architecture overview

The system mimics a reliable transport layer by sitting between the application and the network. It consists of three main components communicating via shared memory:

1. **The KTP Library (`ksocket.c` / `ksocket.h`):** Provides a standard socket API wrapper (`k_socket`, `k_bind`, `k_sendto`, `k_recvfrom`, `k_close`) for the user applications. It handles buffer management and assigns sequence numbers.
2. **The Background Engine (`initksocket.c`):** A daemon process that acts as the actual network interface. It uses multithreading to manage network I/O:
   - **Thread R (Receiver):** Listens for incoming UDP packets via `select()`, processes ACKs, buffers valid out-of-order packets, and sends cumulative ACKs.
   - **Thread S (Sender):** Periodically scans the shared memory send buffers, transmits unacknowledged packets, and handles timeout retransmissions.
3. **The Applications (`user1.c` / `user2.c`):** Two independent processes that use the KTP library to send and receive files reliably over a simulated lossy connection.

## ⚙️ Key Features
* **Sliding Window Protocol:** Manages in-flight packets and buffer space.
* **Simulated Packet Loss:** Artificially drops packets (configurable probability) to test protocol resilience.
* **Strict Packet Ordering:** Ensures the receiving application only reads data in the exact sequence it was sent.
* **Garbage Collection:** Automatically detects dead application processes and cleans up orphaned sockets.
* **Cumulative ACKs:** Prevents network congestion during timeout events.

## 📂 File Structure
* `ksocket.h` - Data structures, window definitions, and function prototypes.
* `ksocket.c` - Implementation of the KTP API and shared memory attachment.
* `initksocket.c` - The core multithreaded network engine and garbage collector.
* `user1.c` - The sender application (Reads from `test.txt` and transmits).
* `user2.c` - The receiver application (Listens and writes to `out.txt`).
* `Makefiles` - Build scripts for the library, engine, and user applications.

## 🚀 How to Build and Run

### 1. Compilation
Use the provided makefiles to compile the static library, the engine, and the executables:
```bash
make -f Makefile_lib
make -f Makefile_init
make -f Makefile_users