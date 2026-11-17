find_package(Qt6 REQUIRED COMPONENTS Test)

# Drives AppController against a private tmux server, so it needs tmux.
add_executable(tst_sessionflow tst_sessionflow.cpp)
x3ro_deck_set_warnings(tst_sessionflow)
target_link_libraries(tst_sessionflow PRIVATE x3ro-deck-core Qt6::Test)
add_test(NAME sessionflow COMMAND tst_sessionflow)
set_tests_properties(sessionflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

add_executable(tst_socketpath tst_socketpath.cpp)
x3ro_deck_set_warnings(tst_socketpath)
target_link_libraries(tst_socketpath PRIVATE x3ro-deck-core Qt6::Test)
target_compile_definitions(tst_socketpath PRIVATE X3RO_DECK_HOOK_PATH="$<TARGET_FILE:x3ro-deck-hook>")
add_dependencies(tst_socketpath x3ro-deck-hook)
add_test(NAME socketpath COMMAND tst_socketpath)

add_executable(tst_paths tst_paths.cpp)
x3ro_deck_set_warnings(tst_paths)
target_link_libraries(tst_paths PRIVATE x3ro-deck-core Qt6::Test)
add_test(NAME paths COMMAND tst_paths)
