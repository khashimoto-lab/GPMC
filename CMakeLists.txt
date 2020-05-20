cmake_minimum_required(VERSION 3.5)
project(mc2020 CXX)

file(GLOB CORE core/*.cc)
file(GLOB UTILS utils/*.cc)
add_executable(gpmc ${CORE} ${UTILS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_CXX_FLAGS "-O3 -D NDEBUG -Wall -Wno-parentheses -Wno-format -Wno-sign-compare -Wno-literal-suffix -Wno-misleading-indentation -std=c++11")
add_definitions(-D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)
target_link_libraries(gpmc z gmp gmpxx)
set_target_properties(gpmc PROPERTIES LINK_FLAGS "-static")
install(TARGETS gpmc DESTINATION ${CMAKE_CURRENT_SOURCE_DIR})
