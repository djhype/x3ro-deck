cmake_minimum_required(VERSION 3.24)
project(x3ro-deck VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(X3RO_DECK_WERROR "Treat compiler warnings as errors" OFF)

# MSVC is also set for clang-cl, which takes MSVC-style flags.
function(x3ro_deck_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ${X3RO_DECK_WERROR})
endfunction()

option(X3RO_DECK_CORE_ONLY "Build without KF6: the controller, backends, hook and tests, but not the app" OFF)

# 6.4 is what Ubuntu 24.04 and Debian 12 ship.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)
# The built-in terminal's emulator.
find_package(PkgConfig REQUIRED)
pkg_check_modules(vterm REQUIRED IMPORTED_TARGET vterm>=0.3)

# The x3ro-deck-sessiond wire protocol and the attach client: QtCore only.
add_library(x3ro-deck-sessionproto STATIC
    src/SessiondProtocol.cpp
    attach/AttachClient.cpp
)
x3ro_deck_set_warnings(x3ro-deck-sessionproto)
target_include_directories(x3ro-deck-sessionproto PUBLIC src attach common)
target_link_libraries(x3ro-deck-sessionproto PUBLIC Qt6::Core)

# Owns agent PTYs so agents outlive the GUI (sessionBackend "daemon").
add_executable(x3ro-deck-sessiond sessiond/main.cpp sessiond/Daemon.cpp)
x3ro_deck_set_warnings(x3ro-deck-sessiond)
target_link_libraries(x3ro-deck-sessiond PRIVATE x3ro-deck-sessionproto Qt6::Network util)

# `x3ro-deck attach` without the GUI's libraries; the agent terminals run it.
add_executable(x3ro-deck-attach attach/main.cpp)
x3ro_deck_set_warnings(x3ro-deck-attach)
target_link_libraries(x3ro-deck-attach PRIVATE x3ro-deck-sessionproto)

# The app minus its KF6-dependent window and terminal, so tests and older
# distros can build it.
add_library(x3ro-deck-base STATIC
    src/Config.cpp
    src/ConfigMigrations.cpp
    src/DockerClient.cpp
    src/DockerEndpoint.cpp
    src/AgentBus.cpp
    common/x3ro-deck-socket.h
    src/SessionBackend.h
    src/TmuxManager.cpp
    src/DaemonBackend.cpp
    src/GitWatcher.cpp
    src/PrWatcher.cpp
    src/Models.cpp
    src/Paths.cpp
    src/AppController.cpp
    src/SettingsDialog.cpp
    src/TerminalChannel.h
    src/PtyChannel.cpp
    src/TerminalAppearance.cpp
    src/TerminalScreen.cpp
    src/TerminalView.cpp
)
x3ro_deck_set_warnings(x3ro-deck-base)
target_include_directories(x3ro-deck-base PUBLIC src common)
target_link_libraries(x3ro-deck-base PUBLIC x3ro-deck-sessionproto Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network PkgConfig::vterm util)

if(NOT X3RO_DECK_CORE_ONLY)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickWidgets Qml DBus)
    find_package(KF6Parts REQUIRED)
    find_package(KF6CoreAddons REQUIRED)
    find_package(KF6XmlGui REQUIRED)
    find_package(KF6Notifications REQUIRED)
    find_package(KF6StatusNotifierItem 6.5 REQUIRED) # quitRequested/abortQuit
    find_package(KF6GlobalAccel REQUIRED)
    find_package(KF6DBusAddons REQUIRED)
    find_package(KF6WindowSystem REQUIRED)

    add_executable(x3ro-deck
        src/main.cpp
        src/MainWindow.cpp
        src/TerminalHost.cpp
        resources.qrc
    )
    x3ro_deck_set_warnings(x3ro-deck)
    target_link_libraries(x3ro-deck PRIVATE
        x3ro-deck-base
        Qt6::Quick Qt6::QuickWidgets Qt6::Qml Qt6::DBus
        KF6::Parts KF6::CoreAddons KF6::XmlGui KF6::Notifications KF6::StatusNotifierItem
        KF6::GlobalAccel KF6::DBusAddons KF6::WindowSystem
    )
endif()

# The hook runs on every agent tool call, so it is plain C++ with no Qt
# dependency: it starts in about a millisecond and never blocks the agent.
add_executable(x3ro-deck-hook hook/x3ro-deck-hook.cpp)
x3ro_deck_set_warnings(x3ro-deck-hook)
target_include_directories(x3ro-deck-hook PRIVATE common)
target_compile_options(x3ro-deck-hook PRIVATE $<IF:$<BOOL:${MSVC}>,/O2,-O2>)

option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NOT X3RO_DECK_CORE_ONLY)
    install(TARGETS x3ro-deck RUNTIME DESTINATION bin)
endif()
install(TARGETS x3ro-deck-hook x3ro-deck-sessiond x3ro-deck-attach RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
    # Exec is absolute: Plasma's session PATH lacks ~/.local/bin, which usually
    # comes from a shell rc file that only terminals read.
    configure_file(data/x3ro-deck.desktop.in x3ro-deck.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/x3ro-deck.desktop DESTINATION share/applications)
    install(FILES data/x3ro-deck.notifyrc DESTINATION share/knotifications6)
    install(FILES data/icons/x3ro-deck.svg DESTINATION share/icons/hicolor/scalable/apps)
    foreach(size 16 22 24 32 48 64 128 256 512)
        install(FILES data/icons/x3ro-deck-${size}.png DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME x3ro-deck.png)
    endforeach()
endif()
