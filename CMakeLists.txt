cmake_minimum_required(VERSION 3.15)
project(elleauto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib"
    COMMAND link.exe /lib /def:"${CMAKE_CURRENT_SOURCE_DIR}/lib/OpenCL.def" /out:"${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib" /machine:x64
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lib/OpenCL.def"
    COMMENT "Generating cloud compilation linker stubs library..."
)

add_custom_target(GenerateOpenCLStub DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib")

add_executable(elleauto 
    src/main.cpp 
    src/stratum_parser.cpp
    src/opencl_manager.cpp
)
add_dependencies(elleauto GenerateOpenCLStub)

target_link_libraries(elleauto PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/OpenCL.lib" ws2_32)

add_custom_command(TARGET elleauto POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/autolykos.cl"
    "$<TARGET_FILE_DIR:elleauto>/autolykos.cl"
)
