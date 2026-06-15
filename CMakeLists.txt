cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCL REQUIRED)

include_directories(${OpenCL_INCLUDE_DIRS})

# Added src/stratum_parser.cpp to compilation sources
add_executable(elleauto 
    src/main.cpp 
    src/stratum_parser.cpp
)

target_link_libraries(elleauto PRIVATE OpenCL::OpenCL ws2_32)
