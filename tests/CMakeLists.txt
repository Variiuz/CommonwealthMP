add_executable(CommonwealthMP.Units units/main.cpp)
target_include_directories(CommonwealthMP.Units PRIVATE
	${CMAKE_SOURCE_DIR}/protocol
	${CMAKE_SOURCE_DIR}/server/src
)
target_compile_definitions(CommonwealthMP.Units PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(CommonwealthMP.ServerCases server/main.cpp)
target_include_directories(CommonwealthMP.ServerCases PRIVATE ${CMAKE_SOURCE_DIR}/protocol)
target_link_libraries(CommonwealthMP.ServerCases PRIVATE ws2_32)
target_compile_definitions(CommonwealthMP.ServerCases PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

add_test(NAME cmp_units COMMAND CommonwealthMP.Units)

add_test(
	NAME cmp_hello_welcome
	COMMAND powershell -NoProfile -ExecutionPolicy Bypass
		-File ${CMAKE_SOURCE_DIR}/tools/probe/run_hello_test.ps1
		-Server $<TARGET_FILE:CommonwealthMP.Server>
		-Probe $<TARGET_FILE:CommonwealthMP.Probe>
		-Port 17777
)

function(cmp_server_case name port)
	cmake_parse_arguments(ARG "FAKE" "EXTRA" "" ${ARGN})
	set(_args
		-File ${CMAKE_SOURCE_DIR}/tests/run_server_case.ps1
		-Server $<TARGET_FILE:CommonwealthMP.Server>
		-Cases $<TARGET_FILE:CommonwealthMP.ServerCases>
		-Case ${name}
		-Port ${port}
	)
	if(NOT ARG_FAKE)
		list(APPEND _args -NoFake)
	endif()
	if(ARG_EXTRA)
		list(APPEND _args -ExtraArgs "${ARG_EXTRA}")
	endif()
	add_test(NAME cmp_${name} COMMAND powershell -NoProfile -ExecutionPolicy Bypass ${_args})
endfunction()

cmp_server_case(reject_not_in_world 17778)
cmp_server_case(reject_protocol 17779)
cmp_server_case(reject_full 17792 EXTRA "--max-players 1")
cmp_server_case(new_player_spawn 17780)
cmp_server_case(returning_pose 17781)
cmp_server_case(guest_meets_host 17787)
cmp_server_case(two_clients_relay 17782)
cmp_server_case(persist_files 17783)
cmp_server_case(host_handoff 17784)
cmp_server_case(fake_off_at_two 17785 FAKE)
cmp_server_case(junk_and_unknown 17786)
cmp_server_case(query_empty 17788)
cmp_server_case(query_with_host 17789)
cmp_server_case(require_host_no_host 17790)
cmp_server_case(require_host_at_host 17791)
cmp_server_case(session_branding 17793 EXTRA "--name BrandSrv --motd BrandMotd --max-players 12")
cmp_server_case(host_not_streaming 17794)
cmp_server_case(query_interior 17795)
cmp_server_case(returning_mismatch_world 17796)
cmp_server_case(bye_relay 17797)
cmp_server_case(hello_again 17798)
cmp_server_case(interest_cull 17799 EXTRA "--interest-uu 50")
cmp_server_case(persist_appearance 17800)
cmp_server_case(snapshot_host_clock 17801)
cmp_server_case(same_key_two_sockets 17802)
