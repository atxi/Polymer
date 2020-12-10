cmake_minimum_required(VERSION 3.2)

file(GLOB SOURCES *.cpp)

find_package(Vulkan REQUIRED)

add_definitions(-DUNICODE)
add_executable(polymer ${SOURCES})

target_include_directories(polymer PRIVATE ${Vulkan_INCLUDE_DIR})
target_link_libraries(polymer ${Vulkan_LIBRARY})
