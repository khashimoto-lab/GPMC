cmake_minimum_required(VERSION 3.0)
project(gpmc CXX)

add_subdirectory (preprocessor)
add_subdirectory(flowcutter)

file(GLOB CORE core/*.cc)
file(GLOB UTILS utils/*.cc)
link_directories(/usr/local/lib)
add_executable(gpmc ${CORE} ${UTILS})

target_compile_options(gpmc 
  PRIVATE $<$<CONFIG:Debug>:-O0> -Wall -Wno-parentheses -Wno-format -Wno-sign-compare -Wno-unused-variable -Wno-unused-result
  $<$<CXX_COMPILER_ID:GNU>:-Wno-literal-suffix -Wno-class-memaccess>
  $<$<CXX_COMPILER_ID:Clang>:-Wno-reserved-user-defined-literal -Wno-gnu-array-member-paren-init>
)

target_compile_definitions(gpmc 
  PRIVATE $<$<CONFIG:Debug>:DEBUG>
           __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS
)

target_include_directories(gpmc 
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
          /usr/local/include
)

# cmake >= 3.18 ??
#target_link_directories(gpmc PRIVATE /usr/local/lib) 

target_link_libraries(gpmc z gmp gmpxx preprocessor $<$<CONFIG:Release>:-static>)
# install(TARGETS gpmc DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin/)


add_executable (flow_cutter_pace17 flowcutter/pace.cpp)
# if(STATIC)
SET_TARGET_PROPERTIES (flow_cutter_pace17 PROPERTIES LINK_SEARCH_START_STATIC 1)
# endif(STATIC)
target_link_libraries (flow_cutter_pace17 libflowcutter)