cmake_minimum_required(VERSION 3.20)
project(CommonwealthMP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
	add_compile_options(/W4 /permissive- /Zc:__cplusplus)
endif()

add_subdirectory(server)
add_subdirectory(tools/probe)

enable_testing()
add_subdirectory(tests)
