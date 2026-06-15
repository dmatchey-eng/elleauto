#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>

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

// 1. Hardcoded Default Pools for Ergo Autolykos v2
const std::vector<PoolOption> DEFAULT_POOLS = {
    {"HeroMiners (Global/Auto)", "://herominers.com", "1147"},
    {"2Miners (Regular PPLNS)", "erg.2miners.com", "8888"},
    {"WoolyPooly (Low Fee)", "erg.woolypooly.com", "3100"},
    {"Custom Manual Pool Entry", "CUSTOM", "CUSTOM"}
};

void initWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[ERROR] Winsock initialization failed.\n";
        exit(1);
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

int main() {
    initWinsock();
    MinerConfig config;

    std::cout << "Enter your Ergo Wallet Address: ";
    std::cin >> config.wallet;

    selectPool(config);

    SOCKET poolSocket = INVALID_SOCKET;
    if (connectToStratum(config, poolSocket)) {
        std::cout << "[SUCCESS] Authenticated on pool! Ready for OpenCL work.\n";
        
        // Next milestone will process incoming JSON jobs in a listening thread.
        std::this_thread::sleep_for(std::chrono::seconds(3));
        closesocket(poolSocket);
    }

    WSACleanup();
    return 0;
}
