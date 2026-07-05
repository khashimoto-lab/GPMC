cmake_minimum_required(VERSION 3.14)
project(gpmc VERSION 1.2.0 LANGUAGES CXX C)

set(GPMC_VERSION "${PROJECT_VERSION}")

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_COMMIT "unknown")
endif()

configure_file(cli/Version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/cli/Version.hpp @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GPMC_STATIC_BUILD "Build gpmc as a statically linked binary" OFF)
option(GPMC_BUILD_PP "Build the standalone gpmc-pp preprocessor (for developers/researchers)" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(GPMC_COMPILE_OPTIONS
  -Wall -Wno-parentheses -Wno-format -Wno-sign-compare
  -Wno-unused-variable -Wno-unused-result -Wno-misleading-indentation
  "$<$<CXX_COMPILER_ID:GNU>:-Wno-literal-suffix;-Wno-class-memaccess>"
  "$<$<CXX_COMPILER_ID:Clang>:-Wno-reserved-user-defined-literal;-Wno-gnu-array-member-paren-init;-Wno-redundant-move>"
  "$<$<CXX_COMPILER_ID:AppleClang>:-Wno-reserved-user-defined-literal;-Wno-gnu-array-member-paren-init;-Wno-redundant-move;-Wno-undefined-var-template>"
  "$<$<CONFIG:Release>:-O3>"
  "$<$<CONFIG:Debug>:-O0;-g>"
)

# --- GMP ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GMP QUIET gmp)
  pkg_check_modules(GMPXX QUIET gmpxx)
endif()
if(GMP_FOUND AND GMPXX_FOUND)
  set(GMP_LIBRARIES ${GMP_LIBRARIES} ${GMPXX_LIBRARIES})
  set(GMP_INCLUDE_DIRS ${GMP_INCLUDE_DIRS} ${GMPXX_INCLUDE_DIRS})
else()
  set(GMP_LIBRARIES gmp gmpxx)
  # FreeBSD, some Linux distros, and Homebrew on Apple Silicon put GMP
  # headers outside the compiler's default search path.
  foreach(_gmp_hint /usr/local/include /opt/local/include /usr/pkg/include /opt/homebrew/include)
    if(EXISTS "${_gmp_hint}/gmpxx.h")
      set(GMP_INCLUDE_DIRS ${_gmp_hint})
      break()
    endif()
  endforeach()
endif()
if(GMP_INCLUDE_DIRS)
  include_directories(${GMP_INCLUDE_DIRS})
endif()
# Same deal for the libs: FreeBSD/some Linux distros use /usr/local/lib,
# Homebrew on Apple Silicon uses /opt/homebrew/lib.
foreach(_gmp_lib_hint /usr/local/lib /opt/local/lib /usr/pkg/lib /opt/homebrew/lib)
  if(EXISTS "${_gmp_lib_hint}/libgmp.so" OR EXISTS "${_gmp_lib_hint}/libgmp.a" OR EXISTS "${_gmp_lib_hint}/libgmp.dylib")
    link_directories(${_gmp_lib_hint})
    break()
  endif()
endforeach()

# `-static` (GPMC_STATIC_BUILD) needs libgmp.a/libgmpxx.a, not just the .so
# most package managers install by default. Fail with a clear message instead
# of an opaque linker error ("cannot find -lgmp") when they're missing.
if(GPMC_STATIC_BUILD)
  find_library(GMP_STATIC_LIB NAMES libgmp.a)
  find_library(GMPXX_STATIC_LIB NAMES libgmpxx.a)
  if(NOT GMP_STATIC_LIB OR NOT GMPXX_STATIC_LIB)
    message(FATAL_ERROR
      "GPMC_STATIC_BUILD requires static GMP libraries (libgmp.a and "
      "libgmpxx.a), which were not found. The dynamic .so is not enough for "
      "a static build. On Debian/Ubuntu, `apt install libgmp-dev` normally "
      "provides both; on macOS (Homebrew), `brew install gmp` does not "
      "install static archives, so a static GPMC build is not supported "
      "there without building GMP from source.")
  endif()
endif()

# --- CaDiCaL ---
# The vendored CaDiCaL under extern/cadical/ is kept pristine. Our changes live
# as patches in extern/cadical-patches/ and are applied at configure time to a
# copy in the build tree (cadical-patched/). We then build that patched copy.
# This keeps extern/cadical/ untouched. See extern/cadical-patches/README.md.
set(CADICAL_SRC_DIR     "${CMAKE_SOURCE_DIR}/extern/cadical")
set(CADICAL_PATCHED_DIR "${CMAKE_BINARY_DIR}/cadical-patched")
set(CADICAL_PATCH_DIR   "${CMAKE_SOURCE_DIR}/extern/cadical-patches")

# Fresh copy of the pristine sources (the patched tree is a build artifact).
file(REMOVE_RECURSE "${CADICAL_PATCHED_DIR}")
file(COPY "${CADICAL_SRC_DIR}/" DESTINATION "${CADICAL_PATCHED_DIR}")

# Apply every extern/cadical-patches/*.patch in sorted order via `git apply`.
file(GLOB CADICAL_PATCHES "${CADICAL_PATCH_DIR}/*.patch")
list(SORT CADICAL_PATCHES)
foreach(patch ${CADICAL_PATCHES})
  message(STATUS "Applying CaDiCaL patch: ${patch}")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} apply -p1 --whitespace=nowarn "${patch}"
    WORKING_DIRECTORY "${CADICAL_PATCHED_DIR}"
    RESULT_VARIABLE patch_result
  )
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply CaDiCaL patch: ${patch}")
  endif()
