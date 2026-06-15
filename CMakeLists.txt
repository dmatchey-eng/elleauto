cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Point compiler to your local folder containing include/CL/cl.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Compile main project files AND our custom OpenCL linker stub
add_executable(elleauto 
    src/main.cpp 
    src/stratum_parser.cpp
    src/opencl_manager.cpp
    src/opencl_stub.cpp
)

# Link against standard Windows sockets
target_link_libraries(elleauto PRIVATE ws2_32)

# Copy the hardware kernel file to the build directory automatically
add_custom_command(TARGET elleauto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/autolykos.cl"
    "$<TARGET_FILE_DIR:elleauto>/autolykos.cl"
)
