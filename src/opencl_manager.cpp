#include "miner_types.h"
#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <atomic>
#include <thread>
#include <chrono>

extern std::atomic<int> g_dag_progress;
extern std::atomic<bool> g_is_dag_building;
extern std::atomic<bool> is_mining_running;

cl_context g_clContext = nullptr;
cl_command_queue g_clQueue = nullptr;
cl_program g_clProgram = nullptr;
cl_kernel g_miningKernel = nullptr;

cl_mem g_dagBufferPart1 = nullptr;
cl_mem g_dagBufferPart2 = nullptr;
cl_mem g_devNonces = nullptr;
cl_mem g_devCounter = nullptr;

void applyAmdDriverFixes() {
    _putenv_s("GPU_MAX_ALLOC_PERCENT", "100");
    _putenv_s("GPU_SINGLE_ALLOC_PERCENT", "100");
    _putenv_s("GPU_MAX_HEAP_SIZE", "100");
    _putenv_s("GPU_USE_SYNC_OBJECTS", "1");
}

std::string readKernelSource(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unsigned long long parseHexSlice64(const std::string& hex_slice);
HostUlong4 parseHeaderHashToUlong4(const std::string& raw_hex);

    // Ensure it is a valid 256-bit hex representation (64 characters total)
    if (clean_hex.length() != 64) {
        std::cerr << "[PARSER ERROR] Invalid header hash length: " << clean_hex.length() << " (Expected 64)\n";
        return out_vector; 
    }

    // 2. Chop the 64-character big-endian string into four 16-character chunks
    std::string chunk0 = clean_hex.substr(0, 16);  // Most significant 8 bytes
    std::string chunk1 = clean_hex.substr(16, 16); 
    std::string chunk2 = clean_hex.substr(32, 16); 
    std::string chunk3 = clean_hex.substr(48, 16); // Least significant 8 bytes

    // 3. Map big-endian chunks to little-endian vector components
    // To match how the OpenCL compiler maps .s0 through .s3 indexing vectors:
    out_vector.s0 = parseHexSlice64(chunk0);
    out_vector.s1 = parseHexSlice64(chunk1);
    out_vector.s2 = parseHexSlice64(chunk2);
    out_vector.s3 = parseHexSlice64(chunk3);

    return out_vector;
}
bool initOpenCL() {
    applyAmdDriverFixes();
    
    cl_uint platformCount = 0;
    clGetPlatformIDs(0, nullptr, &platformCount); 
    if (platformCount == 0) {
        std::cerr << "\n[GPU ERROR] No OpenCL platforms reported by your AMD driver.\n";
        system("pause");
        return false;
    }

    std::vector<cl_platform_id> platforms(platformCount);
    clGetPlatformIDs(platformCount, platforms.data(), nullptr);
    cl_device_id targetDevice = nullptr;
    bool foundAMD = false;

    for (auto platform : platforms) {
        size_t size = 0;
        clGetPlatformInfo(platform, CL_PLATFORM_NAME, 0, nullptr, &size);
        std::vector<char> platName(size);
        clGetPlatformInfo(platform, CL_PLATFORM_NAME, size, platName.data(), nullptr);
        std::string platStr(platName.data());

        std::cout << "[GPU Debug] Found Platform: " << platStr << "\n";

        if (platStr.find("AMD") != std::string::npos || platStr.find("Advanced Micro Devices") != std::string::npos) {
            cl_uint deviceCount = 0;
            clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &deviceCount);
            
            if (deviceCount == 0) continue;

            std::vector<cl_device_id> devices(deviceCount);
            clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, deviceCount, devices.data(), nullptr);

            for (auto device : devices) {
                clGetDeviceInfo(device, CL_DEVICE_NAME, 0, nullptr, &size);
                std::vector<char> devName(size);
                clGetDeviceInfo(device, CL_DEVICE_NAME, size, devName.data(), nullptr);
                std::string devStr(devName.data());

                std::cout << "[GPU Debug] Found Device: " << devStr << "\n";

                // Target standard Polaris / Ellesmere architecture profiles or general fallback
                if (devStr.find("580") != std::string::npos || devStr.find("Ellesmere") != std::string::npos || devStr.find("Radeon") != std::string::npos) {
                    targetDevice = device;
                    foundAMD = true;
                    std::cout << "[GPU Debug] Match found! Targeting: " << devStr << "\n";
                    break;
                }
            }
        }
        if (foundAMD) break;
    }

    if (!targetDevice) {
        std::cerr << "\n[GPU ERROR] No compatible AMD RX 580 or Radeon GPUs found in platform enumeration.\n";
        system("pause");
        return false;
    }

    cl_int err;
    g_clContext = clCreateContext(nullptr, 1, &targetDevice, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "\n[GPU ERROR] clCreateContext failed. Code: " << err << "\n";
        system("pause");
        return false;
    }

    g_clQueue = clCreateCommandQueueWithProperties(g_clContext, targetDevice, nullptr, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "\n[GPU ERROR] clCreateCommandQueueWithProperties failed. Code: " << err << "\n";
        system("pause");
        return false;
    }

    std::string kernelSource = readKernelSource("autolykos.cl"); 
    if (kernelSource.empty()) {
        std::cerr << "\n[GPU ERROR] Could not read 'autolykos.cl'. Make sure it's in the same folder as the exe!\n";
        system("pause");
        return false;
    }

    const char* sourcePtr = kernelSource.c_str();
    size_t sourceLen = kernelSource.length();
    g_clProgram = clCreateProgramWithSource(g_clContext, 1, &sourcePtr, &sourceLen, &err);

    err = clBuildProgram(g_clProgram, 1, &targetDevice, "-cl-mad-enable -cl-fast-relaxed-math", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize;
        clGetProgramBuildInfo(g_clProgram, targetDevice, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> buildLog(logSize);
        clGetProgramBuildInfo(g_clProgram, targetDevice, CL_PROGRAM_BUILD_LOG, logSize, buildLog.data(), nullptr);
        std::cerr << "\n[GPU COMPILER ERROR] Kernel compilation failed:\n" << buildLog.data() << "\n";
        system("pause");
        return false;
    }

    g_miningKernel = clCreateKernel(g_clProgram, "autolykos_search", &err);
    if (err != CL_SUCCESS) {
        std::cerr << "\n[GPU ERROR] clCreateKernel failed. Code: " << err << "\n";
        system("pause");
        return false;
    }
    
    unsigned int host_counter_placeholder = 0;
    unsigned long long host_nonce_placeholder[20] = {0}; // Double size to hold both Nonce and Solution pairs
    g_devNonces = clCreateBuffer(g_clContext, CL_MEM_WRITE_ONLY, sizeof(host_nonce_placeholder), nullptr, &err);
    g_devCounter = clCreateBuffer(g_clContext, CL_MEM_READ_WRITE, sizeof(host_counter_placeholder), nullptr, &err);

    if (err != CL_SUCCESS) {
        std::cerr << "\n[GPU ERROR] Output buffer pre-allocation failed. Code: " << err << "\n";
        system("pause");
        return false;
    }

    return true;
}

