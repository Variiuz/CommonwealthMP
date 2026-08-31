add_executable(CommonwealthMP.Probe main.cpp)
target_include_directories(CommonwealthMP.Probe PRIVATE ${CMAKE_SOURCE_DIR}/protocol)
target_link_libraries(CommonwealthMP.Probe PRIVATE ws2_32)
target_compile_definitions(CommonwealthMP.Probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
