cmake_minimum_required(VERSION 3.16.0)

project(unity-plugin)

# Project settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# $<$<CONFIG:>:> hacky generator that removes any multi-config folders generated (e.g. msvc)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/$<$<CONFIG:>:>)

# Output executable
add_executable(unity-plugin)

# Source files
add_subdirectory(src)
