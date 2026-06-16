#include "miner_types.h"
#include <iostream>
#include <vector>
#include <CL/cl.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>

// Global variables tracked by the subsystem runtime
cl_context g_clContext = nullptr;
cl_command_queue g_clQueue = nullptr;
cl_kernel g_miningKernel = nullptr;
cl_mem g_dagBufferPart1 = nullptr;
cl_mem g_dagBufferPart2 = nullptr;
cl_mem g_devCounter = nullptr;
cl_mem g_devNonces = nullptr;

extern std::atomic<bool> is_mining_running;
extern std::atomic<bool> is_current_job_valid;
extern std::atomic<bool> g_is_dag_building;
extern std::atomic<int> g_dag_progress;
extern std::string g_current_job_id;

// 🚀 CLEAN FORWARD DECLARATIONS: No dangling brackets or code statements allowed!
unsigned long long parseHexSlice64(const std::string& hex_slice);
HostUlong4 parseHeaderHashToUlong4(const std::string& raw_hex);
void submitShare(const std::string& job_id, unsigned long long found_nonce, HostUlong4 found_solution);

bool allocateAndBuildVectorDag(size_t total_elements_count) {
    g_is_dag_building = true;
    cl_int err1, err2;

    size_t half_elements = total_elements_count / 2;
    size_t half_bytes_size = half_elements * sizeof(HostUlong4); 

    g_dagBufferPart1 = clCreateBuffer(g_clContext, CL_MEM_READ_ONLY, half_bytes_size, nullptr, &err1);
    g_dagBufferPart2 = clCreateBuffer(g_clContext, CL_MEM_READ_ONLY, half_bytes_size, nullptr, &err2);

    if (err1 != CL_SUCCESS || err2 != CL_SUCCESS) {
        g_is_dag_building = false;
        return false;
    }

    std::vector<HostUlong4> host_chunk(half_elements);
    for(size_t i = 0; i < half_elements; ++i) {
        unsigned long long dummy_val = (i * 0xFFFFFFFFFFFFFFFFULL) ^ (half_elements + i);
        host_chunk[i].s0 = dummy_val;
        host_chunk[i].s1 = dummy_val ^ 0x123456789ABCDEF0ULL; 
        host_chunk[i].s2 = dummy_val + i;
        host_chunk[i].s3 = dummy_val | 0x0F0F0F0F0F0F0F0FULL;
    }
    
    g_dag_progress = 25;
    clEnqueueWriteBuffer(g_clQueue, g_dagBufferPart1, CL_TRUE, 0, half_bytes_size, host_chunk.data(), 0, nullptr, nullptr);
    
    g_dag_progress = 75;
    clEnqueueWriteBuffer(g_clQueue, g_dagBufferPart2, CL_TRUE, 0, half_bytes_size, host_chunk.data(), 0, nullptr, nullptr);
    
    clFinish(g_clQueue); 
    g_dag_progress = 100;
    g_is_dag_building = false;
    return true;
}

extern cl_context g_clContext;
extern cl_command_queue g_clQueue;
extern cl_kernel g_miningKernel;
extern cl_mem g_devCounter;
extern cl_mem g_devNonces;

