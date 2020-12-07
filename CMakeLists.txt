cmake_minimum_required(VERSION 3.2)

project(polymer VERSION 0.0.1 LANGUAGES CXX)

include(GNUInstallDirs)

add_subdirectory(src)

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
