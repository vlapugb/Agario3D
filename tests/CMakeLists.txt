add_executable(agar_tests match_tests.cpp)
target_link_libraries(agar_tests PRIVATE agar_match huynity::runtime)
add_test(NAME agar_match COMMAND agar_tests)

add_executable(agar_session_tests session_tests.cpp)
target_link_libraries(agar_session_tests PRIVATE agar_session)
add_test(NAME agar_session COMMAND agar_session_tests)

add_executable(agar_cell_render_tests cell_render_tests.cpp)
target_link_libraries(agar_cell_render_tests PRIVATE agar_cells huynity::engine)
add_test(NAME agar_cell_render COMMAND agar_cell_render_tests)
set_tests_properties(agar_cell_render PROPERTIES LABELS native SKIP_RETURN_CODE 77)
