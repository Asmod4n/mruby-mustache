cmake_minimum_required(VERSION 3.25)
project(mustache LANGUAGES C CXX)

add_library(mustache INTERFACE)
add_library(mustache::mustache ALIAS mustache)
target_include_directories(mustache INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(mustache INTERFACE cxx_std_20)

add_library(mustache-c src/mustache-c.cpp)
add_library(mustache::mustache-c ALIAS mustache-c)
target_link_libraries(mustache-c PRIVATE mustache)
target_include_directories(mustache-c PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

include(GNUInstallDirs)
install(TARGETS mustache mustache-c EXPORT mustache-targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT mustache-targets NAMESPACE mustache:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mustache
  FILE mustache-config.cmake)


include(CTest)
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_subdirectory(test)
endif()
