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
     is_new_job = false;
};

struct ActiveMiningJob {
    unsigned long long difficulty = 0;
    unsigned long long header_hash = 0;
    unsigned long long nonce_start = 1000000000ULL;
};

// Hardcoded Pool List Constants
// 🚀 FIX 2: Clean hostnames without any protocol prefixes
const std::vector<PoolOption> DEFAULT_POOLS = {
    {"HeroMiners (Global/Auto)", "://herominers.com", "1147"},
    {"2Miners (Regular PPLNS)", "://2miners.com", "8888"},
    {"WoolyPooly (Low Fee)", "://woolypooly.com", "3100"},
    {"Custom Manual Pool Entry", "CUSTOM", "CUSTOM"}
};


// Thread Lifespan and Work Signal Controls
// Thread Lifespan and Work Signal Controls
std::atomic<> is_mining_running(true);
std::atomic<> is_current_job_valid(false);
std::string g_current_job_id = ""; 
ActiveMiningJob g_next_job;

// 🚀 FIX: Defining the global atomic variables so opencl_manager can link to them!
std::atomic<int> g_dag_progress(0);
std::atomic<bool> g_is_dag_building(false);

// Dynamic share counters
std::atomic<unsigned int> g_shares_submitted(0);
std::atomic<unsigned int> g_shares_accepted(0);
std::atomic<unsigned int> g_shares_rejected(0);
std::string g_network_status_msg = "Awaiting network jobs...";

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

// Global socket handle for external subsystem usage
SOCKET g_poolSocketGlobal = INVALID_SOCKET;
std::atomic<unsigned int> g_rpc_id_counter(3);

void submitShare(const std::string& job_id, unsigned long long found_nonce) {
    if (g_poolSocketGlobal == INVALID_SOCKET) return;

    g_shares_submitted++;
    g_network_status_msg = "🚀 [SUBMITTING SHARE] Nonce found! Transmitting to pool...";

    std::stringstream hex_stream;
    hex_stream << "0x" << std::hex << found_nonce;
    std::string nonce_hex = hex_stream.str();

    unsigned int message_id = g_rpc_id_counter.fetch_add(1);

    std::string submitPayload = "{\"id\": " + std::to_string(message_id) + 
                                ", \"method\": \"mining.submit\", \"params\": [\"elleauto-worker\", \"" + 
                                job_id + "\", \"" + nonce_hex + "\"]}\n";

    send(g_poolSocketGlobal, submitPayload.c_str(), (int)submitPayload.length(), 0);
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
            runMiningLoop(work_pass.nonce_start, work_pass.difficulty, work_pass.header_hash);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    shutdownOpenCL();
}

