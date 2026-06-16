# elleauto v1.0.6

A lightweight, multithreaded cryptocurrency miner built natively for Windows 10. This project targets the memory-hard **Autolykos v2 (Ergo Platform)** proof-of-work algorithm, utilizing an optimized OpenCL compute kernel tailored explicitly for the **8GB AMD Radeon RX 580 (Ellesmere)**.

---

## ⚠️ Important System Warning

> **FALSE POSITIVE ALERTS:** Because this application contains custom cryptographic hashing code compiled from scratch on cloud runners, Windows Defender and third-party antivirus suites will flag `elleauto.exe` as a security exception (typically categorized as a generic *CoinMiner* or *Unwanted Application*). 
> 
> **Action Required:** Before running the software, you must create a local directory exclusion in **Windows Security / Defender Settings** for the folder where you unpack the binary. Failure to add an exclusion will cause the operating system to silently delete or block the executable upon launch.

---

## 🏃 Quick Start

You do not need to compile the source code manually to use this miner. 

1. Go to the **Releases** tab on the right side of this repository page.
2. Download the latest `elleauto-rolling-x64.zip` archive.
3. Extract the contents to your excluded folder. 
   *(Note: `elleauto.exe` and `autolykos.cl` must remain in the exact same directory).*
4. Run `elleauto.exe`, paste your Ergo wallet address, select your regional pool, and press Enter.

---

## 🛠️ Key Features & Architecture

* **Vectorized Compute Loop**: Utilizes 256-bit wide `ulong4` vector lanes to fully saturate the Polaris architecture's 256-bit memory bus width per clock cycle.
* **WDDM Buffer Splitting**: Bypasses strict Windows 10 single-buffer allocation limits by dividing the multi-gigabyte DAG dataset into two separate `CL_MEM_READ_ONLY` blocks.
* **Asynchronous Multithreading**: Decoupled into three completely independent execution branches to ensure zero interface or compute throttling:
  1. *Main UI Thread*: Renders a static, real-time dashboard panel using native Windows Virtual Terminal Processing.
  2. *Network Socket Thread*: Runs a low-overhead Winsock2 TCP stream socket listening for live Stratum pool changes.
  3. *GPU Engine Thread*: Manages low-latency OpenCL command queues and execution blocks.
* **Managed Network Diagnostics**: Generates a localized `network_log.txt` on startup to verify stratum handshakes. The log automatically truncates (wipes old data) on launch and enforces a 150-packet write ceiling to permanently prevent storage bloat.

---

## 📁 Repository Structure

```text
elleauto/
├── .github/workflows/
│   └── windows-build.yml   <-- Automated rolling release CI pipeline
├── include/CL/              <-- Standard Khronos OpenCL development headers
├── lib/
│   └── OpenCL.def          <-- Module definition map for headless cloud builds
├── src/
│   ├── autolykos.cl        <-- Vectorized OpenCL compute kernel
│   ├── main.cpp            <-- Thread orchestrator, UI, and network stack
│   ├── opencl_manager.cpp  <-- AMD platform discovery and runtime memory controller
│   └── stratum_parser.cpp  <-- Comma-delimited JSON-RPC network stream tokenizer
└── CMakeLists.txt          <-- Standalone build configuration script
```

---

## ⚙️ Building from Source

To compile the miner manually from source, ensure you have CMake and Visual Studio 2022 (MSVC C++ tools) installed, then run the following commands in your terminal:

```bash
# Configure the compiler and target directories
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -B build -S .

# Build the optimized executable binary
cmake --build build --config Release
```
*The build script will automatically generate the required linker files from the definition map and copy `src/autolykos.cl` directly into your output executable directory.*

---

## ⚖️ License
This project is open-source. Please review the code layout structures and verify your local hardware constraints before deploying execution runs.
