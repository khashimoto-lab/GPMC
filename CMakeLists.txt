cmake_minimum_required(VERSION 3.0)
project(gpmc CXX)
option(GPMC_STATIC_BUILD "build gpmc as a static execution file" off)

link_directories(/usr/local/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/Glucose ${CMAKE_CURRENT_SOURCE_DIR}/flow-cutter-pace17/src /usr/local/include)

add_compile_options("$<$<CONFIG:Debug>:-O0>")
add_compile_options(-Wall -Wno-parentheses -Wno-format -Wno-sign-compare -Wno-unused-variable -Wno-unused-result)
add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-Wno-literal-suffix;-Wno-class-memaccess>")
add_compile_options("$<$<CXX_COMPILER_ID:Clang>:-Wno-reserved-user-defined-literal;-Wno-gnu-array-member-paren-init>")
add_compile_options("$<$<CXX_COMPILER_ID:AppleClang>:-Wno-redundant-move;-Wno-undefined-var-template;-Wno-reserved-user-defined-literal;-Wno-gnu-array-member-paren-init;-std=c++11>")

add_subdirectory(Glucose)
add_subdirectory(flow-cutter-pace17)

add_library(gpmc STATIC 
	src/ComponentCache.cc src/ComponentManager.cc src/Counter.cc
	src/Config.cc src/Instance.cc ddnnf/DecisionTree.cc 
	src/preprocessor/Preprocessor.cc src/preprocessor/TestSolver.cc 
	src/preprocessor/lib_sharpsat_td/subsumer.cpp
	src/preprocessor/TreeDecomposition.cc src/preprocessor/IFlowCutter.cc
	)
target_compile_definitions(gpmc 
  PRIVATE $<$<CONFIG:Debug>:DEBUG>
           __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS
)

add_executable(gpmc-bin src/Main.cc)
set_target_properties(gpmc-bin PROPERTIES OUTPUT_NAME gpmc)
target_link_libraries(gpmc-bin gpmc flowcutter glucose z mpfr gmp gmpxx $<$<BOOL:${GPMC_STATIC_BUILD}>:-static>)