// Network Socket Background Listener Thread
void listenToPool(SOCKET poolSocket) {
    // 🚀 FIX: Use a proper 2048-byte buffer array to safely hold incoming pool data packets
    char recv_buffer[2048]; 
    std::string stream_accumulator = "";

    while (is_mining_running) {
        // Pass the array and its size so recv can properly parse network traffic
        int bytesReceived = recv(poolSocket, recv_buffer, sizeof(recv_buffer) - 1, 0);
        if (bytesReceived <= 0) {
            std::cerr << "\n[ERROR] Connection lost to pool server.\n";
            is_mining_running = false;
            is_current_job_valid = false;
            break;
        }

        // Properly null-terminate the character data array
        recv_buffer[bytesReceived] = '\0'; 
        stream_accumulator += recv_buffer;

        size_t newline_pos;
        while ((newline_pos = stream_accumulator.find('\n')) != std::string::npos) {
            std::string single_line = stream_accumulator.substr(0, newline_pos);
            stream_accumulator.erase(0, newline_pos + 1);

            StratumJob current_job = parseStratumLine(single_line);

            if (current_job.is_new_job) {
                if (current_job.job_id != g_current_job_id) {
                    is_current_job_valid = false; 
                    g_current_job_id = current_job.job_id;

                    g_next_job.header_hash = convertHexToUlong(current_job.seed_hash);
                    g_next_job.difficulty  = convertHexToUlong(current_job.difficulty);
                    g_next_job.nonce_start = 1000000000ULL; 

                    is_current_job_valid = true; 
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
    
    if (choice > 0 && choice <= DEFAULT_POOLS.size()) {
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
    // Debug printout to see exactly what address string is hitting the resolver
    std::cout << "[NET Debug] Attempting DNS resolution for host: " << config.pool_host 
              << " on port: " << config.pool_port << "\n";

    struct addrinfo hints {}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;     // 🚀 FIX 1: Allow both IPv4 and IPv6 lookup paths
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Execute Windows native address translation protocol
    int dns_status = getaddrinfo(config.pool_host.c_str(), config.pool_port.c_str(), &hints, &result);
    if (dns_status != 0) {
        std::cerr << "[ERROR] DNS Resolution failed. Windows Error Code: " << WSAGetLastError() 
                  << " (getaddrinfo code: " << dns_status << ")\n";
        return false;
    }

    connectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connectSocket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Socket creation failed. Error: " << WSAGetLastError() << "\n";
        freeaddrinfo(result);
        return false;
    }

    std::cout << "[NET] Connecting to pool socket endpoint...\n";
    if (connect(connectSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "[ERROR] Pool connection refused or timed out. Error: " << WSAGetLastError() << "\n";
        closesocket(connectSocket);
        freeaddrinfo(result);
        return false;
    }
    
    freeaddrinfo(result);
    std::cout << "[NET] Connected successfully! Sending Stratum handshake...\n";

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

     // 🚀 FIX 1: This completely clears out the leftover "Enter" key from the input buffer
    std::cin.ignore(10000, '\n'); 


    selectPool(config);

    // 🚀 FIX: Declared cleanly at the top of the main function scope
    SOCKET poolSocket = INVALID_SOCKET; 

    if (connectToStratum(config, poolSocket)) {
        std::cout << "[SUCCESS] Authenticated on pool!\n";
        g_poolSocketGlobal = poolSocket; 

        std::thread network_worker(listenToPool, poolSocket);
        network_worker.detach();

        std::thread gpu_worker(gpuMiningOrchestrator);
        gpu_worker.detach();

        while (is_mining_running) {
            std::cout << "\033[2J\033[1;1H"; 
            std::cout << "=========================================================\n";
            std::cout << "  ELLEAUTO MINER V1.0 - AMD RX 580 (Ellesmere 8GB)\n";
            std::cout << "=========================================================\n";
            std::cout << " [POOL]    " << config.pool_host << ":" << config.pool_port << "\n";
            std::cout << " [WALLET]  " << config.wallet.substr(0, 15) << "... (Truncated)\n";
            std::cout << "---------------------------------------------------------\n";
            std::cout << " [STATUS]  " << g_network_status_msg << "\n";
            std::cout << "---------------------------------------------------------\n";
            std::cout << " [SHARES]  Submitted: " << g_shares_submitted 
                      << " | Accepted: " << g_shares_accepted 
                      << " | Rejected: " << g_shares_rejected << "\n";
            std::cout << "=========================================================\n";
            std::cout << " Press Ctrl+C to terminate mining safely.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    else {
        // 🚀 NEW: Log if the stratum connection itself refused to handshake
        std::cerr << "\n[CRITICAL] connectToStratum returned false! Handshake failed.\n";
    }

    // 🚀 FIX: This prevents the window from closing under ANY exit condition
    std::cout << "\n=========================================================\n";
    std::cout << " [APPLICATION TERMINATED] Miner thread loop stopped.\n";
    std::cout << "=========================================================\n";
    system("pause"); 
    if (poolSocket != INVALID_SOCKET) {closesocket(poolSocket);}WSACleanup();
    return 0;
}
