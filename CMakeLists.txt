cmake_minimum_required(VERSION 3.14)

project(
    kode54-plugins
    VERSION 0.1.0
    DESCRIPTION "Plugins for fooyin"
    HOMEPAGE_URL "https://github.com/kode54/fooyin-kode54-plugins"
    LANGUAGES C CXX
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Fooyin)

add_subdirectory(midiplugin)
add_subdirectory(vgmstream)
add_subdirectory(xsf)
