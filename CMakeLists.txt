cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 🚀 FIX 1: Generate a valid OpenCL.lib directly from our text export definition map
# This satisfies the linker on headless cloud systems without installing GPU SDKs!
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib"
    COMMAND link.exe /lib /def:"${CMAKE_CURRENT_SOURCE_DIR}/lib/OpenCL.def" /out:"${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib" /machine:x64
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lib/OpenCL.def"
    COMMENT "Generating cloud compilation linker stubs library..."
)

# Force the tracking tree to treat the generated stub as an official link target
add_custom_target(GenerateOpenCLStub DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib")

add_executable(elleauto 
    src/main.cpp 
    src/stratum_parser.cpp
    src/opencl_manager.cpp
)
add_dependencies(elleauto GenerateOpenCLStub)

# 🚀 FIX 2: Link our freshly generated stub library alongside Windows Sockets
target_link_libraries(elleauto PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib" ws2_32)

add_custom_command(TARGET elleauto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/autolykos.cl"
    "$<TARGET_FILE_DIR:elleauto>/autolykos.cl"
)
