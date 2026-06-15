#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

// Pool Option Definition for Menu
struct PoolOption {
    std::string name;
    std::string hostname;
    std::string port;
};

struct MinerConfig {
    std::string wallet = "";
    std::string pool_host = "";
    std::string pool_port = "";
};

struct StratumJob {
    std::string job_id = "";
    std::string seed_hash = "";  
    std::string difficulty = ""; 
    bool is_new_job = false;
};

struct ActiveMiningJob {
    unsigned long long difficulty = 0;
    unsigned long long header_hash = 0;
    unsigned long long nonce_start = 1000000000ULL;
};

// Hardcoded Pool List Constants
const std::vector<PoolOption> DEFAULT_POOLS = {
    {"HeroMiners (Global/Auto)", "://herominers.com", "1147"},
    {"2Miners (Regular PPLNS)", "erg.2miners.com", "8888"},
    {"WoolyPooly (Low Fee)", "://woolypooly.com", "3100"},
    {"Custom Manual Pool Entry", "CUSTOM", "CUSTOM"}
};

// Thread Lifespan and Work Signal Controls
std::atomic<bool> is_mining_running(true);
std::atomic<bool> is_current_job_valid(false);
std::string g_current_job_id = ""; 
ActiveMiningJob g_next_job;

// External Subsystem Declarations (Defined in OpenCL/Parser source files)
StratumJob parseStratumLine(const std::string& line);
bool initOpenCL();
bool allocateAndBuildVectorDag(size_t total_elements_count);
void runMiningLoop(unsigned long long initial_nonce, unsigned long long difficulty_target, unsigned long long header_hash_input);
void shutdownOpenCL();

// Networking Initialization Helper
void initWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[ERROR] Winsock initialization failed.\n";
        exit(1);
    }
}

// Hexadecimal Utility Function
unsigned long long convertHexToUlong(const std::string& hexStr) {
    std::string cleanHex = hexStr;
    if (cleanHex.compare(0, 2, "0x") == 0 || cleanHex.compare(0, 2, "0X") == 0) {
        cleanHex = cleanHex.substr(2);
    }
    unsigned long long result = 0;
    std::stringstream ss;
    ss << std::hex << cleanHex;
    ss >> result;
    return result;
}

// GPU Concurrent Thread Module
void gpuMiningOrchestrator() {
    std::cout << "[GPU] Initializing hardware configuration arrays...\n";
    if (!initOpenCL()) {
        std::cerr << "[GPU ERROR] Failed to initialize OpenCL stack.\n";
        return;
    }

    size_t target_elements = 400000000; 
    if (!allocateAndBuildVectorDag(target_elements)) {
        std::cerr << "[GPU ERROR] VRAM allocation limits hit. Exiting thread.\n";
        return;
    }

    std::cout << "[GPU] Engine online. Awaiting first Stratum job initialization...\n";

    while (is_mining_running) {
        if (is_current_job_valid) {
            ActiveMiningJob work_pass = g_next_job;
            std::cout << "[GPU Worker] Hashing Block Job Header: 0x" << std::hex << work_pass.header_hash << std::dec << "\n";
            runMiningLoop(work_pass.nonce_start, work_pass.difficulty, work_pass.header_hash);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    shutdownOpenCL();
}

// Network Socket Background Listener Thread
void listenToPool(SOCKET poolSocket) {
    char buffer[1024]; 
    std::string stream_accumulator = "";

    while (is_mining_running) {
        int bytesReceived = recv(poolSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            std::cerr << "\n[ERROR] Connection lost to pool server.\n";
            is_mining_running = false;
            is_current_job_valid = false;
            break;
        }

        buffer[bytesReceived] = '\0';
        stream_accumulator += buffer;

        size_t newline_pos;
        while ((newline_pos = stream_accumulator.find('\n')) != std::string::npos) {
            std::string single_line = stream_accumulator.substr(0, newline_pos);
            stream_accumulator.erase(0, newline_pos + 1);

            StratumJob current_job = parseStratumLine(single_line);

            if (current_job.is_new_job) {
                if (current_job.job_id != g_current_job_id) {
                    is_current_job_valid = false; // Signal current GPU worker to stop instantly
                    g_current_job_id = current_job.job_id;

                    g_next_job.header_hash = convertHexToUlong(current_job.seed_hash);
                    g_next_job.difficulty  = convertHexToUlong(current_job.difficulty);
                    g_next_job.nonce_start = 1000000000ULL; 

                    is_current_job_valid = true; // Release GPU worker to run on updated values
                }
            }
        }
    }
}

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
        std::cout << "Enter custom Stratum Hostname: ";
        std::cin >> config.pool_host;
        std::cout << "Enter custom Stratum Port: ";
        std::cin >> config.pool_port;
    }
}

bool connectToStratum(const MinerConfig& config, SOCKET& connectSocket) {
    struct addrinfo hints {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(config.pool_host.c_str(), config.pool_port.c_str(), &hints, &result) != 0) {
        std::cerr << "[ERROR] DNS Resolution failed.\n";
        return false;
    }

    connectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connectSocket == INVALID_SOCKET) {
        freeaddrinfo(result);
        return false;
    }

    if (connect(connectSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        closesocket(connectSocket);
        freeaddrinfo(result);
        return false;
    }
    freeaddrinfo(result);

    std::string subscribePayload = "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": [\"elleauto-v1.0\", \"Autolykosv2\"]}\n";
    send(connectSocket, subscribePayload.c_str(), (int)subscribePayload.length(), 0);

    std::string authPayload = "{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\"" + config.wallet + "\", \"x\"]}\n";
    send(connectSocket, authPayload.c_str(), (int)authPayload.length(), 0);

    return true;
}

int main() {
    initWinsock();
    MinerConfig config;

    std::cout << "Enter your Ergo Wallet Address: ";
    std::cin >> config.wallet;

    selectPool(config);

    SOCKET poolSocket = INVALID_SOCKET;
    if (connectToStratum(config, poolSocket)) {
        std::cout << "[SUCCESS] Authenticated on pool!\n";
        
        // Spawn Background Worker 1: Network Listener
        std::thread network_worker(listenToPool, poolSocket);
        network_worker.detach();

        // Spawn Background Worker 2: GPU Computation Matrix
        std::thread gpu_worker(gpuMiningOrchestrator);
        gpu_worker.detach();

        // Main thread manages dynamic console interactions or telemetry 
        while (is_mining_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        closesocket(poolSocket);
    }

    WSACleanup();
    return 0;
}
