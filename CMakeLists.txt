cmake_minimum_required(VERSION 3.14)
project(Keydrive LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Install udev rules for keyboard access
install(FILES 99-keydrive.rules
        DESTINATION /etc/udev/rules.d/
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

# Add libraries
add_library(keydrive_input
    input_handler.hpp
    input_handler.cpp
)
target_link_libraries(keydrive_input
    PRIVATE
        PkgConfig::LIBEVDEV
)
target_include_directories(keydrive_input
    PRIVATE
        ${LIBEVDEV_INCLUDE_DIRS}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_library(keydrive_output
    output_handler.cpp
    output_handler.hpp
)
target_link_libraries(keydrive_output
    PRIVATE
        PkgConfig::LIBEVDEV
        nlohmann_json::nlohmann_json
)
target_include_directories(keydrive_output
    PRIVATE
        ${LIBEVDEV_INCLUDE_DIRS}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

add_library(keydrive_layout
    layout_manager.hpp
    layout_manager.cpp
)
target_link_libraries(keydrive_layout
    PRIVATE
        yaml-cpp::yaml-cpp
)
target_include_directories(keydrive_layout
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Add executable
add_executable(keydrive main.cpp)
target_link_libraries(keydrive PRIVATE
    keydrive_input
    keydrive_output
    keydrive_layout
)
