add_executable(CommonwealthMP.Server src/main.cpp)
target_include_directories(CommonwealthMP.Server PRIVATE ${CMAKE_SOURCE_DIR}/protocol)
target_link_libraries(CommonwealthMP.Server PRIVATE ws2_32)
target_compile_definitions(CommonwealthMP.Server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
	target_link_options(CommonwealthMP.Server PRIVATE /SUBSYSTEM:CONSOLE)
endif()
set_target_properties(CommonwealthMP.Server PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/server
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/dist/server
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/dist/server
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/dist/server
)
