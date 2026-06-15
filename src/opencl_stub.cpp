#include <windows.h>

// This empty export stub satisfies the Windows linker requirements during the cloud build
extern "C" __declspec(dllexport) void clGetPlatformIDs() {}
extern "C" __declspec(dllexport) void clGetPlatformInfo() {}
extern "C" __declspec(dllexport) void clGetDeviceIDs() {}
extern "C" __declspec(dllexport) void clGetDeviceInfo() {}
extern "C" __declspec(dllexport) void clCreateContext() {}
extern "C" __declspec(dllexport) void clCreateCommandQueueWithProperties() {}
extern "C" __declspec(dllexport) void clCreateProgramWithSource() {}
extern "C" __declspec(dllexport) void clBuildProgram() {}
extern "C" __declspec(dllexport) void clGetProgramBuildInfo() {}
extern "C" __declspec(dllexport) void clCreateKernel() {}
extern "C" __declspec(dllexport) void clCreateBuffer() {}
extern "C" __declspec(dllexport) void clEnqueueWriteBuffer() {}
extern "C" __declspec(dllexport) void clEnqueueNDRangeKernel() {}
extern "C" __declspec(dllexport) void clEnqueueReadBuffer() {}
extern "C" __declspec(dllexport) void clReleaseKernel() {}
extern "C" __declspec(dllexport) void clReleaseMemObject() {}
extern "C" __declspec(dllexport) void clReleaseProgram() {}
extern "C" __declspec(dllexport) void clReleaseCommandQueue() {}
extern "C" __declspec(dllexport) void clReleaseContext() {}

// 🚀 FIX: Added the missing argument setter function export hook
extern "C" __declspec(dllexport) void clSetKernelArg() {}
