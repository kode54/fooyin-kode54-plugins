cmake_minimum_required(VERSION 3.14)

project(
    midiplugin
    VERSION 0.1.0
    DESCRIPTION "MIDI plugin for fooyin"
    HOMEPAGE_URL "https://github.com/kode54/fooyin-kode54-plugins"
    LANGUAGES CXX
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Fooyin)

create_fooyin_plugin(
    midiplugin
    DEPENDS Fooyin::Gui
            bass
            bassmidi
    SOURCES midiinputplugin.cpp
            midiinputplugin.h
            midiinput.cpp
            midiinput.h
            midiinputsettings.cpp
            midiinputsettings.h
            BMPlayer.cpp
            BMPlayer.h
            MIDIPlayer.cpp
            MIDIPlayer.h
            sflist.c
            sflist.h
            sflist_rewrite.c
            midi_processing/midi_container.cpp
            midi_processing/midi_container.h
            midi_processing/midi_processor_gmf.cpp
            midi_processing/midi_processor_helpers.cpp
            midi_processing/midi_processor_hmi.cpp
            midi_processing/midi_processor_hmp.cpp
            midi_processing/midi_processor_lds.cpp
            midi_processing/midi_processor_mids.cpp
            midi_processing/midi_processor_mus.cpp
            midi_processing/midi_processor_riff_midi.cpp
            midi_processing/midi_processor_standard_midi.cpp
            midi_processing/midi_processor_syx.cpp
            midi_processing/midi_processor_xmi.cpp
            midi_processing/midi_processor.h
)
