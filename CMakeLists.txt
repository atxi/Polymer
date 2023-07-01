cmake_minimum_required(VERSION 3.2)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

set(VCPKG_TARGET_TRIPLET x64-windows-static)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
  CACHE STRING "Vcpkg toolchain file")

project(polymer VERSION 0.0.1 LANGUAGES CXX)

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

include(GNUInstallDirs)

add_subdirectory(polymer)

set(CPACK_PACKAGE_NAME "Polymer")
set(CPACK_PACKAGE_VENDOR "atxi")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Polymer - Clean-room Minecraft client implementation")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Polymer")

set(CMAKE_INSTALL_PREFIX ".")

install(TARGETS polymer 
        CONFIGURATIONS Debug
        RUNTIME DESTINATION Debug)
        
install(TARGETS polymer 
        CONFIGURATIONS Release
        RUNTIME DESTINATION Release)
