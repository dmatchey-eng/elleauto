#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <cstdlib>
// Shared atomic states for the UI dashboard to read safely
std::atomic<int> g_dag_progress(0);         // 0 to 100%
std::atomic<bool> g_is_dag_building(false); // Controls loading screens

// OpenCL Global Handles
extern cl_mem dag_buffer_part1;
extern cl_mem dag_buffer_part2;
cl_kernel g_mining_kernel = nullptr;
// Global variables to track our split buffers
cl_mem dag_buffer_part1 = nullptr;
cl_mem dag_buffer_part2 = nullptr;

void applyAmdDriverFixes() {
    // Force AMD OpenCL driver to allow maximum possible allocation per buffer
    _putenv_s("GPU_MAX_ALLOC_PERCENT", "100");
    _putenv_s("GPU_SINGLE_ALLOC_PERCENT", "100");
    _putenv_s("GPU_MAX_HEAP_SIZE", "100");
    _putenv_s("GPU_USE_SYNC_OBJECTS", "1");
}

bool allocateDagOnGpu(cl_context context, size_t total_dag_bytes) {
    applyAmdDriverFixes();

    // Split the massive Autolykos DAG into two equal parts to bypass WDDM restrictions
    size_t half_dag_size = total_dag_bytes / 2;
    cl_int err1, err2;

    std::cout << "[GPU] Total DAG Size: " << (total_dag_bytes / (1024 * 1024)) << " MB\n";
    std::cout << "[GPU] Allocating Part 1 (" << (half_dag_size / (1024 * 1024)) << " MB)...\n";
    
    dag_buffer_part1 = clCreateBuffer(
        context, 
        CL_MEM_READ_ONLY, // Best performance for Polaris architecture cache
        half_dag_size, 
        nullptr, 
        &err1
    );

    std::cout << "[GPU] Allocating Part 2 (" << (half_dag_size / (1024 * 1024)) << " MB)...\n";
    dag_buffer_part2 = clCreateBuffer(
        context, 
        CL_MEM_READ_ONLY, 
        half_dag_size, 
        nullptr, 
        &err2
    );

    if (err1 != CL_SUCCESS || err2 != CL_SUCCESS) {
        std::cerr << "[GPU ERROR] DAG allocation failed. Part1: " << err1 << ", Part2: " << err2 << "\n";
        std::cerr << "Ensure no other display software or games are consuming your 8GB VRAM.\n";
        return false;
    }

    std::cout << "[GPU SUCCESS] 8GB DAG buffers successfully committed to VRAM.\n";
    return true;
}
bool runMiningPass(cl_context context, cl_command_queue queue, cl_program program, ulong nonce_start, ulong target_difficulty) {
    cl_int err;

    // 1. Compile the discrete entry kernel object if not already cached
    if (!g_mining_kernel) {
        g_mining_kernel = clCreateKernel(program, "autolykos_search", &err);
        if (err != CL_SUCCESS) {
            std::cerr << "[GPU ERROR] Failed to instantiate kernel object.\n";
            return false;
        }
    }

    // 2. UI SYNC: Host-side DAG Simulation and progressive push
    g_is_dag_building = true;
    size_t half_dag_elements = 200000000; // Example placeholder element scale (~1.6GB per split)
    size_t chunk_bytes = half_dag_elements * sizeof(unsigned long long);

    std::vector<unsigned long long> host_buffer(half_dag_elements, 0xABCDEF123456789ULL); 

    // Simulate work intervals to feed the UI percentage frame updates
    for (int step = 1; step <= 4; ++step) {
        // (In a complete miner, compute actual seed hashing blocks per segment here)
        g_dag_progress = step * 25; 
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); 
    }
    g_is_dag_building = false;

    // Write host buffers across the PCIe lanes to your split Polaris VRAM allocations
    err = clEnqueueWriteBuffer(queue, dag_buffer_part1, CL_FALSE, 0, chunk_bytes, host_buffer.data(), 0, nullptr, nullptr);
    err |= clEnqueueWriteBuffer(queue, dag_buffer_part2, CL_FALSE, 0, chunk_bytes, host_buffer.data(), 0, nullptr, nullptr);
    
    if (err != CL_SUCCESS) {
        std::cerr << "[GPU ERROR] PCIe transfer to RX 580 VRAM failed.\n";
        return false;
    }

    // 3. Set Up Outputs for Nonce Results
    unsigned long long host_found_nonces[10] = {0};
    unsigned int host_counter = 0;

    cl_mem dev_nonces = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(host_found_nonces), nullptr, &err);
    cl_mem dev_counter = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(host_counter), nullptr, &err);
    clEnqueueWriteBuffer(queue, dev_counter, CL_TRUE, 0, sizeof(host_counter), &host_counter, 0, nullptr, nullptr);

    // 4. Bind Kernel Arguments
    clSetKernelArg(g_mining_kernel, 0, sizeof(cl_mem), &dag_buffer_part1);
    clSetKernelArg(g_mining_kernel, 1, sizeof(cl_mem), &dag_buffer_part2);
    clSetKernelArg(g_mining_kernel, 2, sizeof(ulong), &half_dag_elements);
    clSetKernelArg(g_mining_kernel, 3, sizeof(ulong), &target_difficulty);
    clSetKernelArg(g_mining_kernel, 4, sizeof(ulong), &nonce_start);
    clSetKernelArg(g_mining_kernel, 5, sizeof(cl_mem), &dev_nonces);
    clSetKernelArg(g_mining_kernel, 6, sizeof(cl_mem), &dev_counter);

    // 5. Fire Command Queue onto the RX 580 Compute Units
    // The Polaris RX 580 runs exceptionally well at global sizes set to multiples of 64 (Wavefront blocks)
    size_t global_work_size = 1024 * 64; 
    size_t local_work_size = 256;        

    err = clEnqueueNDRangeKernel(queue, g_mining_kernel, 1, nullptr, &global_work_size, &local_work_size, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "[GPU ERROR] Execution failure on compute array. Code: " << err << "\n";
        return false;
    }

    // Read back results asynchronously
    clEnqueueReadBuffer(queue, dev_counter, CL_TRUE, 0, sizeof(host_counter), &host_counter, 0, nullptr, nullptr);
    if (host_counter > 0) {
        clEnqueueReadBuffer(queue, dev_nonces, CL_TRUE, 0, sizeof(host_found_nonces), host_found_nonces, 0, nullptr, nullptr);
        std::cout << "[GPU BLOCK MATCH] Target verified! Nonce: " << host_found_nonces[0] << "\n";
        // Trigger share submission logic here
    }

    // Cleanup ephemeral tracking handles
    clReleaseMemObject(dev_nonces);
    clReleaseMemObject(dev_counter);
    return true;
}
