target_sources(unity-plugin PRIVATE main.cpp)

if(${BUILD_TEST})
  add_subdirectory(test)
endif()