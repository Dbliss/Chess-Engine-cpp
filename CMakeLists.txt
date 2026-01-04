cmake_minimum_required(VERSION 3.20)
project(ChessEngine LANGUAGES CXX)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ChessEngine
    src/main.cpp
    src/chess.cpp
    src/engine.cpp
    src/BoardDisplay.cpp
    src/zobrist.cpp
)

add_executable(PerftSuite
    src/perft_suite.cpp
    src/chess.cpp
    src/zobrist.cpp
)

add_executable(perft_cli
    src/perft_cli.cpp
    src/chess.cpp
    src/zobrist.cpp
)

add_executable(EngineTuning
    src/engine_tuning.cpp
    src/chess.cpp
    src/engine.cpp
    src/BoardDisplay.cpp
    src/zobrist.cpp
)

target_link_libraries(EngineTuning PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
)

target_compile_features(PerftSuite PRIVATE cxx_std_20)


find_package(SFML CONFIG REQUIRED COMPONENTS Graphics Window System Audio)

target_link_libraries(ChessEngine PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
)