endforeach()

file(GLOB CADICAL_SOURCES
  ${CADICAL_PATCHED_DIR}/src/*.cpp
  ${CADICAL_PATCHED_DIR}/src/kitten.c
)
list(FILTER CADICAL_SOURCES EXCLUDE REGEX ".*(cadical|mobical|ipasir|ccadical)\\.cpp$")
add_library(cadical OBJECT ${CADICAL_SOURCES})
target_include_directories(cadical PUBLIC ${CADICAL_PATCHED_DIR}/src)
target_compile_definitions(cadical PRIVATE NBUILD)
target_compile_options(cadical PRIVATE -w)

# --- FlowCutter ---
add_subdirectory(extern/flow-cutter-pace17)
target_compile_options(flowcutter PRIVATE -w)

# --- Glucose ---
# The vendored Glucose under extern/glucose/ is kept pristine. Our changes live
# as patches in extern/glucose-patches/ and are applied at configure time to a
# copy in the build tree (glucose-patched/). We then build that patched copy.
# This keeps extern/glucose/ untouched. See extern/glucose-patches/README.md.
set(GLUCOSE_SRC_DIR     "${CMAKE_SOURCE_DIR}/extern/glucose")
set(GLUCOSE_PATCHED_DIR "${CMAKE_BINARY_DIR}/glucose-patched")
set(GLUCOSE_PATCH_DIR   "${CMAKE_SOURCE_DIR}/extern/glucose-patches")

# Fresh copy of the pristine sources (the patched tree is a build artifact).
file(REMOVE_RECURSE "${GLUCOSE_PATCHED_DIR}")
file(COPY "${GLUCOSE_SRC_DIR}/" DESTINATION "${GLUCOSE_PATCHED_DIR}")

# Apply every extern/glucose-patches/*.patch in sorted order via `git apply`.
file(GLOB GLUCOSE_PATCHES "${GLUCOSE_PATCH_DIR}/*.patch")
list(SORT GLUCOSE_PATCHES)
foreach(patch ${GLUCOSE_PATCHES})
  message(STATUS "Applying Glucose patch: ${patch}")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} apply -p1 --whitespace=nowarn "${patch}"
    WORKING_DIRECTORY "${GLUCOSE_PATCHED_DIR}"
    RESULT_VARIABLE patch_result
  )
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply Glucose patch: ${patch}")
  endif()
endforeach()

add_library(glucose OBJECT
  ${GLUCOSE_PATCHED_DIR}/core/Solver.cc
  ${GLUCOSE_PATCHED_DIR}/utils/Options.cc
  ${GLUCOSE_PATCHED_DIR}/utils/System.cc
)
target_include_directories(glucose PUBLIC ${GLUCOSE_PATCHED_DIR})
target_compile_definitions(glucose PUBLIC __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS)
target_compile_options(glucose PRIVATE -w)

# --- GPMC core library ---
# Vendored libraries (cadical/glucose/flowcutter) are OBJECT libraries above,
# so their object files are folded directly into gpmc_core's own archive
# instead of being separate .a files. This makes lib/libgpmc.a (see build.sh)
# a single self-contained archive built with a plain `ar`, with no reliance on
# GNU-binutils-only features like `ar -M` MRI scripts (unsupported by the
# macOS/BSD ar), so build.sh only needs to copy it into lib/.
add_library(gpmc_core STATIC
  counter/CNF.cc
  counter/Normalizer.cc
  counter/ComponentAnalyzer.cc
  counter/ComponentCache.cc
  counter/ComponentManager.cc
  counter/Counter.cc
  counter/CounterImpl.cc
  counter/TDScorer.cc
  counter/VarSelector.cc
  preprocessor/Preprocessor.cc
  td/IFlowCutter.cc
  td/PrimalGraph.cc
  td/TreeDecomposition.cc
  $<TARGET_OBJECTS:glucose>
  $<TARGET_OBJECTS:flowcutter>
  $<TARGET_OBJECTS:cadical>
)
target_include_directories(gpmc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(gpmc_core PRIVATE ${GPMC_COMPILE_OPTIONS})
target_link_libraries(gpmc_core PUBLIC glucose flowcutter cadical ${GMP_LIBRARIES})

# --- GPMC binary ---
add_executable(gpmc cli/Main.cc cli/Options.cc cli/Report.cc cli/SignalHandling.cc)
target_include_directories(gpmc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/CLI11
    ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(gpmc PRIVATE ${GPMC_COMPILE_OPTIONS} -Wno-deprecated-declarations)
target_link_libraries(gpmc PRIVATE gpmc_core
  $<$<BOOL:${GPMC_STATIC_BUILD}>:-static>)

# --- GPMC standalone preprocessor: reads a CNF, preprocesses it, writes the
#     simplified CNF (Arjun-compatible "c MUST MULTIPLY BY") back out.
#     Off by default; enable with -DGPMC_BUILD_PP=ON. ---
if(GPMC_BUILD_PP)
  add_executable(gpmc-pp cli/PreprocMain.cc cli/Options.cc)
  target_include_directories(gpmc-pp PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/extern/CLI11
      ${CMAKE_CURRENT_BINARY_DIR})
  target_compile_options(gpmc-pp PRIVATE ${GPMC_COMPILE_OPTIONS} -Wno-deprecated-declarations)
  target_link_libraries(gpmc-pp PRIVATE gpmc_core
    $<$<BOOL:${GPMC_STATIC_BUILD}>:-static>)
endif()
