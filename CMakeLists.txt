# SPDX-License-Identifier: CC0-1.0
cmake_minimum_required(VERSION 3.20)
project(ng-recent VERSION 0.1 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6)
set(KF_MIN_VERSION 6.0)
find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS KIO)

add_library(ngrecent MODULE src/ngrecent.cpp src/events.cpp)
target_link_libraries(ngrecent PRIVATE Qt6::Core Qt6::DBus KF6::KIOCore)
set_target_properties(ngrecent PROPERTIES OUTPUT_NAME "ngrecent")
install(TARGETS ngrecent DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/kio)

# ng-recent-list: the same list on the terminal, to see what the worker would show
add_executable(ng-recent-list src/list.cpp src/events.cpp)
target_link_libraries(ng-recent-list PRIVATE Qt6::Core Qt6::DBus)
install(TARGETS ng-recent-list DESTINATION ${KDE_INSTALL_BINDIR})

# ng-recentd: the same list for any program, and where programs tell what they did (session bus, org.ng.Recent1;
# started by the bus when called, gone after a minute idle)
set(CMAKE_AUTOMOC ON)
add_executable(ng-recentd src/daemon.cpp src/events.cpp)
target_link_libraries(ng-recentd PRIVATE Qt6::Core Qt6::DBus)
install(TARGETS ng-recentd DESTINATION ${KDE_INSTALL_BINDIR})
configure_file(data/org.ng.Recent1.service.in org.ng.Recent1.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.ng.Recent1.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})

# ng-recent-status: an icon in the system tray that says whether all of this works (started with the session)
find_package(KF6StatusNotifierItem ${KF_MIN_VERSION} QUIET)
find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS Widgets)
if(KF6StatusNotifierItem_FOUND AND Qt6Widgets_FOUND)
    add_executable(ng-recent-status src/status.cpp src/events.cpp)
    target_link_libraries(ng-recent-status PRIVATE Qt6::Widgets Qt6::DBus KF6::StatusNotifierItem)
    install(TARGETS ng-recent-status DESTINATION ${KDE_INSTALL_BINDIR})
    install(FILES data/ng-recent-status.desktop DESTINATION ${KDE_INSTALL_AUTOSTARTDIR})
else()
    message(STATUS "KF6StatusNotifierItem or Qt6Widgets not found: ng-recent-status is not built")
endif()
