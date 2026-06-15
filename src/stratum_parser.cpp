#include <iostream>
#include <string>

// Structure to hold information sent from the mining pool
struct StratumJob {
    std::string job_id = "";
    std::string seed_hash = "";  // Used to generate the memory-hard DAG
    std::string difficulty = ""; // Target boundary for valid shares
    bool is_new_job = false;
};

// Parser hook function to decode standard Stratum JSON-RPC payloads
StratumJob parseStratumLine(const std::string& line) {
    StratumJob job;

    // 1. Hook into Subscription/Authorization Responses
    if (line.find("\"id\":1") != std::string::npos && line.find("\"result\"") != std::string::npos) {
        std::cout << "[NET] Subscribed successfully to Stratum server.\n";
        return job;
    }
    if (line.find("\"id\":2") != std::string::npos && line.find("\"result\":true") != std::string::npos) {
        std::cout << "[NET] Worker authorization verified.\n";
        return job;
    }

    // 2. Hook into New Block Notifications ("mining.notify")
    // Autolykos v2 standard payload: ["job_id", "seed_hash", "difficulty", ...]
    if (line.find("\"method\":\"mining.notify\"") != std::string::npos) {
        job.is_new_job = true;

        size_t params_pos = line.find("\"params\":[");
        if (params_pos != std::string::npos) {
            std::string params = line.substr(params_pos + 10); // Strip prefix

            // Extract Job ID (First item inside the parameter array)
            size_t start = params.find("\"");
            size_t end = params.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                job.job_id = params.substr(start + 1, end - start - 1);
            }

            // Extract Seed Hash / Target Header (Second item)
            start = params.find("\"", end + 1);
            end = params.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                job.seed_hash = params.substr(start + 1, end - start - 1);
            }

            // Extract Difficulty / Extra parameters if provided
            start = params.find("\"", end + 1);
            end = params.find("\"", start + 1);
            if (start != std::string::npos && end != std::string::npos) {
                job.difficulty = params.substr(start + 1, end - start - 1);
            }
        }
    }
    return job;
// Inside src/stratum_parser.cpp's parseStratumLine function:
// Update your parsing rules to monitor dynamic message IDs greater than 2

if (line.find("\"result\":true") != std::string::npos && line.find("\"error\":null") != std::string::npos) {
    // Check if this true result corresponds to a dynamic share submission response
    if (line.find("\"id\":1") == std::string::npos && line.find("\"id\":2") == std::string::npos) {
        extern std::atomic<unsigned int> g_shares_accepted;
        extern std::string g_network_status_msg;
        
        g_shares_accepted++;
        g_network_status_msg = "✅ [SHARE ACCEPTED] Pool verified solution block!";
        return job;
    }
}

if (line.find("\"error\":") != std::string::npos && line.find("\"error\":null") == std::string::npos) {
    if (line.find("\"id\":1") == std::string::npos && line.find("\"id\":2") == std::string::npos) {
        extern std::atomic<unsigned int> g_shares_rejected;
        extern std::string g_network_status_msg;
        
        g_shares_rejected++;
        g_network_status_msg = "❌ [SHARE REJECTED] Stale or invalid nonce verification.";
        return job;
    }
}

}
