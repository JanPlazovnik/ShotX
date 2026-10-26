cmake_minimum_required(VERSION 3.20)
project(shotx VERSION 0.1.0 DESCRIPTION "Fast region screenshots with an annotation editor" LANGUAGES CXX)

include(GNUInstallDirs)

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
    src/settings.cpp
)
target_link_libraries(shotx PRIVATE Qt6::Widgets Qt6::DBus KF6::GlobalAccel LayerShellQt::Interface)

install(TARGETS shotx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# KWin only allows ScreenShot2 for the binary named in an installed desktop file.
set(BIN ${CMAKE_INSTALL_FULL_BINDIR}/shotx)
configure_file(data/shotx.desktop shotx.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/shotx.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/shotx.desktop DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Jan Plazovnik <dev@plazovnik.si>")
set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /etc/xdg /etc/xdg/autostart /usr/share/applications)
include(CPack)
