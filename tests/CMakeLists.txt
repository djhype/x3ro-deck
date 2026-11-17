find_package(Qt6 REQUIRED COMPONENTS Test)

# Drives AppController against a private tmux server, so it needs tmux.
add_executable(tst_sessionflow tst_sessionflow.cpp)
x3ro_deck_set_warnings(tst_sessionflow)
target_link_libraries(tst_sessionflow PRIVATE x3ro-deck-core Qt6::Test)
add_test(NAME sessionflow COMMAND tst_sessionflow)
set_tests_properties(sessionflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
