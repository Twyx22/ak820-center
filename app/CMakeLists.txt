cmake_minimum_required(VERSION 3.16)
project(ak820-center VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia LinguistTools)
find_package(hidapi REQUIRED)

# Qt 6.12: the translated-language list is declared at project setup;
# qt_add_translations() then compiles the .ts files and embeds the .qm
# files under ":/i18n".
qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES fr en)

add_executable(ak820-center
    src/main.cpp
    src/device.cpp
    src/hidworker.cpp
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
    src/musicwidget.cpp
    resources.qrc
)

qt_add_translations(ak820-center TS_FILES i18n/ak820-center_fr.ts)

target_link_libraries(ak820-center PRIVATE Qt6::Widgets Qt6::Multimedia hidapi::hidraw)
target_include_directories(ak820-center PRIVATE src)
