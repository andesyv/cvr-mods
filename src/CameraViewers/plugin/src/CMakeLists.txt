target_sources(unity-plugin PRIVATE main.cpp)

if (${BUILD_TEST})
  add_subdirectory(test)
  target_sources(interop_test
    PRIVATE common_gl.hpp
    PRIVATE common_gl.cpp
    PRIVATE ipc.hpp
    PRIVATE ipc.cpp
  )
  target_sources(unit_test
    PRIVATE ipc.hpp
    PRIVATE ipc.cpp
  )
  target_include_directories(interop_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(unit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif ()