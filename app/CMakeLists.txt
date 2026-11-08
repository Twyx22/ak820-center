cmake_minimum_required(VERSION 3.16)
project(ak820-center VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(hidapi REQUIRED)

add_executable(ak820-center
    src/main.cpp
    src/device.cpp
    src/layout.cpp
    src/keycodes.cpp
    src/appstate.cpp
    src/mainwindow.cpp
    src/keyboardview.cpp
    src/lightingwidget.cpp
    src/keymapwidget.cpp
    src/rgbwidget.cpp
    src/macrowidget.cpp
    src/screenwidget.cpp
    src/settingswidget.cpp
)

target_link_libraries(ak820-center PRIVATE Qt6::Widgets hidapi::hidraw)
target_include_directories(ak820-center PRIVATE src)
