#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// Function to read the autolykos.cl file from disk
std::string readKernelSource(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[GPU] Failed to open kernel file: " << filepath << "\n";
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool initOpenCL() {
    cl_uint platformCount;
    clGetPlatformIDs(0, nullptr, &platformCount);
    if (platformCount == 0) {
        std::cerr << "[GPU ERROR] No OpenCL platforms found on this Windows system.\n";
        return false;
    }

    std::vector<cl_platform_id> platforms(platformCount);
    clGetPlatformIDs(platformCount, platforms.data(), nullptr);

    cl_device_id targetDevice = nullptr;
    bool foundAMD = false;

    // 1. Enumerate and discover the AMD RX 580
    for (auto platform : platforms) {
        char platformName[128];
        clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platformName), platformName, nullptr);
        std::string platStr(platformName);

        if (platStr.find("AMD") != std::string::npos || platStr.find("Advanced Micro Devices") != std::string::npos) {
            cl_uint deviceCount;
            clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &deviceCount);
            
            std::vector<cl_device_id> devices(deviceCount);
            clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, deviceCount, devices.data(), nullptr);

            for (auto device : devices) {
                char deviceName[256];
                clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(deviceName), deviceName, nullptr);
                std::string devStr(deviceName);

                std::cout << "[GPU] Discovered Device: " << devStr << "\n";
                
                // Matches standard AMD RX 580 string architectures
                if (devStr.find("580") != std::string::npos || devStr.find("Ellesmere") != std::string::npos) {
                    targetDevice = device;
                    foundAMD = true;
                    std::cout << "[GPU SUCCESS] Targeted AMD RX 580 (Ellesmere) for mining context.\n";
                    break;
                }
            }
        }
        if (foundAMD) break;
    }

    if (!targetDevice) {
        std::cerr << "[GPU ERROR] Could not bind specifically to an AMD RX 580 8GB GPU.\n";
        return false;
    }

    // 2. Initialize the Execution Context
    cl_int err;
    cl_context context = clCreateContext(nullptr, 1, &targetDevice, nullptr, nullptr, &err);
    cl_command_queue queue = clCreateCommandQueueWithProperties(context, targetDevice, nullptr, &err);

    // 3. Load the Autolykos v2 OpenCL file
    std::string kernelSource = readKernelSource("src/autolykos.cl");
    if (kernelSource.empty()) return false;

    const char* sourcePtr = kernelSource.c_str();
    size_t sourceLen = kernelSource.length();
    cl_program program = clCreateProgramWithSource(context, 1, &sourcePtr, &sourceLen, &err);

    // Build the OpenCL kernel binary optimized for the Polaris architecture
    err = clBuildProgram(program, 1, &targetDevice, "-cl-mad-enable -cl-fast-relaxed-math", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "[GPU ERROR] Kernel compilation failed.\n";
        // Log compilation errors from the AMD compiler engine
        size_t logSize;
        clGetProgramBuildInfo(program, targetDevice, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::vector<char> buildLog(logSize);
        clGetProgramBuildInfo(program, targetDevice, CL_PROGRAM_BUILD_LOG, logSize, buildLog.data(), nullptr);
        std::cerr << buildLog.data() << "\n";
        return false;
    }

    std::cout << "[GPU] Autolykos v2 OpenCL code successfully compiled for hardware!\n";
    return true;
}
