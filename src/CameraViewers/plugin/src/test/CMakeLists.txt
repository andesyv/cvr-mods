add_executable(interop_test)
add_executable(unit_test)

if (WIN32)
  target_compile_definitions(interop_test PRIVATE _WIN32)
  target_compile_options(interop_test PRIVATE /diagnostics:caret)
endif()

## GLAD OpenGL loader
#target_include_directories(interop_test PRIVATE ${LIB_DIR}/glad/include)
#target_sources(interop_test PRIVATE ${LIB_DIR}/glad/src/glad.c)

# GLBinding
find_package(glbinding CONFIG REQUIRED)
target_link_libraries(interop_test PRIVATE glbinding::glbinding)

# GLFW
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(interop_test PRIVATE glfw)

# GLM
find_package(glm CONFIG REQUIRED)
target_link_libraries(interop_test PRIVATE glm::glm)

# Google Test
find_package(GTest CONFIG REQUIRED)
target_link_libraries(unit_test PRIVATE GTest::gtest GTest::gtest_main)

file(READ shaders/test.vs TEST_VS_SHADER)
file(READ shaders/test.fs TEST_FS_SHADER)

if(WIN32)
  set(VIEWER_APP_PATH ${CMAKE_SOURCE_DIR}/../viewer/target/debug/viewer.exe)
else()
  set(VIEWER_APP_PATH ${CMAKE_SOURCE_DIR}/../viewer/target/debug/viewer)
endif()

configure_file(Resources.hpp.in Resources.hpp @ONLY)
target_include_directories(interop_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_sources(interop_test PRIVATE interop_test.cpp)
target_sources(unit_test PRIVATE unit_tests.cpp)

target_compile_definitions(unit_test PRIVATE UNIT_TEST_MOCK)