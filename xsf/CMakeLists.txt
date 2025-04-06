cmake_minimum_required(VERSION 3.14)

project(
    xsf
    VERSION 0.1.0
    DESCRIPTION "xSF plugin for fooyin"
    HOMEPAGE_URL "https://github.com/kode54/fooyin-kode54-plugins"
    LANGUAGES C CXX
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(psflib)
add_subdirectory(highly_experimental)
add_subdirectory(highly_theoretical)
add_subdirectory(highly_quixotic)
add_subdirectory(lazyusf2)
add_subdirectory(vio2sf)
add_subdirectory(sseqplayer)

create_fooyin_plugin(
    xsf
    DEPENDS Fooyin::Gui
            psflib
            highly_experimental
            highly_theoretical
            highly_quixotic
            lazyusf2
            vio2sf
            sseqplayer
            mgba
    SOURCES xsfinputplugin.cpp
            xsfinputplugin.h
            xsfinput.cpp
            xsfinput.h
            xsfinputdefs.h
            xsfinputsettings.cpp
            xsfinputsettings.h
)
