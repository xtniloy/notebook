# Compiler warnings, sanitizers and fuzzing instrumentation.
# Every first-party target links `notes_compile_options` to pick these up.

add_library(notes_compile_options INTERFACE)

target_compile_options(notes_compile_options INTERFACE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
    $<$<CONFIG:Debug>:-Werror>)

if(NOTES_ENABLE_ASAN)
    target_compile_options(notes_compile_options INTERFACE
        -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(notes_compile_options INTERFACE -fsanitize=address,undefined)
endif()

if(NOTES_ENABLE_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "NOTES_ENABLE_FUZZ requires clang (use the 'fuzz' preset)")
    endif()
    # Instrument everything for coverage feedback; only fuzz targets link the libFuzzer main.
    target_compile_options(notes_compile_options INTERFACE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    target_link_options(notes_compile_options INTERFACE -fsanitize=address,undefined)
endif()
