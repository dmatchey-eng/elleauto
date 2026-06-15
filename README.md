# elleauto v1.0

A lightweight, multithreaded cryptocurrency miner built natively for Windows 10. This project targets the memory-hard **Autolykos v2 (Ergo Platform)** proof-of-work algorithm, utilizing an optimized OpenCL compute kernel tailored for the **8GB AMD Radeon RX 580 (Ellesmere)**.

---

## 🏃 Quick Start

You do not need to compile the source code manually to use this miner. 

1. Go to the **Releases** tab on the right side of this repository page.
2. Download the latest `elleauto-rolling-x64.zip` archive.
3. Extract the contents to any folder on your PC. 
   *(Note: `elleauto.exe` and `autolykos.cl` must remain in the same folder).*
4. Add a local folder exclusion in **Windows Defender** to prevent false-positive security blocks.
5. Run `elleauto.exe`, input your Ergo wallet address, select your regional pool, and press Enter.

---

## 🛠️ Key Features & Architecture

* **Vectorized Compute Loop**: Utilizes 256-bit wide `ulong4` vector lanes to saturate the Polaris architecture's 256-bit memory bus width per clock cycle.
* **WDDM Buffer Splitting**: Bypasses strict Windows 10 single-buffer memory caps by dividing the multi-gigabyte DAG dataset into two separate `CL_MEM_READ_ONLY` blocks.
* **Asynchronous Multithreading**: Decoupled into three independent execution branches to ensure zero interface or compute throttling:
  1. *Main Thread*: Renders a static, real-time console dashboard using native Windows Virtual Terminal Processing.
  2. *Network Thread*: Runs a low-overhead Winsock2 TCP stream socket listening for live Stratum pool changes.
  3. *GPU Thread*: Manages low-latency OpenCL command queues and execution blocks.

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
*The build script will automatically generate the required linker files and copy `src/autolykos.cl` directly into your output executable directory.*
