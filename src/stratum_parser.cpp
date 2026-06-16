#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <atomic>

struct StratumJob {
    std::string job_id = "";
    std::string seed_hash = "";  
    std::string difficulty = ""; 
    bool is_new_job = false;
};

// 🚀 FIX 1: Globally tracked string variables to hold the pool's actual assigned configurations
std::string g_active_pool_diff = "1";
std::string g_pool_extra_nonce1 = "0000"; // Fallback placeholder if parse fails

std::string cleanToken(const std::string& token) {
    std::string clean = token;
    while (!clean.empty() && (clean.front() == ' ' || clean.front() == '"' || clean.front() == '[')) {
        clean.erase(0, 1);
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '"' || clean.back() == ']' || clean.back() == '}')) {
        clean.pop_back();
    }
    return clean;
}

StratumJob parseStratumLine(const std::string& line) {
    StratumJob job;

    // 🚀 FIX 2: Parse packet 1 to grab your custom ExtraNonce1 assignment string ("1bb5")
    if (line.find("\"id\":1") != std::string::npos && line.find("\"result\"") != std::string::npos) {
        size_t result_pos = line.find("\"result\":[");
        if (result_pos != std::string::npos) {
            std::string inner_res = line.substr(result_pos + 9);
            std::stringstream ss(inner_res);
            std::string t1, t2;
            std::getline(ss, t1, ','); // Skip null
            std::getline(ss, t2, ','); // This holds your true ExtraAnonce1 token string!
            g_pool_extra_nonce1 = cleanToken(t2);
        }
        return job;
    }
    
    if (line.find("\"id\":2") != std::string::npos && line.find("\"result\":true") != std::string::npos) return job;

    // Keep the rest of your set_difficulty and mining.notify loops exactly the same below...
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
            std::string params_str = line.substr(params_pos + 9);
            std::vector<std::string> tokens;
            std::stringstream ss(params_str);
            std::string token;
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }

            // 🚀 ARCHITECTURAL FIX: Explicitly match the true array positions of the Ergo protocol
            // params[0] = Job ID ("0")
            // params[1] = Block Height (1808415)
            // params[2] = Seed Hash ("cf1dbe9e...")
            // params[3] = ExtraNonce2 length tracking placeholder ("")
            // params[4] = Public key target mapping template ("")
            // params[5] = Target difficulty boundary limit parameter block (Numeric text string)
            if (tokens.size() > 5) {
                job.is_new_job = true;
                job.job_id = cleanToken(tokens[0]);
                job.seed_hash = cleanToken(tokens[2]);
                
                // Read from slot 5 if filled by the pool, otherwise use the fallback multiplier scalar
                std::string target_token = cleanToken(tokens[5]);
                if (!target_token.empty() && target_token != "\"\"") {
                    job.difficulty = target_token;
                } else {
                    job.difficulty = g_active_pool_diff;
                }
                
                extern std::string g_network_status_msg;
                g_network_status_msg = "[OK] Mining Active | Processing Ergo Block Height: " + cleanToken(tokens[1]);
            }
        }
    }
    return job;
}
