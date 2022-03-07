cmake_minimum_required(VERSION 3.2)

file(GLOB_RECURSE SOURCES *.cpp)

find_package(Vulkan REQUIRED)

add_definitions(-DUNICODE)
add_executable(polymer ${SOURCES})

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Ignore VMA nullability warnings
  set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -Wno-nullability-completeness")
endif()

target_include_directories(polymer PRIVATE ${Vulkan_INCLUDE_DIR})
target_link_libraries(polymer ${Vulkan_LIBRARY})
