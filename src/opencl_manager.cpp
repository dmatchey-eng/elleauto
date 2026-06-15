#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <atomic>
#include <thread>
#include <chrono>

// Shared atomic variables for UI synchronization
extern std::atomic<int> g_dag_progress;
extern std::atomic<bool> g_is_dag_building;
extern std::atomic<bool> is_mining_running;

// Global OpenCL handles
cl_context g_clContext = nullptr;
cl_command_queue g_clQueue = nullptr;
cl_program g_clProgram = nullptr;
cl_kernel g_miningKernel = nullptr;

cl_mem g_dagBufferPart1 = nullptr;
cl_mem g_dagBufferPart2 = nullptr;
cl_mem g_devNonces = nullptr;
cl_mem g_devCounter = nullptr;

// Overrides Windows WDDM video driver throttling behaviors
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

bool initOpenCL() {
    applyAmdDriverFixes();
    
    cl_uint platformCount;
    clGetPlatformIDs(0, nullptr, &platformCount);
    if (platformCount == 0) return false;

    std::vector<cl_platform_id> platforms(platformCount);
    clGetPlatformIDs(platformCount, platforms.data(), nullptr);

    cl_device_id targetDevice = nullptr;
    bool foundAMD = false;

    for (auto platform : platforms) {
        char platformName[256];
        clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platformName), platformName, nullptr);
        std::string platStr(platformName);

        if (platStr.find("AMD") != std::string::npos || platStr.find("Advanced Micro Devices") != std::string::npos) {
            cl_uint deviceCount;
            clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &deviceCount);
            std::vector<cl_device_id> devices(deviceCount);
            clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, deviceCount, devices.data(), nullptr);

            for (auto device : devices) {
                char deviceName[256];
                clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(deviceName), deviceName, nullptr);
                std::string devStr(deviceName);

                if (devStr.find("580") != std::string::npos || devStr.find("Ellesmere") != std::string::npos) {
                    targetDevice = device;
                    foundAMD = true;
                    break;
                }
            }
        }
        if (foundAMD) break;
    }

    if (!targetDevice) return false;

    cl_int err;
    g_clContext = clCreateContext(nullptr, 1, &targetDevice, nullptr, nullptr, &err);
    g_clQueue = clCreateCommandQueueWithProperties(g_clContext, targetDevice, nullptr, &err);

    std::string kernelSource = readKernelSource("src/autolykos.cl");
    if (kernelSource.empty()) return false;

    const char* sourcePtr = kernelSource.c_str();
    size_t sourceLen = kernelSource.length();
    g_clProgram = clCreateProgramWithSource(g_clContext, 1, &sourcePtr, &sourceLen, &err);

    // Optimized compiler options targeting Polaris compute engine layout arrays
    err = clBuildProgram(g_clProgram, 1, &targetDevice, "-cl-mad-enable -cl-fast-relaxed-math", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize;
        clGetProgramBuildInfo(g_clProgram, targetDevice, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> buildLog(logSize);
        clGetProgramBuildInfo(g_clProgram, targetDevice, CL_PROGRAM_BUILD_LOG, logSize, buildLog.data(), nullptr);
        std::cerr << "[COMPILER LOG]\n" << buildLog.data() << "\n";
        return false;
    }

    g_miningKernel = clCreateKernel(g_clProgram, "autolykos_search", &err);
    
    // Allocate shared outputs once to minimize garbage collection latency
    unsigned long long host_nonce_placeholder = 0;
    unsigned int host_counter_placeholder = 0;
    g_devNonces = clCreateBuffer(g_clContext, CL_MEM_WRITE_ONLY, sizeof(host_nonce_placeholder), nullptr, &err);
    g_devCounter = clCreateBuffer(g_clContext, CL_MEM_READ_WRITE, sizeof(host_counter_placeholder), nullptr, &err);

    return (err == CL_SUCCESS);
}

