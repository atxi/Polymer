cmake_minimum_required(VERSION 3.2)

file(GLOB_RECURSE SOURCES *.cpp)

find_package(Vulkan REQUIRED)
find_package(CURL REQUIRED)

if (UNIX)
  find_package(glfw3 CONFIG REQUIRED)

  list(FILTER SOURCES EXCLUDE REGEX "/platform/")
  file(GLOB_RECURSE UNIX_SOURCES ${PROJECT_SOURCE_DIR}/polymer/platform/unix/*.cpp)
  list(APPEND SOURCES ${UNIX_SOURCES})

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
elseif(WIN32)
  file(GLOB_RECURSE WIN32_SOURCES ${PROJECT_SOURCE_DIR}/polymer/platform/win32/*.cpp)
  list(APPEND SOURCES ${WIN32_SOURCES})

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

add_definitions(-DUNICODE)
add_executable(polymer ${SOURCES})
include_directories(polymer PRIVATE ..)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Ignore VMA nullability warnings
  set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -Wno-nullability-completeness -Wplacement-new=")
endif()

if (UNIX)
  target_link_libraries(polymer PRIVATE glfw)
endif()

target_include_directories(polymer PRIVATE ${Vulkan_INCLUDE_DIR} ${CURL_INCLUDE_DIRS})
target_link_libraries(polymer PRIVATE ${Vulkan_LIBRARY} CURL::libcurl)
