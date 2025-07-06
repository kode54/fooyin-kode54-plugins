cmake_minimum_required(VERSION 3.14)

project(
    vgmstreaminput
    VERSION 0.1.0
    DESCRIPTION "VGMStream plugin for fooyin"
    HOMEPAGE_URL "https://github.com/kode54/fooyin-kode54-plugins"
    LANGUAGES CXX
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

create_fooyin_plugin(
    vgmstreaminput
    DEPENDS Fooyin::Gui
            vgmstream
    SOURCES vgmstreaminputplugin.cpp
            vgmstreaminputplugin.h
            vgmstreaminput.cpp
            vgmstreaminput.h
            vgmstreaminputdefs.h
            vgmstreaminputsettings.cpp
            vgmstreaminputsettings.h
)
