if(WIN32)
	set(CMP_SERVER_RC res/CommonwealthMP.Server.rc)
	set(CMP_SERVER_GUI src/win_console.cpp)
endif()
add_executable(CommonwealthMP.Server
	src/main.cpp
	src/session_persist.cpp
	src/net_util.cpp
	src/fake_peers.cpp
	src/handlers.cpp
	src/admin_cmds.cpp
	${CMP_SERVER_RC}
	${CMP_SERVER_GUI})
cmp_link_udp(CommonwealthMP.Server)
if(MSVC)
	target_link_options(CommonwealthMP.Server PRIVATE /SUBSYSTEM:CONSOLE)
endif()
set_target_properties(CommonwealthMP.Server PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/server
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/dist/server
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/dist/server
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_SOURCE_DIR}/dist/server
)

set(CMP_SERVER_VERSION "0.6.0")
if(CMP_GIT_VERSION AND NOT CMP_GIT_VERSION STREQUAL "unknown")
	string(REGEX REPLACE "^v" "" CMP_SERVER_VERSION_SUFFIX "${CMP_GIT_VERSION}")
else()
	set(CMP_SERVER_VERSION_SUFFIX "${CMP_SERVER_VERSION}")
endif()
add_custom_command(TARGET CommonwealthMP.Server POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:CommonwealthMP.Server> ${CMAKE_SOURCE_DIR}/dist/server/CommonwealthMP.Server-${CMP_SERVER_VERSION_SUFFIX}.exe
	COMMENT "Copying versioned server binary (${CMP_SERVER_VERSION_SUFFIX})"
)
