cmake_minimum_required(VERSION 3.20)
project(ChessEngine LANGUAGES CXX)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Your executable
add_executable(ChessEngine
    src/main.cpp
    src/chess.cpp
    src/engine1.cpp
    src/engine2.cpp
    src/BoardDisplay.cpp
    src/zobrist.cpp
)

# SFML via vcpkg
find_package(SFML CONFIG REQUIRED COMPONENTS Graphics Window System)

target_link_libraries(ChessEngine PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
)
