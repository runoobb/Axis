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

add_executable(axis_test test/main.cpp)
target_link_libraries(axis_test PRIVATE axis_hw)
