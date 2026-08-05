cmake_minimum_required(VERSION 3.16)

project(axis_systemc_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SYSTEMC_HOME /opt/systemc CACHE PATH "SystemC installation prefix")

add_executable(axis_test
    test/main.cpp
)

target_include_directories(axis_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/module
    ${SYSTEMC_HOME}/include
)
target_link_directories(axis_test PRIVATE ${SYSTEMC_HOME}/lib)
target_link_libraries(axis_test PRIVATE systemc pthread)
