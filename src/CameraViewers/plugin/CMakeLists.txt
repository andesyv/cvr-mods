cmake_minimum_required(VERSION 3.16.0)

set(LIB_DIR ${CMAKE_SOURCE_DIR}/../../../lib)

option(BUILD_TEST "Build test application" ON)

project(unity-plugin)

# Project settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# $<$<CONFIG:>:> hacky generator that removes any multi-config folders generated (e.g. msvc)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin/$<$<CONFIG:>:>)

# Output executable
add_library(unity-plugin SHARED)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "com.andough.cameraviewers.renderingplugin")

target_include_directories(unity-plugin PRIVATE ${LIB_DIR}/UnityPluginAPI)

## On linux, we make use of the system function pidfd_getfd, which requires Linux Kernel >= 5.6
#if(UNIX)
#  find_program(UNAME_EXEC uname)
#  execute_process(COMMAND ${UNAME_EXEC} -r
#    OUTPUT_VARIABLE KERNEL_VERSION_STR
#    OUTPUT_STRIP_TRAILING_WHITESPACE
#  )
#
##  string(REGEX MATCH "([0-9]+\.[0-9]+)" KERNEL_VERSION ${KERNEL_VERSION_STR})
#
#  if(${KERNEL_VERSION_STR} VERSION_LESS "5.6")
#    message(FATAL_ERROR "A linux Kernel >= 5.6 is required")
#  endif()
#endif()

# Source files
add_subdirectory(src)
