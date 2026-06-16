#include "miner_types.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

extern std::string g_active_pool_diff;
extern std::string g_pool_extra_nonce1;

std::string cleanToken(const std::string& token) {
    std::string clean = token;
    while (!clean.empty() && (clean.front() == ' ' || clean.front() == '"' || clean.front() == '\'' || clean.front() == '[')) {
        clean.erase(0, 1);
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '"' || clean.back() == '\'' || clean.back() == ']' || clean.back() == '}')) {
        clean.pop_back();
    }
    return clean;
}

StratumJob parseStratumLine(const std::string& line) {
    StratumJob job;

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

    if (line.find("\"method\":\"mining.set_difficulty\"") != std::string::npos) {
        size_t params_pos = line.find("\"params\":[");
        if (params_pos != std::string::npos) {
            std::string diff_val = line.substr(params_pos + 10);
            g_active_pool_diff = cleanToken(diff_val);
        }
        return job;
    }

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

            if (tokens.size() >= 3) {
                job.job_id = cleanToken(tokens[0]);
                job.block_height_hex = cleanToken(tokens[1]);
                
                // 🚀 FIXED: Clean the quotes off EACH token before checking for a 64-character hash length
                for (const auto& raw_t : tokens) {
                    std::string cleaned = cleanToken(raw_t);
                    if (cleaned.length() == 64) {
                        job.header_hash_hex = cleaned;
                        job.is_new_job = true;
                        break;
                    }
                }

                if (job.is_new_job) {
                    extern std::string g_network_status_msg;
                    g_network_status_msg = "[OK] Mining Active | Processing Ergo Height: " + job.block_height_hex;
                }
            }
        }
    }
    return job;
}