bool initOpenCL() {
    cl_uint platform_count = 0;
    clGetPlatformIDs(0, nullptr, &platform_count);
    if (platform_count == 0) {
        std::cerr << "[GPU INIT ERROR] No OpenCL platforms detected on this machine.\n";
        return false;
    }

    std::vector<cl_platform_id> platforms(platform_count);
    clGetPlatformIDs(platform_count, platforms.data(), nullptr);

    // Target the primary available compute engine framework path
    cl_platform_id active_platform = platforms[0];

    cl_uint device_count = 0;
    clGetDeviceIDs(active_platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &device_count);
    if (device_count == 0) {
        std::cerr << "[GPU INIT ERROR] No dedicated GPU devices found on active platform.\n";
        return false;
    }

    std::vector<cl_device_id> devices(device_count);
    clGetDeviceIDs(active_platform, CL_DEVICE_TYPE_GPU, device_count, devices.data(), nullptr);
    cl_device_id active_device = devices[0];

    // Read and display hardware footprint information
    char gpu_name[256];
    clGetDeviceInfo(active_device, CL_DEVICE_NAME, sizeof(gpu_name), gpu_name, nullptr);
    std::cout << "[GPU INIT] Connected to compute device: " << gpu_name << "\n";

    cl_int err;
    g_clContext = clCreateContext(nullptr, 1, &active_device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) return false;

    g_clQueue = clCreateCommandQueueWithProperties(g_clContext, active_device, nullptr, &err);
    if (err != CL_SUCCESS) return false;

    // Allocation blocks for dynamic share counters and nonces output storage arrays
    unsigned int initial_counter_val = 0;
    g_devCounter = clCreateBuffer(g_clContext, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(unsigned int), &initial_counter_val, &err);
    if (err != CL_SUCCESS) return false;

    // Hold up to 10 concurrently found share vectors (10 results * 5 ulong variables * 8 bytes)
    g_devNonces = clCreateBuffer(g_clContext, CL_MEM_READ_WRITE, 10 * 5 * sizeof(unsigned long long), nullptr, &err);
    if (err != CL_SUCCESS) return false;

    // 🚀 LOAD & COMPILE KERNEL SOURCE
    std::ifstream kernel_file("autolykos.cl");
    if (!kernel_file.is_open()) {
        std::cerr << "[GPU INIT ERROR] Failed to locate autolykos.cl inside binary file path.\n";
        return false;
    }

    std::stringstream source_stream;
    source_stream << kernel_file.rdbuf();
    std::string source_str = source_stream.str();
    const char* source_ptr = source_str.c_str();
    size_t source_len = source_str.length();

    cl_program program = clCreateProgramWithSource(g_clContext, 1, &source_ptr, &source_len, &err);
    if (err != CL_SUCCESS) return false;

    std::cout << "[GPU INIT] Compiling autolykos.cl optimized kernel layout text parameters...\n";
    err = clBuildProgram(program, 1, &active_device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        // Capture build log parameters if compiler crashes
        size_t log_size;
        clGetProgramBuildInfo(program, active_device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> build_log(log_size);
        clGetProgramBuildInfo(program, active_device, CL_PROGRAM_BUILD_LOG, log_size, build_log.data(), nullptr);
        std::cerr << "[GPU COMPILER FATAL ERROR]:\n" << build_log.data() << "\n";
        clReleaseProgram(program);
        return false;
    }

    g_miningKernel = clCreateKernel(program, "autolykos_search", &err);
    clReleaseProgram(program); // Safe to discard program handle container once kernel instance extraction clears

    if (err != CL_SUCCESS) {
        std::cerr << "[GPU INIT ERROR] Failed to capture 'autolykos_search' kernel footprint.\n";
        return false;
    }

    std::cout << "[GPU SUCCESS] Context infrastructure binding initialization complete.\n";
    return true;
}

void runMiningLoop(unsigned long long initial_nonce, HostUlong4 target_difficulty, HostUlong4 header_hash_input) {
    cl_int err;
    unsigned long long nonce_iterator = initial_nonce;
    unsigned long long half_elements_vector = 200000000; 

    unsigned int reset_counter = 0;
    clEnqueueWriteBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(reset_counter), &reset_counter, 0, nullptr, nullptr);
    
    clSetKernelArg(g_miningKernel, 0, sizeof(cl_mem), &g_dagBufferPart1);
    clSetKernelArg(g_miningKernel, 1, sizeof(cl_mem), &g_dagBufferPart2);
    clSetKernelArg(g_miningKernel, 2, sizeof(unsigned long long), &half_elements_vector);
    clSetKernelArg(g_miningKernel, 3, sizeof(HostUlong4), &header_hash_input);
    clSetKernelArg(g_miningKernel, 4, sizeof(HostUlong4), &target_difficulty); 
    clSetKernelArg(g_miningKernel, 6, sizeof(cl_mem), &g_devNonces);
    clSetKernelArg(g_miningKernel, 7, sizeof(cl_mem), &g_devCounter);

    // 🚀 STEP 1: Balanced work dimensions
    size_t global_work_size = 512 * 1024; // 524,288 parallel threads per wave pass
    size_t local_work_size = 256; 

    while (is_mining_running && is_current_job_valid && !g_is_dag_building) {
        
        // 🚀 STEP 2: Stack multiple non-blocking execution blocks back-to-back.
        // This keeps the hardware compute queues full so the GPU doesn't drop to 0% load!
        for (int wave = 0; wave < 8; wave++) {
            clSetKernelArg(g_miningKernel, 5, sizeof(unsigned long long), &nonce_iterator);
            
            // Queue the execution wave without blocking the CPU
            err = clEnqueueNDRangeKernel(g_clQueue, g_miningKernel, 1, nullptr, &global_work_size, &local_work_size, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) break;

            // Shift nonces step-wise per wave block
            nonce_iterator += global_work_size;
        }

        // 🚀 STEP 3: Block the CPU once per big batch to let the GPU clear its stacked waves
        clFinish(g_clQueue);

        // 🚀 STEP 4: Safe, clean share match validation verification check
        unsigned int found_count = 0;
        clEnqueueReadBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(found_count), &found_count, 0, nullptr, nullptr);

        if (found_count > 0) {
            if (found_count > 10) found_count = 10; 

            unsigned long long solved_data[50] = {0}; 
            size_t bytes_to_read = found_count * 5 * sizeof(unsigned long long);
            
            clEnqueueReadBuffer(g_clQueue, g_devNonces, CL_TRUE, 0, bytes_to_read, solved_data, 0, nullptr, nullptr);
            clEnqueueWriteBuffer(g_clQueue, g_devCounter, CL_TRUE, 0, sizeof(reset_counter), &reset_counter, 0, nullptr, nullptr);
            
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

        // Yield slightly at the end of the full batch processing block to keep Windows responsive
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


void shutdownOpenCL() {
    if (g_devCounter) clReleaseMemObject(g_devCounter);
    if (g_devNonces) clReleaseMemObject(g_devNonces);
    if (g_miningKernel) clReleaseKernel(g_miningKernel);
    if (g_clQueue) clReleaseCommandQueue(g_clQueue);
    if (g_clContext) clReleaseContext(g_clContext);
}
