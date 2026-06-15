cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Search for the OpenCL library on Windows
find_package(OpenCL REQUIRED)

# Include both global OpenCL paths and our local include directory
include_directories(${OpenCL_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(elleauto 
    src/main.cpp 
    src/stratum_parser.cpp
    src/opencl_manager.cpp    # Added new file
)

target_link_libraries(elleauto PRIVATE OpenCL::OpenCL ws2_32)
