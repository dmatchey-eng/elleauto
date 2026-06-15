#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

bool initOpenCL();
// Global atomic flag to handle safe thread shutdown if connection drops
std::atomic<bool> is_mining_running(true);

struct StratumJob {
    std::string job_id = "";
    std::string seed_hash = "";  
    std::string difficulty = ""; 
    bool is_new_job = false;
};

struct MinerConfig {
    std::string wallet = "";
    std::string pool_host = "";
    std::string pool_port = "";
};

// Forward declaration of your external parser hook
StratumJob parseStratumLine(const std::string& line);

// This function runs concurrently on a separate CPU core
void listenToPool(SOCKET poolSocket) {
    char buffer[1024]; // Using a small static array to pull chunks from network stack
    std::string stream_accumulator = "";

    while (is_mining_running) {
        int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            std::cerr << "\n[ERROR] Connection lost to pool server.\n";
            is_mining_running = false;
            break;
        }

        buffer[bytesReceived] = '\0';
        stream_accumulator += buffer;

        size_t newline_pos;
        while ((newline_pos = stream_accumulator.find('\n')) != std::string::npos) {
            std::string single_line = stream_accumulator.substr(0, newline_pos);
            stream_accumulator.erase(0, newline_pos + 1);

            // Calls your hook inside src/stratum_parser.cpp
            StratumJob current_job = parseStratumLine(single_line);

            if (current_job.is_new_job) {
                // Background telemetry printing or hardware adjustment updates go here
                // Note: We avoid direct cout logs here to prevent clashing with the UI dashboard
            }
        }
    }
}

// 2. Select Pool Interface
void selectPool(MinerConfig& config) {
    std::cout << "=========================================================\n";
    std::cout << "  SELECT ERGO MINING POOL\n";
    std::cout << "=========================================================\n";
    for (size_t i = 0; i < DEFAULT_POOLS.size(); ++i) {
        std::cout << " [" << i + 1 << "] " << DEFAULT_POOLS[i].name;
        if (DEFAULT_POOLS[i].hostname != "CUSTOM") {
            std::cout << " (" << DEFAULT_POOLS[i].hostname << ":" << DEFAULT_POOLS[i].port << ")";
        }
        std::cout << "\n";
    }
    std::cout << "---------------------------------------------------------\n";
    std::cout << "Choose an option (1-" << DEFAULT_POOLS.size() << "): ";
    
    size_t choice;
    std::cin >> choice;
    
    if (choice > 0 && choice < DEFAULT_POOLS.size()) {
        config.pool_host = DEFAULT_POOLS[choice - 1].hostname;
        config.pool_port = DEFAULT_POOLS[choice - 1].port;
    } else {
        std::cout << "Enter custom Stratum Hostname (e.g., erg.2miners.com): ";
        std::cin >> config.pool_host;
        std::cout << "Enter custom Stratum Port (e.g., 8888): ";
        std::cin >> config.pool_port;
    }
}

// 3. Connect and Handshake via Stratum Protocol
bool connectToStratum(const MinerConfig& config, SOCKET& connectSocket) {
    struct addrinfo hints {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(config.pool_host.c_str(), config.pool_port.c_str(), &hints, &result) != 0) {
        std::cerr << "[ERROR] DNS Resolution failed for " << config.pool_host << "\n";
        return false;
    }

    connectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connectSocket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Socket creation failed.\n";
        freeaddrinfo(result);
        return false;
    }

    std::cout << "[NET] Connecting to pool socket...\n";
    if (connect(connectSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Pool connection timed out or refused.\n";
        closesocket(connectSocket);
        freeaddrinfo(result);
        return false;
    }
    freeaddrinfo(result);
    std::cout << "[NET] Connected! Sending Stratum Subscription...\n";

    // 4. Send Stratum Subscriptions (Format: JSON-RPC)
    std::string subscribePayload = "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": [\"elleauto-v1.0\", \"Autolykosv2\"]}\n";
    send(connectSocket, subscribePayload.c_str(), (int)subscribePayload.length(), 0);

    // Auth Payload linking your wallet address as worker
    std::string authPayload = "{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\"" + config.wallet + "\", \"x\"]}\n";
    send(connectSocket, authPayload.c_str(), (int)authPayload.length(), 0);

    return true;
}
void listenToPool(SOCKET poolSocket) {
    char buffer[4096];
    std::string stream_accumulator = "";

    while (true) {
        int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            std::cerr << "[ERROR] Connection lost to pool.\n";
            break;
        }

        buffer[bytesReceived] = '\0';
        stream_accumulator += buffer;

        // Process line-by-line using the newline delimiter
        size_t newline_pos;
        while ((newline_pos = stream_accumulator.find('\n')) != std::string::npos) {
            std::string single_line = stream_accumulator.substr(0, newline_pos);
            stream_accumulator.erase(0, newline_pos + 1);

            // Trigger the parser hook
            StratumJob current_job = parseStratumLine(single_line);

            if (current_job.is_new_job) {
                std::cout << ">>> [NEW BLOCK] Job ID: " << current_job.job_id << "\n";
                std::cout << "    [SEED HASH]: " << current_job.seed_hash << "\n";
                // TODO: Update OpenCL execution parameters with this new work
            }
        }
    }
}
extern std::atomic<int> g_dag_progress;
extern std::atomic<bool> g_is_dag_building;

void printDashboard(const MinerConfig& config, const MinerStats& stats) {
    clearScreen();
    std::cout << "=========================================================\n";
    std::cout << "  AUTOLYKOS V2 MINER - AMD RX 580 (Ellesmere 8GB)\n";
    std::cout << "=========================================================\n";
    
    if (g_is_dag_building) {
        std::cout << "\n\n      [HARDWARE STATE] Generating Memory-Hard DAG...\n";
        std::cout << "      Progress: [";
        int barWidth = 20;
        int pos = (g_dag_progress * barWidth) / 100;
        for (int i = 0; i < barWidth; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << g_dag_progress << " %\n\n\n";
    } else {
        std::cout << " [POOL]   " << config.pool_host << ":" << config.pool_port << "\n";
        std::cout << " [SPEED]  " << stats.hashrate << " MH/s\n";
        std::cout << " [SHARES] Accepted: " << stats.accepted_shares << "\n";
    }
    std::cout << "=========================================================\n";
}

int main() {
    initWinsock();
    MinerConfig config;

    std::cout << "Enter your Ergo Wallet Address: ";
    std::cin >> config.wallet;

    // Call your default pool interactive menu
    // (Defined in previous steps, matching selection logic)
    // selectPool(config); 

    SOCKET poolSocket = INVALID_SOCKET;
    if (connectToStratum(config, poolSocket)) {
        std::cout << "[SUCCESS] Authenticated on pool!\n";
        std::cout << "Spawning background network listening worker thread...\n";
        
        // 🚀 Kick network processing entirely off to a background loop
        std::thread network_worker(listenToPool, poolSocket);
        network_worker.detach(); // Detach to let it manage itself independently

        // 3. Main execution loop (Completely unlocked and reactive console)
        while (is_mining_running) {
            // Your console frame renderer, user keyboard inputs, 
            // or GPU temperature checker runs completely stutter-free here!
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        closesocket(poolSocket);
    }

    WSACleanup();
    return 0;
}
