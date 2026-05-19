cmake_minimum_required(VERSION 3.16)

project(qt-dashboard VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When cross-compiling via the Yocto SDK, OECORE_TARGET_SYSROOT is set
# and we need to add ROS 2's install prefix to the CMake search path,
# since meta-ros installs under /opt/ros/<distro>/ rather than /usr/.
# When building natively, this block is skipped and CMake uses
# whatever ROS environment was sourced (e.g., /opt/ros/jazzy on the laptop).
if(DEFINED ENV{OECORE_TARGET_SYSROOT})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{OECORE_TARGET_SYSROOT}/opt/ros/humble")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

qt_standard_project_setup()

qt_add_executable(appqtdashboard
    main.cpp
)

qt_add_qml_module(appqtdashboard
    URI qtdashboard
    VERSION 1.0
    QML_FILES
        Main.qml
        SOURCES
            dashboard_backend.h dashboard_backend.cpp
            vehicledatanode.h vehicledatanode.cpp
            rpicamerastreamnode.h rpicamerastreamnode.cpp
            rpicameraimageprovider.h rpicameraimageprovider.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appqtdashboard PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appqtdashboard
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appqtdashboard
    PRIVATE Qt6::Quick
    PRIVATE rclcpp::rclcpp
    PRIVATE ${std_msgs_TARGETS}
    PRIVATE ${sensor_msgs_TARGETS}
)

include(GNUInstallDirs)
install(TARGETS appqtdashboard
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
message(STATUS "**Qt version: ${Qt6_VERSION}")