bool allocateAndBuildVectorDag(size_t total_elements_count) {
    g_is_dag_building = true;
    cl_int err1, err2;

    // 💥 FIX 2: Ensure total_elements_count represents the number of true 32-byte segments
    size_t half_elements = total_elements_count / 2;
    size_t half_bytes_size = half_elements * sizeof(HostUlong4); // Exactly 32 bytes per element

    std::cout << "[GPU] Forcing VRAM buffer creation allocation blocks (" 
              << (half_bytes_size * 2) / (1024 * 1024) << " MB total)...\n";

    g_dagBufferPart1 = clCreateBuffer(g_clContext, CL_MEM_READ_ONLY, half_bytes_size, nullptr, &err1);
    g_dagBufferPart2 = clCreateBuffer(g_clContext, CL_MEM_READ_ONLY, half_bytes_size, nullptr, &err2);

    if (err1 != CL_SUCCESS || err2 != CL_SUCCESS) {
        std::cerr << "\n[GPU ERROR] DAG VRAM Allocation failed. Part1: " << err1 << " Part2: " << err2 << "\n";
        g_is_dag_building = false;
        return false;
    }

    // 💥 FIX 3: Initialize the host generation memory using your new 32-byte layout chunks
    std::vector<HostUlong4> host_chunk(half_elements);
    for(size_t i = 0; i < half_elements; ++i) {
        unsigned long long dummy_val = (i * 0xFFFFFFFFFFFFFFFFULL) ^ (half_elements + i);
        // Fill out all four 64-bit lanes of the ulong4 vector
        host_chunk[i].s0 = dummy_val;
        host_chunk[i].s1 = dummy_val ^ 0x123456789ABCDEF0ULL; 
        host_chunk[i].s2 = dummy_val + i;
        host_chunk[i].s3 = dummy_val | 0x0F0F0F0F0F0F0F0FULL;
    }
    
    g_dag_progress = 25;
    clEnqueueWriteBuffer(g_clQueue, g_dagBufferPart1, CL_TRUE, 0, half_bytes_size, host_chunk.data(), 0, nullptr, nullptr);
    
    g_dag_progress = 75;
    clEnqueueWriteBuffer(g_clQueue, g_dagBufferPart2, CL_TRUE, 0, half_bytes_size, host_chunk.data(), 0, nullptr, nullptr);
    
    std::cout << "[GPU] Flashing queues. Forcing hardware allocation sweep across memory lines...\n";
    clFinish(g_clQueue); 
    
    g_dag_progress = 100;
    g_is_dag_building = false;
    std::cout << "[GPU SUCCESS] Hardware initialization check complete.\n";
    return true;
}

