cmake_minimum_required(VERSION 3.20)

project(TermDock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(QT_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_WIDGETS_TARGET Qt5::Widgets)
endif()

add_executable(TermDock WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/TerminalView.h
    src/TerminalView.cpp
    src/TerminalCell.h
    src/TerminalBuffer.h
    src/TerminalBuffer.cpp
    src/TerminalEmulator.h
    src/TerminalEmulator.cpp
    src/ConPtyProcess.h
    src/ConPtyProcess.cpp
    src/Session.h
    src/Session.cpp
    src/SessionManager.h
    src/SessionManager.cpp
    src/resources.rc
)

target_compile_definitions(TermDock PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

target_compile_options(TermDock PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(TermDock PRIVATE
    ${QT_WIDGETS_TARGET}
    kernel32
    user32
)
