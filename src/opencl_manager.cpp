#include "miner_types.h"
#include <iostream>
#include <string>
#include <vector>
#include <CL/cl.h>
#include <thread>
#include <atomic>
#include <chrono>

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
    clSetKernelArg(g_miningKernel, 5, sizeof(unsigned long long), &nonce_iterator);
    clSetKernelArg(g_miningKernel, 6, sizeof(cl_mem), &g_devNonces);
    clSetKernelArg(g_miningKernel, 7, sizeof(cl_mem), &g_devCounter);

    size_t global_work_size = 64 * 1024;
    size_t local_work_size = 256;

    while (is_mining_running && is_current_job_valid && !g_is_dag_building) {
        clSetKernelArg(g_miningKernel, 5, sizeof(unsigned long long), &nonce_iterator);

        err = clEnqueueNDRangeKernel(g_clQueue, g_miningKernel, 1, nullptr, &global_work_size, &local_work_size, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) break;

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
        nonce_iterator += global_work_size;
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); 
    }
}

bool initOpenCL() { return true; }
void shutdownOpenCL() {}