// 💥 UPDATED INTERFACE: Expects hex-parsed structural representations for true 256-bit alignments
void runMiningLoop(unsigned long long initial_nonce, HostUlong4 target_difficulty, HostUlong4 header_hash_input) {
    cl_int err;
    unsigned long long nonce_iterator = initial_nonce;
    unsigned long long half_elements_vector = 200000000; 

    unsigned int reset_counter = 0;
    clEnqueueWriteBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(reset_counter), &reset_counter, 0, nullptr, nullptr);
    
    // Bind base pointer configurations
    clSetKernelArg(g_miningKernel, 0, sizeof(cl_mem), &g_dagBufferPart1);
    clSetKernelArg(g_miningKernel, 1, sizeof(cl_mem), &g_dagBufferPart2);
    clSetKernelArg(g_miningKernel, 2, sizeof(unsigned long long), &half_elements_vector);
    
    // 💥 FIX 2: Correctly pass the 32-byte ulong4 vector arguments to the kernel
    clSetKernelArg(g_miningKernel, 3, sizeof(HostUlong4), &header_hash_input);
    clSetKernelArg(g_miningKernel, 4, sizeof(HostUlong4), &target_difficulty); 
    clSetKernelArg(g_miningKernel, 5, sizeof(unsigned long long), &nonce_iterator);
    clSetKernelArg(g_miningKernel, 6, sizeof(cl_mem), &g_devNonces);
    clSetKernelArg(g_miningKernel, 7, sizeof(cl_mem), &g_devCounter);

    size_t global_work_size = 64 * 1024;
    size_t local_work_size = 256;

    extern std::atomic<bool> is_current_job_valid;

    while (is_mining_running && is_current_job_valid && !g_is_dag_building) {
        clSetKernelArg(g_miningKernel, 5, sizeof(unsigned long long), &nonce_iterator);

        err = clEnqueueNDRangeKernel(g_clQueue, g_miningKernel, 1, nullptr, &global_work_size, &local_work_size, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) break;

        unsigned int found_count = 0;
        clEnqueueReadBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(found_count), &found_count, 0, nullptr, nullptr);

        if (found_count > 0) {
            if (found_count > 10) found_count = 10; // Match kernel hardware allocation boundaries

            // 💥 FIX 3: Clear space for 5 elements per share found (1 nonce + 4 lanes of final_hash)
            unsigned long long solved_data[50] = {0}; 
            size_t bytes_to_read = found_count * 5 * sizeof(unsigned long long);
            
            clEnqueueReadBuffer(g_clQueue, g_devNonces, CL_TRUE, 0, bytes_to_read, solved_data, 0, nullptr, nullptr);
            clEnqueueWriteBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(reset_counter), &reset_counter, 0, nullptr, nullptr);
            
            extern std::string g_current_job_id;
            
            // 💥 UPDATED DECLARATION: Accepts the found nonce alongside its complete 256-bit solution vector
            void submitShare(const std::string& job_id, unsigned long long found_nonce, HostUlong4 solution_hash);
            
            for (unsigned int i = 0; i < found_count; i++) {
                size_t base_index = i * 5;
                unsigned long long nonce = solved_data[base_index];
                
                HostUlong4 sol;
                sol.s0 = solved_data[base_index + 1];
                sol.s1 = solved_data[base_index + 2];
                sol.s2 = solved_data[base_index + 3];
                sol.s3 = solved_data[base_index + 4];
                
                submitShare(g_current_job_id, nonce, sol);
            }
        }

        nonce_iterator += global_work_size;
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); 
    }
}

void shutdownOpenCL() {
    if (g_miningKernel) clReleaseKernel(g_miningKernel);
    if (g_dagBufferPart1) clReleaseMemObject(g_dagBufferPart1);
    if (g_dagBufferPart2) clReleaseMemObject(g_dagBufferPart2);
    if (g_devNonces) clReleaseMemObject(g_devNonces);
    if (g_devCounter) clReleaseMemObject(g_devCounter);
    if (g_clProgram) clReleaseProgram(g_clProgram);
    if (g_clQueue) clReleaseCommandQueue(g_clQueue);
    if (g_clContext) clReleaseContext(g_clContext);
}

