#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <miner_types.h>

struct StratumJob {
    std::string job_id = "";
    std::string block_height_hex = ""; // Autolykos v2 sends height as a hex string
    std::string header_hash_hex = "";  // Maps to your kernel's 256-bit header_hash
    bool is_new_job = false;
};

// Tracks the raw difficulty scalar sent by mining.set_difficulty
std::string g_active_pool_diff = "1";
std::string g_pool_extra_nonce1 = "0000"; 

// Keeps your existing token cleaner function completely intact
std::string cleanToken(const std::string& token); 

StratumJob parseStratumLine(const std::string& line) {
    StratumJob job;

    // Parse packet 1 to grab your custom ExtraNonce1 assignment string
    if (line.find("\"id\":1") != std::string::npos && line.find("\"result\"") != std::string::npos) {
        size_t result_pos = line.find("\"result\":[");
        if (result_pos != std::string::npos) {
            std::string inner_res = line.substr(result_pos + 9);
            std::stringstream ss(inner_res);
            std::string t1, t2;
            std::getline(ss, t1, ','); 
            std::getline(ss, t2, ','); 
            g_pool_extra_nonce1 = cleanToken(t2);
        }
        return job;
    }
    
    if (line.find("\"id\":2") != std::string::npos && line.find("\"result\":true") != std::string::npos) return job;

    // Parse difficulty scalar
    if (line.find("\"method\":\"mining.set_difficulty\"") != std::string::npos) {
        size_t params_pos = line.find("\"params\":[");
        if (params_pos != std::string::npos) {
            std::string diff_val = line.substr(params_pos + 10);
            g_active_pool_diff = cleanToken(diff_val);
        }
        return job;
    }

    // 💥 FIX: Parse the true 3-parameter Ergo notification frame
    if (line.find("\"method\":\"mining.notify\"") != std::string::npos) {
        size_t params_pos = line.find("\"params\":[");
        if (params_pos != std::string::npos) {
            std::string params_str = line.substr(params_pos + 10);
            std::vector<std::string> tokens;
            std::stringstream ss(params_str);
            std::string token;
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }

            // Ergo solo pools send exactly 3 standard string array parameters
            if (tokens.size() >= 3) {
                job.is_new_job = true;
                job.job_id = cleanToken(tokens[0]);
                job.block_height_hex = cleanToken(tokens[1]);
                job.header_hash_hex = cleanToken(tokens[2]); // This is your 256-bit target hash hex string
                
                extern std::string g_network_status_msg;
                g_network_status_msg = "[OK] Mining Active | Processing Ergo Block Height Hex: " + job.block_height_hex;
            }
        }
    }
    return job;
}
