cmake_minimum_required(VERSION 3.25)

project(NotesApp
    VERSION 0.1.0
    DESCRIPTION "Secure notes app for Ubuntu"
    LANGUAGES CXX)

set(NOTES_APP_ID "io.augmex.NotesApp")
set(NOTES_ORG_NAME "augmex")
set(NOTES_ORG_DOMAIN "augmex.io")

option(NOTES_ENABLE_TESTS "Build unit, integration and UI tests" ON)
option(NOTES_ENABLE_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(NOTES_ENABLE_FUZZ "Build libFuzzer targets (requires clang)" OFF)
option(NOTES_ENABLE_BENCH "Build performance benchmarks" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerFlags)
include(Dependencies)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_subdirectory(src)

if(NOTES_ENABLE_TESTS OR NOTES_ENABLE_FUZZ OR NOTES_ENABLE_BENCH)
    enable_testing()
    add_subdirectory(tests)
endif()
