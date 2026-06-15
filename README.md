# elleauto v1.0

`elleauto` is a high-performance, multithreaded cryptocurrency mining application compiled natively for Windows 10. It is specifically engineered to execute the memory-hard Autolykos v2 Proof-of-Work algorithm (Ergo Platform) utilizing **The Autolykos v2 OpenCL Hardware Kernel** optimized for the **AMD Radeon RX 580 8GB (Ellesmere)** Polaris architecture.

---

## 🚀 Quick Start Instructions

If you are just interested in running the miner on your Windows 10 rig, you do not need to compile the source code manually. Follow these deployment steps:

1. **Download the Package**: Navigate to the **Releases** tab on the right side of this repository page and download the latest `elleauto-rolling-x64.zip` archive.
2. **Extract Files**: Extract the compressed folder contents to a directory of your choice on your local machine. 
   * *Note: Ensure both `elleauto.exe` and `autolykos.cl` remain in the **same folder** together.*
3. **Configure Windows Defender (False Positive)**: Custom cryptographic applications are frequently flagged as false positives by antivirus scanners. Add a local folder exclusion in Windows Security to prevent the OS from interrupting the application.
4. **Launch the Miner**: Double-click `elleauto.exe`, paste your public Ergo wallet receiving address, select your preferred regional mining pool from the interactive menu, and let the hardware loop initialize.

---

## 🛠️ Architecture & Optimization Technical Specs

* **Vectorized VRAM Saturation (`ulong4`)**: Engineered utilizing 256-bit wide `ulong4` vectorized inputs within the OpenCL compute kernel. This strategy saturates the Polaris architecture's 256-bit memory bus width completely per clock cycle, maximizing global dataset processing speeds.
* **WDDM Allocation Bypass (Split DAG)**: Windows 10 imposes strict allocation caps on single memory buffers. `elleauto` addresses this constraint by splitting the massive multi-gigabyte Autolykos DAG into distinct sub-allocations (`CL_MEM_READ_ONLY`) across VRAM boundaries, avoiding runtime allocations crashes.
* **Wavefront-Aligned Asynchronous Execution**: The thread dispatch matrix leverages a global-to-local work ratio mapped directly to AMD's native 64-thread execution Wavefront configuration layout, eliminating instruction divergence.
* **Deduplicated Multithreaded Topology**: Built on three completely asynchronous processing branches executing concurrently:
  1. *Main UI Thread*: Drives an ANSI-escaped, responsive console telemetry performance dashboard.
  2. *Stratum Network Thread*: Manages an uninterrupted background Winsock2 TCP stream socket listening dynamically for new block updates.
  3. *GPU Computing Orchestrator*: Dictates low-latency OpenCL resource queue dispatch passes without locking the host environment.

---

## 📁 Repository Directory Structure

```text
elleauto/
├── .github/
│   └── workflows/
│       └── windows-build.yml   <-- Continuous rolling MSBuild CI deployment script
├── CMakeLists.txt              <-- Multi-source compiler configuration layout
├── include/
│   └── CL/                     <-- Isolated Khronos OpenCL development headers
│       ├── cl.h
│       ├── cl_platform.h
│       └── ... (and all auxiliary system environment headers)
└── src/
    ├── autolykos.cl            <-- Vectorized mathematical hardware execution kernel
    ├── main.cpp                <-- Orchestration layer, Winsock network stack, and user UI
    ├── opencl_manager.cpp      <-- AMD Ellesmere platform discovery and context manager
    ├── opencl_stub.cpp         <-- Virtual system stubs handling cloud compilation environments
    └── stratum_parser.cpp      <-- Low-overhead JSON-RPC protocol token parsing hook
```

---

## ⚙️ Compilation & Local Building

To build the application from source code on a local Windows development machine environment, make sure you have CMake and Visual Studio 2022 (MSVC tools) configured, then execute the following console commands:

```bash
# Initialize build directory tracking trees
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -B build -S .

# Compile optimized release binary targets
cmake --build build --config Release
```
The compiler engine will link the source segments, map the standard library definitions, and duplicate the `src/autolykos.cl` kernel asset file right into your output compilation directory automatically.

---

## ⚖️ License
This project is open-source. Please review the code layout structures and verify your local hardware constraints before deploying execution runs.
