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

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Network Quick QuickWidgets Qml DBus)
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
    src/Config.cpp
    src/DockerClient.cpp
    src/AgentBus.cpp
    src/TmuxManager.cpp
    src/GitWatcher.cpp
    src/PrWatcher.cpp
    src/Models.cpp
    src/AppController.cpp
    src/TerminalHost.cpp
    src/MainWindow.cpp
    src/SettingsDialog.cpp
    resources.qrc
)
target_compile_options(x3ro-deck PRIVATE -Wall -Wextra)
target_link_libraries(x3ro-deck PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Quick Qt6::QuickWidgets Qt6::Qml Qt6::DBus
    KF6::Parts KF6::CoreAddons KF6::XmlGui KF6::Notifications KF6::StatusNotifierItem
    KF6::GlobalAccel KF6::DBusAddons KF6::WindowSystem
)

# The hook runs on every agent tool call, so it is plain C++ with no Qt
# dependency: it starts in about a millisecond and never blocks the agent.
add_executable(x3ro-deck-hook hook/x3ro-deck-hook.cpp)
target_compile_options(x3ro-deck-hook PRIVATE -Wall -Wextra -O2)

install(TARGETS x3ro-deck x3ro-deck-hook RUNTIME DESTINATION bin)
install(FILES data/x3ro-deck.desktop DESTINATION share/applications)
install(FILES data/x3ro-deck.notifyrc DESTINATION share/knotifications6)
install(FILES data/icons/x3ro-deck.svg DESTINATION share/icons/hicolor/scalable/apps)
foreach(size 16 22 24 32 48 64 128 256 512)
    install(FILES data/icons/x3ro-deck-${size}.png DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME x3ro-deck.png)
endforeach()
