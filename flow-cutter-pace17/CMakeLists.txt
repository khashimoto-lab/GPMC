SET(flowcutter_SOURCES 
	src/cell.cpp 
	src/greedy_order.cpp
	src/list_graph.cpp
	src/tree_decomposition.cpp)

add_compile_options("$<$<CONFIG:Debug>:-O0>")

if(GPMC_USE_STATIC_LIBS)
	add_library(flowcutter_static STATIC ${flowcutter_SOURCES})
	target_compile_definitions(flowcutter_static PRIVATE $<$<CONFIG:Debug>:DEBUG>)
  	if(GPMC_BUILD_PYTHON)
    		set_target_properties(flowcutter_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
  	endif()
else()
	add_library(flowcutter_shared SHARED ${flowcutter_SOURCES})
	target_compile_definitions(flowcutter_shared PRIVATE $<$<CONFIG:Debug>:DEBUG>)
endif()