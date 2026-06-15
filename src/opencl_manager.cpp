#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <cstdlib>

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
