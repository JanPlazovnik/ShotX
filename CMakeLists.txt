cmake_minimum_required(VERSION 3.20)
project(shotx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)
find_package(KF6GlobalAccel REQUIRED)
find_package(LayerShellQt REQUIRED)

add_executable(shotx
    src/main.cpp
    src/app.cpp
    src/capture.cpp
    src/overlay.cpp
    src/editor.cpp
)
target_link_libraries(shotx PRIVATE Qt6::Widgets Qt6::DBus KF6::GlobalAccel LayerShellQt::Interface)
