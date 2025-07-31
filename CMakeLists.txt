cmake_minimum_required(VERSION 3.14)
project(gpmc LANGUAGES CXX)

option(GPMC_STATIC_BUILD "build gpmc as a static execution file" OFF)
option(GPMC_USE_STATIC_LIBS "Use static libraries for subprojects" ON)
option(GPMC_BUILD_PYTHON "Build Python bindings via pybind11" OFF)

set(GPMC_USE_STATIC_LIBS ${GPMC_USE_STATIC_LIBS} CACHE BOOL "Use static libraries" FORCE)

link_directories(/usr/local/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/Glucose ${CMAKE_CURRENT_SOURCE_DIR}/flow-cutter-pace17/src /usr/local/include)

add_compile_options("$<$<CONFIG:Debug>:-O0>")
add_compile_options(-Wall -Wno-parentheses -Wno-format -Wno-sign-compare -Wno-unused-variable -Wno-unused-result)
add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-Wno-literal-suffix;-Wno-class-memaccess>")
add_compile_options("$<$<CXX_COMPILER_ID:Clang>:-Wno-reserved-user-defined-literal;-Wno-gnu-array-member-paren-init>")
add_compile_options("$<$<CXX_COMPILER_ID:AppleClang>:-Wno-redundant-move;-Wno-undefined-var-template;-Wno-reserved-user-defined-literal;-Wno-gnu-array-member-paren-init;-std=c++11>")

add_subdirectory(Glucose)
add_subdirectory(flow-cutter-pace17)

set(GPMC_SOURCES
	src/ComponentCache.cc
	src/ComponentManager.cc
	src/Counter.cc
	src/Config.cc
	src/Instance.cc
	ddnnf/DecisionTree.cc 
	src/preprocessor/Preprocessor.cc
	src/preprocessor/TestSolver.cc 
	src/preprocessor/lib_sharpsat_td/subsumer.cpp
	src/preprocessor/TreeDecomposition.cc
	src/preprocessor/IFlowCutter.cc
)

if(GPMC_USE_STATIC_LIBS)
  add_library(gpmc STATIC ${GPMC_SOURCES})
  target_link_libraries(gpmc PRIVATE flowcutter_static glucose_static z mpfr gmp gmpxx)
else()
  add_library(gpmc SHARED ${GPMC_SOURCES})
  target_link_libraries(gpmc PUBLIC flowcutter_shared glucose_shared z mpfr gmp gmpxx)
endif()

target_compile_definitions(gpmc
  PRIVATE $<$<CONFIG:Debug>:DEBUG>
          __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS
)

if(NOT GPMC_BUILD_PYTHON)
  add_executable(gpmc-bin src/Main.cc)
  set_target_properties(gpmc-bin PROPERTIES OUTPUT_NAME gpmc)
  target_link_libraries(gpmc-bin PRIVATE gpmc)
  target_link_options(gpmc-bin PRIVATE $<$<BOOL:${GPMC_STATIC_BUILD}>:-static>)
endif()

# === Pythonバインディング（必要なら） ===
if(GPMC_BUILD_PYTHON)
  set_target_properties(gpmc PROPERTIES POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(python)
endif()