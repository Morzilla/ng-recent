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
