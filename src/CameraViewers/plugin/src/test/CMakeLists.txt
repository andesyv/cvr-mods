file(READ shaders/test.vs TEST_VS_SHADER)
file(READ shaders/test.fs TEST_FS_SHADER)

configure_file(Resources.hpp.in Resources.hpp @ONLY)
target_include_directories(test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_sources(test PRIVATE test.cpp)