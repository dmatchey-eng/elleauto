#include <windows.h>

// 🚀 FIX: Renamed symbols so they satisfy the linker but never hijack real OS runtime DLL commands
extern "C" __declspec(dllexport) void clGetPlatformIDs_stub() {}
extern "C" __declspec(dllexport) void clGetPlatformInfo_stub() {}
extern "C" __declspec(dllexport) void clGetDeviceIDs_stub() {}
extern "C" __declspec(dllexport) void clGetDeviceInfo_stub() {}
extern "C" __declspec(dllexport) void clCreateContext_stub() {}
extern "C" __declspec(dllexport) void clCreateCommandQueueWithProperties_stub() {}
extern "C" __declspec(dllexport) void clCreateProgramWithSource_stub() {}
extern "C" __declspec(dllexport) void clBuildProgram_stub() {}
extern "C" __declspec(dllexport) void clGetProgramBuildInfo_stub() {}
extern "C" __declspec(dllexport) void clCreateKernel_stub() {}
extern "C" __declspec(dllexport) void clCreateBuffer_stub() {}
extern "C" __declspec(dllexport) void clEnqueueWriteBuffer_stub() {}
extern "C" __declspec(dllexport) void clEnqueueNDRangeKernel_stub() {}
extern "C" __declspec(dllexport) void clEnqueueReadBuffer_stub() {}
extern "C" __declspec(dllexport) void clReleaseKernel_stub() {}
extern "C" __declspec(dllexport) void clReleaseMemObject_stub() {}
extern "C" __declspec(dllexport) void clReleaseProgram_stub() {}
extern "C" __declspec(dllexport) void clReleaseCommandQueue_stub() {}
extern "C" __declspec(dllexport) void clReleaseContext_stub() {}
extern "C" __declspec(dllexport) void clSetKernelArg_stub() {}
