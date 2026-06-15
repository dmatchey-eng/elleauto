cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(elleauto 
    src/main.cpp 
    src/stratum_parser.cpp
    src/opencl_manager.cpp
)

# 🚀 FIX: Completely removed opencl.lib from the build requirements.
# This prevents GitHub from failing, and we handle the driver dynamically instead!
target_link_libraries(elleauto PRIVATE ws2_32)

add_custom_command(TARGET elleauto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/autolykos.cl"
    "$<TARGET_FILE_DIR:elleauto>/autolykos.cl"
)
