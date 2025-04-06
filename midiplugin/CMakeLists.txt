cmake_minimum_required(VERSION 3.14)

project(
    midiplugin
    VERSION 0.1.0
    DESCRIPTION "MIDI plugin for fooyin"
    HOMEPAGE_URL "https://github.com/kode54/fooyin-kode54-plugins"
    LANGUAGES C CXX
)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(sflist)
add_subdirectory(midi_processing)

create_fooyin_plugin(
    midiplugin
    DEPENDS Fooyin::Gui
            bass
            bassmidi
            sflist
            midi_processing
    SOURCES midiinputplugin.cpp
            midiinputplugin.h
            midiinput.cpp
            midiinput.h
            midiinputdefs.h
            midiinputsettings.cpp
            midiinputsettings.h
            BMPlayer.cpp
            BMPlayer.h
            MIDIPlayer.cpp
            MIDIPlayer.h
)
