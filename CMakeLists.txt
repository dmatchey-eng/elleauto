cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# 1. Locate the OpenCL framework libraries on Windows 10
find_package(OpenCL REQUIRED)

# 2. Tell the compiler where to look for local headers (include/CL)
include_directories(${OpenCL_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 3. Explicitly link all of your newly written source files
add_executable(elleauto 
    src/main.cpp 
    src/stratum_parser.cpp
    src/opencl_manager.cpp
)

# 4. Link against OpenCL and standard Windows network socket libraries
target_link_libraries(elleauto PRIVATE OpenCL::OpenCL ws2_32)

# 5. AUTOMATION: Copy autolykos.cl to the output directory automatically on build
add_custom_command(TARGET elleauto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/autolykos.cl"
    "$<TARGET_FILE_DIR:elleauto>/autolykos.cl"
)
