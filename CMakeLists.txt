cmake_minimum_required(VERSION 3.20)
project(CommonwealthMP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
	add_compile_options(/W4 /permissive- /Zc:__cplusplus)
endif()

find_package(Threads REQUIRED)

# Build-time version suffix: git describe if available, otherwise "unknown".
find_package(Git QUIET)
if(Git_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE CMP_GIT_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
endif()
if(NOT CMP_GIT_VERSION)
	set(CMP_GIT_VERSION "unknown")
endif()
add_compile_definitions(CMP_GIT_VERSION="${CMP_GIT_VERSION}")

function(cmp_link_udp tgt)
	target_include_directories(${tgt} PRIVATE ${CMAKE_SOURCE_DIR}/protocol)
	target_link_libraries(${tgt} PRIVATE Threads::Threads)
	if(WIN32)
		target_link_libraries(${tgt} PRIVATE ws2_32)
		target_compile_definitions(${tgt} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
	endif()
endfunction()

add_subdirectory(server)
add_subdirectory(tools/probe)

enable_testing()
add_subdirectory(tests)
