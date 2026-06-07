add_executable(sapiens_tests tests.cpp)
target_link_libraries(sapiens_tests PRIVATE sapienscore)
add_test(NAME sapiens_tests COMMAND sapiens_tests)

add_executable(sapiens_sqlite_tests test_sqlite.cpp)
target_link_libraries(sapiens_sqlite_tests PRIVATE sapiensstore)
add_test(NAME sapiens_sqlite_tests COMMAND sapiens_sqlite_tests)

add_executable(sapiens_sync_tests test_sync.cpp)
target_link_libraries(sapiens_sync_tests PRIVATE sapienscore)
add_test(NAME sapiens_sync_tests COMMAND sapiens_sync_tests)

add_executable(sapiens_core_extra_tests test_core_extra.cpp)
target_link_libraries(sapiens_core_extra_tests PRIVATE sapienscore)
add_test(NAME sapiens_core_extra_tests COMMAND sapiens_core_extra_tests)

add_executable(sapiens_app_tests test_app.cpp)
target_link_libraries(sapiens_app_tests PRIVATE sapienscore)
add_test(NAME sapiens_app_tests COMMAND sapiens_app_tests)

# Renders the simulator's Canvas + Renderer off-screen (no SDL needed).
add_executable(sapiens_render_tests test_render.cpp)
target_link_libraries(sapiens_render_tests PRIVATE sapienscore)
target_include_directories(sapiens_render_tests PRIVATE ${CMAKE_SOURCE_DIR}/simulator/src)
add_test(NAME sapiens_render_tests COMMAND sapiens_render_tests)
