cmake_minimum_required(VERSION 3.2)

file(GLOB SOURCES *.cpp)

add_executable(polymer ${SOURCES})
target_link_libraries(polymer)
