cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCL REQUIRED)

include_directories(${OpenCL_INCLUDE_DIRS})
add_executable(elleauto src/main.cpp)

# Link both OpenCL and Windows Sockets library
target_link_libraries(elleauto PRIVATE OpenCL::OpenCL ws2_32)
