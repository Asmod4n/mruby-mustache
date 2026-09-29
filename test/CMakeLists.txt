find_package(simdjson REQUIRED)

add_executable(spec spec.cpp)
target_link_libraries(spec PRIVATE mustache::mustache simdjson::simdjson)

foreach(module comments delimiters interpolation inverted partials sections ~inheritance)
  add_test(NAME spec.${module} COMMAND spec ${PROJECT_SOURCE_DIR}/refs/mustache-spec/${module}.json)
endforeach()
