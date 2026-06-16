#ifndef MINER_TYPES_H
#define MINER_TYPES_H

#include <string>

// 🚀 MASTER DEFINITION: Only written here!
struct alignas(32) HostUlong4 {
    unsigned long long s0 = 0;
    unsigned long long s1 = 0;
    unsigned long long s2 = 0;
    unsigned long long s3 = 0;
};

struct StratumJob {
    std::string job_id = "";
    std::string block_height_hex = "";  
    std::string header_hash_hex = ""; 
    bool is_new_job = false;
};

// 🚀 UNIVERSAL CROSS-LINK TRACKING REFERENCES
extern std::atomic<bool> is_mining_running;
extern std::atomic<bool> is_current_job_valid;
extern std::string g_current_job_id;
extern std::string g_network_status_msg;
extern std::string g_active_pool_diff;
extern std::string g_pool_extra_nonce1;

#endif // MINER_TYPES_H
