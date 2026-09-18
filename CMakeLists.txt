cmake_minimum_required(VERSION 3.16)

project(axis_systemc_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SYSTEMC_HOME /opt/systemc CACHE PATH "SystemC installation prefix")

file(GLOB AXIS_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(axis_hw ${AXIS_SOURCES})
target_include_directories(axis_hw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SYSTEMC_HOME}/include
)
target_link_directories(axis_hw PUBLIC ${SYSTEMC_HOME}/lib)
target_link_libraries(axis_hw PUBLIC systemc pthread)

set(AXIS_TEST_CASES
    # 01_source_to_sink
    # 02_source_broadcast_to_sink
    # 03_alu_two_input
    03_alu_two_input
    # 04_arbiter
    04_arbiter
    # 05_sram
    05_sram
    06_fifo
)

enable_testing()

foreach(test_case IN LISTS AXIS_TEST_CASES)
    set(test_target axis_test_${test_case})
    add_executable(${test_target} test/${test_case}/main.cpp)
    target_link_libraries(${test_target} PRIVATE axis_hw)
    target_compile_definitions(${test_target}
        PRIVATE AXIS_TEST_TRACE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/${test_case}"
    )
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()
