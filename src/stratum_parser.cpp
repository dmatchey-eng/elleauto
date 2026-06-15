#include <iostream>
#include <string>
#include <atomic>

// Match the exact structure declared across the other files
struct StratumJob {
    std::string job_id = "";
    std::string seed_hash = "";  
    std::string difficulty = ""; 
    bool is_new_job = false;
};

// Parser hook function to decode standard Stratum JSON-RPC payloads
StratumJob parseStratumLine(const std::string& line) {
    StratumJob job;

    // 1. Hook into Subscription/Authorization Responses
    if (line.find("\"id\":1") != std::string::npos && line.find("\"result\"") != std::string::npos) {
        return job;
    }
    if (line.find("\"id\":2") != std::string::npos && line.find("\"result\":true") != std::string::npos) {
        return job;
    }

    // 2. Catch Dynamic Share Submission Acceptances from the Pool
    if (line.find("\"result\":true") != std::string::npos && line.find("\"error\":null") != std::string::npos) {
        // Exclude initial login messages (IDs 1 and 2)
        if (line.find("\"id\":1") == std::string::npos && line.find("\"id\":2") == std::string::npos) {
            extern std::atomic<unsigned int> g_shares_accepted;
            extern std::string g_network_status_msg;
            
            g_shares_accepted++;
            g_network_status_msg = "✅ [SHARE ACCEPTED] Pool verified solution block!";
            return job;
        }
    }

    // 3. Catch Dynamic Share Submission Rejections
    if (line.find("\"error\":") != std::string::npos && line.find("\"error\":null") == std::string::npos) {
        if (line.find("\"id\":1") == std::string::npos && line.find("\"id\":2") == std::string::npos) {
            extern std::atomic<unsigned int> g_shares_rejected;
            extern std::string g_network_status_msg;
            
            g_shares_rejected++;
            g_network_status_msg = "❌ [SHARE REJECTED] Stale or invalid nonce verification.";
            return job;
        }
    }

    // 4. Hook into New Block Notifications ("mining.notify")
    if (line.find("\"method\":\"mining.notify\"") != std::string::npos) {
        job.is_new_job = true;

        size_t params_pos = line.find("\"params\":[");
        if (params_pos != std::string::npos) {
            std::string params = line.substr(params_pos + 10);

            // Extract Job ID
            size_t start = params.find("\"");
            size_t end = params.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                job.job_id = params.substr(start + 1, end - start - 1);
            }

            // Extract Seed Hash
            start = params.find("\"", end + 1);
            end = params.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                job.seed_hash = params.substr(start + 1, end - start - 1);
            }

            // Extract Difficulty
            start = params.find("\"", end + 1);
            end = params.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                job.difficulty = params.substr(start + 1, end - start - 1);
            }
        }
    }
    return job;
}
