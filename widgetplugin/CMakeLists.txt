cmake_minimum_required(VERSION 3.14)

project(
    widgetplugin
    VERSION 0.1.0
    DESCRIPTION "Widget plugin for fooyin"
    HOMEPAGE_URL "https://github.com/fooyin/fooyin"
    LANGUAGES CXX
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Fooyin)

create_fooyin_plugin(
    widgetplugin
    DEPENDS Fooyin::Gui
    SOURCES widgetplugin.cpp
            widgetplugin.h
            widget.cpp
            widget.h
)
