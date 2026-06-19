cmake_minimum_required(VERSION 3.10)

project(amalgam LANGUAGES CXX)

find_package(Threads REQUIRED)

set(REQUIRED CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(incbin INTERFACE)
target_include_directories (incbin INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/incbin
)

add_library(chess-library INTERFACE)
target_include_directories (chess-library INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/deps/chess-library
)

add_executable(amalgam)
target_link_libraries(amalgam PRIVATE incbin chess-library Threads::Threads)
target_compile_options(amalgam PRIVATE -march=native)
target_sources(amalgam PRIVATE
  src/main.cpp
  src/utils.cpp
  src/moveselect.cpp
  src/uci.cpp
  src/nnue.cpp
  src/search.cpp
)
