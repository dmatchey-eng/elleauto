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

// Helper utility to safely strip wrapping quotes or whitespace from an extracted token
std::string cleanToken(const std::string& token) {
    std::string clean = token;
    // Strip leading spaces or brackets
    while (!clean.empty() && (clean.front() == ' ' || clean.front() == '"' || clean.front() == '[')) {
        clean.erase(0, 1);
    }
    // Strip trailing spaces or brackets
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '"' || clean.back() == ']' || clean.back() == '}')) {
        clean.pop_back();
    }
    return clean;
}

StratumJob parseStratumLine(const std::string& line) {
    StratumJob job;

    // 1. Process Subscription and Authorization Responses
    if (line.find("\"id\":1") != std::string::npos && line.find("\"result\"") != std::string::npos) {
        return job;
    }
    if (line.find("\"id\":2") != std::string::npos && line.find("\"result\":true") != std::string::npos) {
        return job;
    }

    // 2. Process Share Acceptance or Rejection Telemetry Responses
    if (line.find("\"result\":true") != std::string::npos && line.find("\"error\":null") != std::string::npos) {
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

    // 3. Process New Block Work Notifications ("mining.notify")
    if (line.find("\"method\":\"mining.notify\"") != std::string::npos) {
        size_t params_pos = line.find("\"params\":[");
        if (params_pos != std::string::npos) {
            std::string params_str = line.substr(params_pos + 9); // Isolate parameters inner string

            // Tokenize parameters split by commas
            std::vector<std::string> tokens;
            std::stringstream ss(params_str);
            std::string token;
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }

            // HeroMiners Ergo Parameter Mapping:
            // tokens[0] = Job ID (String format "0")
            // tokens[1] = Block Height (Numeric format 1808338)
            // tokens[2] = Seed Hash (String format "07e04697...")
            // tokens[6] = Target Difficulty Boundary (Numeric format 28948022...)
            if (tokens.size() > 6) {
                job.is_new_job = true;
                job.job_id = cleanToken(tokens[0]);
                job.seed_hash = cleanToken(tokens[2]);
                job.difficulty = cleanToken(tokens[6]);
                
                // Active status synchronization update for the main dashboard display
                extern std::string g_network_status_msg;
                g_network_status_msg = "⛏️ Mining Active | Processing Ergo Block Height: " + cleanToken(tokens[1]);
            }
        }
    }
    return job;
}
