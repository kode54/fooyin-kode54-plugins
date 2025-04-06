cmake_minimum_required(VERSION 3.14)

project(
    sflist
    VERSION 0.1.0
    DESCRIPTION "sflist parser and generator library"
    HOMEPAGE_URL "https://github.com/kode54/fooyin-kode54-plugins"
    LANGUAGES C
)
set(target sflist)
add_library(${target} STATIC ${sources})

target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_sources(${target}
    PRIVATE
        sflist.c
        sflist.h
        sflist_rewrite.c
        json.c
        json.h
        json-builder.c
        json-builder.h
)