// Memory setup tailored to vectorized 256-bit wide ulong4 lanes
bool allocateAndBuildVectorDag(size_t total_elements_count) {
    g_is_dag_building = true;
    cl_int err1, err2;

    // Split size accounting for ulong4 layouts (4 elements per vector group)
    size_t half_elements = total_elements_count / 2;
    size_t half_bytes_size = half_elements * sizeof(unsigned long long);

    g_dagBufferPart1 = clCreateBuffer(g_clContext, CL_MEM_READ_ONLY, half_bytes_size, nullptr, &err1);
    g_dagBufferPart2 = clCreateBuffer(g_clContext, CL_MEM_READ_ONLY, half_bytes_size, nullptr, &err2);

    if (err1 != CL_SUCCESS || err2 != CL_SUCCESS) {
        g_is_dag_building = false;
        return false;
    }

    // Allocation pipeline feeding progress directly to the UI layer
    std::vector<unsigned long long> host_chunk(half_elements, 0x123456789ABCDEFULL);
    
    g_dag_progress = 25;
    clEnqueueWriteBuffer(g_clQueue, g_dagBufferPart1, CL_TRUE, 0, half_bytes_size, host_chunk.data(), 0, nullptr, nullptr);
    
    g_dag_progress = 75;
    clEnqueueWriteBuffer(g_clQueue, g_dagBufferPart2, CL_TRUE, 0, half_bytes_size, host_chunk.data(), 0, nullptr, nullptr);
    
    g_dag_progress = 100;
    g_is_dag_building = false;
    return true;
}

void runMiningLoop(unsigned long long initial_nonce, unsigned long long difficulty_target, unsigned long long header_hash_input) {
    cl_int err;
    unsigned long long nonce_iterator = initial_nonce;
    
    // Elements bound adjusted for vector boundaries inside the kernel signature
    unsigned long long half_elements_vector = 200000000; 

    // Reset solution counters on hardware memory space
    unsigned int reset_counter = 0;
    clEnqueueWriteBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(reset_counter), &reset_counter, 0, nullptr, nullptr);

    // Bind optimized kernel targets 
    clSetKernelArg(g_miningKernel, 0, sizeof(cl_mem), &g_dagBufferPart1);
    clSetKernelArg(g_miningKernel, 1, sizeof(cl_mem), &g_dagBufferPart2);
    clSetKernelArg(g_miningKernel, 2, sizeof(unsigned long long), &half_elements_vector);
    clSetKernelArg(g_miningKernel, 3, sizeof(unsigned long long), &header_hash_input);
    clSetKernelArg(g_miningKernel, 4, sizeof(unsigned long long), &difficulty_target);
    clSetKernelArg(g_miningKernel, 5, sizeof(unsigned long long), &nonce_iterator);
    clSetKernelArg(g_miningKernel, 6, sizeof(cl_mem), &g_devNonces);
    clSetKernelArg(g_miningKernel, 7, sizeof(cl_mem), &g_devCounter);

    // Execution layout matching Wavefront dimensions (multiples of 64)
    size_t global_work_size = 64 * 1024;
    size_t local_work_size = 256;

    while (is_mining_running && !g_is_dag_building) {
        // Update current iterator index argument rapidly per loop cycle
        clSetKernelArg(g_miningKernel, 5, sizeof(unsigned long long), &nonce_iterator);

        err = clEnqueueNDRangeKernel(g_clQueue, g_miningKernel, 1, nullptr, &global_work_size, &local_work_size, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) break;

        // Fast synchronous check back on solution memory tags
        unsigned int found_count = 0;
        clEnqueueReadBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(found_count), &found_count, 0, nullptr, nullptr);

        if (found_count > 0) {
            unsigned long long solved_nonce = 0;
            clEnqueueReadBuffer(g_clQueue, g_devNonces, CL_TRUE, 0, sizeof(solved_nonce), &solved_nonce, 0, nullptr, nullptr);
            
            // Re-zero card counters instantly to clear lane for next cycles
            clEnqueueWriteBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(reset_counter), &reset_counter, 0, nullptr, nullptr);
            
            // TODO: Trigger mining.submit network packet containing solved_nonce
        }

        nonce_iterator += global_work_size;
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Yield slightly to prevent system display lag
    }
}

// Clean termination sequence releasing standard context allocations
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
