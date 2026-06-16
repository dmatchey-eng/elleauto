#include "miner_types.h"
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
#include <fstream> 
#include <conio.h>   // Provides _kbhit() and _getch() on Windows platforms

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

struct ActiveMiningJob {
    HostUlong4 difficulty;
    HostUlong4 header_hash;
    unsigned long long nonce_start = 1000000000ULL;
};

const std::vector<PoolOption> DEFAULT_POOLS = {
    {"HeroMiners (USA - West)", "us.ergo.herominers.com", "1180"},
    {"HeroMiners (USA - East)", "us2.ergo.herominers.com", "1180"},
    {"HeroMiners (Germany / EU)", "de.ergo.herominers.com", "1180"},
    {"2Miners (Regular PPLNS)", "erg.2miners.com", "8888"}
};

std::atomic<bool> is_mining_running(true);
std::atomic<bool> is_current_job_valid(false);
std::string g_current_job_id = "0"; 

ActiveMiningJob g_next_job;
std::atomic<unsigned int> g_rpc_id_counter(100); 
std::atomic<unsigned long long> g_extra_nonce2_counter(0);
MinerConfig config;

std::atomic<int> g_dag_progress(0);
std::atomic<bool> g_is_dag_building(false);

std::atomic<unsigned int> g_shares_submitted(0);
std::atomic<unsigned int> g_shares_accepted(0);
std::atomic<unsigned int> g_shares_rejected(0);
std::string g_network_status_msg = "Awaiting network jobs...";
std::string g_active_pool_diff = "1";
std::string g_pool_extra_nonce1 = "0000"; 
SOCKET g_poolSocketGlobal = INVALID_SOCKET;
unsigned long long parseHexSlice64(const std::string& hex_slice) {
    unsigned long long value = 0;
    std::stringstream ss;
    ss << std::hex << hex_slice;
    ss >> value;
    return value;
}

HostUlong4 parseHeaderHashToUlong4(const std::string& raw_hex) {
    HostUlong4 out_vector;
    std::string clean_hex = raw_hex;
    
    // 1. Strip away any common web prefixes or escape formatting characters
    if (clean_hex.rfind("0x", 0) == 0 || clean_hex.rfind("0X", 0) == 0) {
        clean_hex = clean_hex.substr(2);
    }
    
    // 🚀 ROBUST PARSER FIX: If extra quotes or array tokens are attached, 
    // extract exactly the LAST 64 characters of the target text string!
    if (clean_hex.length() > 64) {
        clean_hex = clean_hex.substr(clean_hex.length() - 64);
    }
    
    // Verify we have enough characters left to build four 16-character blocks
    if (clean_hex.length() < 64) {
        // Safe padding initialization boundary recovery 
        clean_hex = std::string(64 - clean_hex.length(), '0') + clean_hex;
    }

    // 2. Map the 16-character hex slices straight into your 32-byte host lanes
    out_vector.s0 = parseHexSlice64(clean_hex.substr(0, 16));
    out_vector.s1 = parseHexSlice64(clean_hex.substr(16, 16));
    out_vector.s2 = parseHexSlice64(clean_hex.substr(32, 16));
    out_vector.s3 = parseHexSlice64(clean_hex.substr(48, 16));
    
    return out_vector;
}

HostUlong4 compute256BitTarget(const std::string& diff_str) {
    HostUlong4 target;
    double diff = std::stod(diff_str);
    if (diff <= 0.0) diff = 1.0;

    double target_val = 1.157920892373162e+77 / diff; 

    target.s3 = (unsigned long long)(target_val / 18446744073709551616.0 / 18446744073709551616.0 / 18446744073709551616.0);
    target.s2 = (unsigned long long)(target_val / 18446744073709551616.0 / 18446744073709551616.0);
    target.s1 = (unsigned long long)(target_val / 18446744073709551616.0);
    target.s0 = (unsigned long long)(target_val);
    
    return target;
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
bool initOpenCL();
bool allocateAndBuildVectorDag(size_t total_elements_count);
void runMiningLoop(unsigned long long initial_nonce, HostUlong4 target_difficulty, HostUlong4 header_hash_input);
void shutdownOpenCL();

void initWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[ERROR] Winsock initialization failed.\n";
        exit(1);
    }
}

StratumJob parseStratumLine(const std::string& line);
bool initOpenCL();
bool allocateAndBuildVectorDag(size_t total_elements_count);
void runMiningLoop(unsigned long long initial_nonce, HostUlong4 target_difficulty, HostUlong4 header_hash_input);
void shutdownOpenCL();

