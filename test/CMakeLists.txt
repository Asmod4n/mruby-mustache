find_package(simdjson REQUIRED)

add_executable(spec spec.cpp)
target_link_libraries(spec PRIVATE mustache::mustache mustache::mustache-c simdjson::simdjson)

foreach(module comments delimiters interpolation inverted partials sections ~inheritance)
  add_test(NAME spec.${module} COMMAND spec ${PROJECT_SOURCE_DIR}/refs/mustache-spec/${module}.json)
  add_test(NAME spec-c.${module} COMMAND spec ${PROJECT_SOURCE_DIR}/refs/mustache-spec/${module}.json c)
endforeach()

add_executable(output output.cpp)
target_link_libraries(output PRIVATE mustache::mustache mustache::mustache-c)
add_test(NAME output COMMAND output)

add_executable(c_api c_api.c)
target_link_libraries(c_api PRIVATE mustache::mustache-c)
set_target_properties(c_api PROPERTIES LINKER_LANGUAGE CXX C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME c_api COMMAND c_api)
