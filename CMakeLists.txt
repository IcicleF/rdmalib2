cmake_minimum_required(VERSION 3.10)
project(rdmalib2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wno-sign-compare -Wno-unused-variable -Wno-unused-function -Ofast -march=native")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

include_directories(SYSTEM 3rd/hrpc/include)
include_directories(SYSTEM 3rd/spdlog/include)

execute_process(COMMAND git submodule update --init --recursive)
execute_process(
    COMMAND ofed_info "-n"
    OUTPUT_VARIABLE OFED_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "MLNX_OFED version: ${OFED_VERSION}")

# Force MLNX OFED 4.x
if (OFED_VERSION VERSION_GREATER_EQUAL "5.0-0.0.0.0")
    message(FATAL_ERROR "MLNX_OFED version must be 4.x")
endif()

# Tests
include_directories(SYSTEM include)
link_libraries(ibverbs pthread)

find_package(Catch2 3 REQUIRED)

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp)
foreach(src ${TEST_SOURCES})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src})
    target_link_libraries(${test_name} PRIVATE Catch2::Catch2WithMain)
endforeach()
