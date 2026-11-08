cmake_minimum_required(VERSION 3.13)

project(illviewer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geode REQUIRED)

add_library(${PROJECT_NAME} SHARED
    main.cpp
)

setup_geode_mod(${PROJECT_NAME}
    RESOURCES
        resources/ill_icon.png
)