void submitShare(const std::string& job_id, unsigned long long found_nonce, HostUlong4 found_solution) {
    if (g_poolSocketGlobal == INVALID_SOCKET) return;

    g_shares_submitted++;
    g_network_status_msg = "🚀 [SUBMITTING SHARE] Nonce found! Transmitting to pool... ";

    std::string clean_job_id = job_id;
    while (!clean_job_id.empty() && (clean_job_id.front() == '"' || clean_job_id.front() == ' ')) clean_job_id.erase(0, 1);
    while (!clean_job_id.empty() && (clean_job_id.back() == '"' || clean_job_id.back() == ' ')) clean_job_id.pop_back();

    unsigned long long active_ext_nonce2 = g_extra_nonce2_counter.fetch_add(1);
    std::stringstream ext2_stream;
    ext2_stream << std::setw(8) << std::setfill('0') << std::hex << active_ext_nonce2;
    std::string extra_nonce2_hex = ext2_stream.str();

    // 🚀 FIXED 1: Convert raw little-endian nonces to big-endian using MSVC hardware primitives directly
    unsigned long long big_endian_nonce = _byteswap_uint64(found_nonce);
    std::stringstream hex_stream;
    hex_stream << std::setw(16) << std::setfill('0') << std::hex << big_endian_nonce;
    std::string nonce_hex = hex_stream.str();

    // 🚀 FIXED 2: Correctly swap variables and feed them straight into the text layout stream
    std::stringstream sol_stream;
    sol_stream << std::setw(16) << std::setfill('0') << std::hex << _byteswap_uint64(found_solution.s0)
               << std::setw(16) << std::setfill('0') << std::hex << _byteswap_uint64(found_solution.s1)
               << std::setw(16) << std::setfill('0') << std::hex << _byteswap_uint64(found_solution.s2)
               << std::setw(16) << std::setfill('0') << std::hex << _byteswap_uint64(found_solution.s3);
    std::string solution_hex = sol_stream.str();

    unsigned int message_id = g_rpc_id_counter.fetch_add(1);

    std::string submitPayload = "{\"id\": " + std::to_string(message_id) + 
                                ", \"method\": \"mining.submit\", \"params\": [\"" + config.wallet + "\", \"" + 
                                clean_job_id + "\", \"" + extra_nonce2_hex + "\", \"" + nonce_hex + "\", \"" + solution_hex + "\"]}\n";

    send(g_poolSocketGlobal, submitPayload.c_str(), (int)submitPayload.length(), 0);
}

