SET(preprocessor_SOURCES 
	graph.cpp 
	instance.cpp
	oracle.cpp
	preprocessor.cpp
	subsumer.cpp
	treewidth.cpp
	twpp.cpp
)

ADD_LIBRARY(preprocessor ${preprocessor_SOURCES})

add_executable (preproc main.cpp)
target_link_libraries(preproc preprocessor $<$<CONFIG:Release>:-static>)