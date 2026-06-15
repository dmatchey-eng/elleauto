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
#include <fstream> // Added for local text file debug log tracking

#pragma comment(lib, "Ws2_32.lib")

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
// 🚀 FIX: Overwrite the list at the bottom of Section 1 in src/main.cpp
const std::vector<PoolOption> DEFAULT_POOLS = {
    {"HeroMiners (USA - West)", "us.ergo.herominers.com", "1180"},
    {"HeroMiners (USA - East)", "us2.ergo.herominers.com", "1180"},
    {"HeroMiners (Germany / EU)", "de.ergo.herominers.com", "1180"},
    {"2Miners (Regular PPLNS)", "://2miners.com", "8888"}
};

// Global Thread & State Synced Variables
std::atomic<bool> is_mining_running(true);
std::atomic<bool> is_current_job_valid(false);
// 🚀 FIX: This forces the string variable memory signature to remain visible to opencl_manager.cpp
std::string g_current_job_id = "0"; 

ActiveMiningJob g_next_job;
    MinerConfig config;

std::atomic<int> g_dag_progress(0);
std::atomic<bool> g_is_dag_building(false);

std::atomic<unsigned int> g_shares_submitted(0);
std::atomic<unsigned int> g_shares_accepted(0);
std::atomic<unsigned int> g_shares_rejected(0);
std::string g_network_status_msg = "Awaiting network jobs...";

// Forward Declarations of Subsystems
StratumJob parseStratumLine(const std::string& line);
bool initOpenCL();
bool allocateAndBuildVectorDag(size_t total_elements_count);
void runMiningLoop(unsigned long long initial_nonce, unsigned long long difficulty_target, unsigned long long header_hash_input);
void shutdownOpenCL();
void initWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[ERROR] Winsock initialization failed.\n";
        exit(1);
    }
}

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

SOCKET g_poolSocketGlobal = INVALID_SOCKET;
std::atomic<unsigned int> g_rpc_id_counter(3);

void submitShare(const std::string& job_id, unsigned long long found_nonce) {
    if (g_poolSocketGlobal == INVALID_SOCKET) return;

    g_shares_submitted++;
    g_network_status_msg = "🚀 [SUBMITTING SHARE] Nonce found! Transmitting to pool... ";

    // Sanitizing the Job ID from any accidental double quotes
    std::string clean_job_id = job_id;
    while (!clean_job_id.empty() && (clean_job_id.front() == '"' || clean_job_id.front() == ' ')) {
        clean_job_id.erase(0, 1);
    }
    while (!clean_job_id.empty() && (clean_job_id.back() == '"' || clean_job_id.back() == ' ')) {
        clean_job_id.pop_back();
    }

    // Format the found nonce to exactly 16 hex characters, zero-padded, no "0x"
    std::stringstream hex_stream;
    hex_stream << std::setw(16) << std::setfill('0') << std::hex << found_nonce;
    std::string nonce_hex = hex_stream.str();

    unsigned int message_id = g_rpc_id_counter.fetch_add(1);

    // 🚀 FIX: Query your actual wallet config variable instead of hardcoding "elleauto-worker"!
    extern MinerConfig config; // Access the main runtime configuration struct block
    
    // Construct the strict stratum parameters format: [wallet, job_id, extra_nonce2, nonce]
    std::string submitPayload = "{\"id\": " + std::to_string(message_id) + 
                                ", \"method\": \"mining.submit\", \"params\": [\"" + 
                                config.wallet + "\", \"" + clean_job_id + "\", \"00000000\", \"" + nonce_hex + "\"]}\n";

    send(g_poolSocketGlobal, submitPayload.c_str(), (int)submitPayload.length(), 0);
}

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

void listenToPool(SOCKET poolSocket) {
    char recv_buffer[4096]; 
    std::string stream_accumulator = "";

    // 🚀 FIX 1: Truncate (wipe) the file on launch by omitting std::ios::app
    // This ensures old data from previous runs is deleted instantly.
    std::ofstream debug_log("network_log.txt");
    
    // 🚀 FIX 2: Counter to automatically cap the log size
    int log_counter = 0; 

    if (debug_log.is_open()) {
        debug_log << "=== Fresh Miner Launch: Stratum Handshake Initiated ===\n";
    }

    while (is_mining_running) {
        int bytesReceived = recv(poolSocket, recv_buffer, sizeof(recv_buffer) - 1, 0);
        if (bytesReceived <= 0) {
            std::cerr << "\n[ERROR] Connection lost or stream rejected by pool server.\n";
            is_mining_running = false;
            is_current_job_valid = false;
            break;
        }

        recv_buffer[bytesReceived] = '\0'; 
        std::string dynamic_packet_chunk(recv_buffer, bytesReceived);
        
        // 🚀 FIX 3: Auto-cap logging after 150 incoming network packets
        if (debug_log.is_open() && log_counter < 150) {
            debug_log << "[RAW RECEIVED]: " << dynamic_packet_chunk << "\n";
            debug_log.flush(); 
            log_counter++;
            
            if (log_counter == 150) {
                debug_log << "\n=== LOG AUTO-CAPPED: Stopping file writes to prevent storage bloat. ===\n";
                debug_log.close(); // Safely disconnect file handle early
            }
        }

        stream_accumulator += dynamic_packet_chunk;

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
    if (debug_log.is_open()) debug_log.close();
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
    std::cout << "[NET Debug] Attempting DNS resolution for host: " << config.pool_host 
              << " on port: " << config.pool_port << "\n";

    struct addrinfo hints {}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;     
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

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
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            // Hexadecimal code for ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x0004)
            // Using the raw number ensures it compiles perfectly on any environment
            dwMode |= 0x0004; 
            SetConsoleMode(hOut, dwMode);
        }
    }
    initWinsock();

    std::cout << "Enter your Ergo Wallet Address: ";
    std::cin >> config.wallet;

    std::cin.ignore(10000, '\n'); 
    selectPool(config);

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
    } else {
        std::cerr << "\n[CRITICAL] connectToStratum returned false! Handshake failed.\n";
    }

    std::cout << "\n=========================================================\n";
    std::cout << " [APPLICATION TERMINATED] Miner thread loop stopped.\n";
    std::cout << "=========================================================\n";
    system("pause"); 

    if (poolSocket != INVALID_SOCKET) {
        closesocket(poolSocket);
    }
    WSACleanup();
    return 0;
}
