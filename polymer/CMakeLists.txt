cmake_minimum_required(VERSION 3.2)

file(GLOB_RECURSE SOURCES *.cpp)

find_package(Vulkan REQUIRED)
find_package(CURL REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_definitions(-DUNICODE)
add_executable(polymer ${SOURCES})
include_directories(polymer PRIVATE ..)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Ignore VMA nullability warnings
  set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -Wno-nullability-completeness")
endif()

target_include_directories(polymer PRIVATE ${Vulkan_INCLUDE_DIR} ${CURL_INCLUDE_DIRS})
target_link_libraries(polymer ${Vulkan_LIBRARY} CURL::libcurl)