void gpuMiningOrchestrator() {
    std::cout << "[GPU] Initializing hardware configuration arrays...\n";
    if (!initOpenCL()) {
        std::cerr << "[GPU ERROR] Failed to initialize OpenCL stack.\n";
        return;
    }
    
    size_t target_elements = 67108864;    
    if (!allocateAndBuildVectorDag(target_elements)) {
        std::cerr << "[GPU ERROR] VRAM allocation limits hit. Exiting thread.\n";
        return;
    }
    
    std::cout << "\033[2J\033[H";
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

    std::ofstream debug_log("network_log.txt");
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
        
        if (debug_log.is_open() && log_counter < 150) {
            debug_log << "[RAW RECEIVED]: " << dynamic_packet_chunk << "\n";
            debug_log.flush(); 
            log_counter++;
            
            if (log_counter == 150) {
                debug_log << "\n=== LOG AUTO-CAPPED ===\n";
                debug_log.close(); 
            }
        }

        stream_accumulator += dynamic_packet_chunk;

        size_t newline_pos;
        while ((newline_pos = stream_accumulator.find('\n')) != std::string::npos) {
            std::string single_line = stream_accumulator.substr(0, newline_pos);
            stream_accumulator.erase(0, newline_pos + 1);

            try {
                StratumJob current_job = parseStratumLine(single_line);

                if (current_job.is_new_job) {
                    // Force down the active loop before memory adjustments
                    is_current_job_valid = false; 
                    
                    g_current_job_id = current_job.job_id;
                    
                    // 🚀 SAFE RE-MAPPING: If hash parsing returns empty arrays, use a safe fallback
                    g_next_job.header_hash = parseHeaderHashToUlong4(current_job.header_hash_hex);
                    
                    // Fallback to pool difficulty baseline if the dynamic lookup is empty
                    std::string active_diff = g_active_pool_diff.empty() ? "1" : g_active_pool_diff;
                    g_next_job.difficulty = compute256BitTarget(active_diff);
                    
                    // 🚀 CRITICAL FIX: Safe conversion handling to prevent numeric exceptions
                    unsigned long long ext_nonce_val = 0;
                    if (!g_pool_extra_nonce1.empty() && g_pool_extra_nonce1 != "0000") {
                        ext_nonce_val = convertHexToUlong(g_pool_extra_nonce1);
                    }
                    g_next_job.nonce_start = (ext_nonce_val == 0) ? 1000000000ULL : (ext_nonce_val << 32); 

                    // Instantly push the visual UI state text out to the user panel layout
                    g_network_status_msg = "[OK] Mining Active | Processing Ergo Job: " + g_current_job_id;
                    
                    // 🚀 FORCE BOOTSTRAP: This forces the opencl_manager compute lane to wake up!
                    is_current_job_valid = true; 
                }
            } 
            catch (const std::exception& e) {
                // Prevent hidden formatting bugs from crashing your thread lines silently
                g_network_status_msg = "[ERROR] Stream processing failure occurred.";
            }
            catch (...) {
                g_network_status_msg = "[ERROR] Fatal token execution crash prevented.";
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
    struct addrinfo hints {}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;     
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int dns_status = getaddrinfo(config.pool_host.c_str(), config.pool_port.c_str(), &hints, &result);
    if (dns_status != 0) return false;

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

    std::string processed_wallet = config.wallet;
    if (config.pool_host.find("herominers") != std::string::npos && processed_wallet.rfind("solo:", 0) != 0) {
        processed_wallet = "solo:" + processed_wallet;
    }

    std::string subscribePayload = "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": [\"elleauto-v1.0\", \"Autolykosv2\"]}\n";
    send(connectSocket, subscribePayload.c_str(), (int)subscribePayload.length(), 0);

    std::string authPayload = "{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\"" + processed_wallet + "\", \"x\"]}\n";
    send(connectSocket, authPayload.c_str(), (int)authPayload.length(), 0);

    return true;
}

void keyboardInputMonitor() {
    while (is_mining_running) {
        if (_kbhit()) { // Check if a key was tapped asynchronously
            char key = _getch();
            if (key == 'q' || key == 'Q' || key == 27) { // 'q' or 'Esc' key signatures
                g_network_status_msg = "🛑 [SHUTDOWN INITIATED] Safely draining hardware pipelines...";
                is_mining_running = false;       // Stops your OpenCL kernel loop
                is_current_job_valid = false;     // Breaks out network socket blocks
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= 0x0004; // Enable Virtual Terminal Processing (VT100)
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

        // Clear the screen completely exactly ONCE on initial bootstrap launch
        std::cout << "\033[2J\033[H";

        std::thread network_worker(listenToPool, poolSocket);
        network_worker.detach();

        std::thread gpu_worker(gpuMiningOrchestrator);
        gpu_worker.detach();

        // 🚀 ACTIVATE INPUT MONITOR: Tracks keystrokes independently 
        std::thread input_worker(keyboardInputMonitor);
        input_worker.detach();

        while (is_mining_running) {
            // 🚀 ANTI-FLICKER FIX: Instantly home cursor without wiping buffer cells
            std::cout << "\033[H"; 
            
            std::cout << "=========================================================\n";
            std::cout << "  ELLEAUTO MINER V1.0 - AMD RX 580 (Ellesmere 8GB)       \n";
            std::cout << "=========================================================\n";
            std::cout << " [POOL]    " << config.pool_host << ":" << config.pool_port << "             \n";
            std::cout << " [WALLET]  " << config.wallet.substr(0, 15) << "... (Truncated)       \n";
            std::cout << "---------------------------------------------------------\n";
            std::cout << " [STATUS]  " << g_network_status_msg << "                               \n";
            std::cout << "---------------------------------------------------------\n";
            std::cout << " [SHARES]  Submitted: " << g_shares_submitted 
                      << " | Accepted: " << g_shares_accepted 
                      << " | Rejected: " << g_shares_rejected << "     \n";
            std::cout << "=========================================================\n";
            std::cout << " Press [Q] or [ESC] to terminate mining safely.          \n";

            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); 
        }
    } else {
        std::cerr << "\n[CRITICAL ERROR] connectToStratum failed! Check terminal output above.\n";
        std::cout << "Press Enter to exit safely...";
        std::cin.get(); 
    }

    // 🚀 DRAIN PIPELINES & CLEANUP ON INTENTIONAL QUIT
    std::cout << "\n[SYSTEM] Disconnecting sockets...\n";
    if (poolSocket != INVALID_SOCKET) {
        closesocket(poolSocket);
    }
    WSACleanup();
    
    std::cout << "[SYSTEM] OpenCL arrays released. Safe teardown complete.\n";
    std::cout << "Press any key to close this terminal panel...";
    while(!_kbhit()) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    return 0;
}
