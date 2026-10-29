# libFuzzer targets. Built only with the `fuzz` preset (clang).
#
#   notes_add_fuzzer(NAME fuzz_x SOURCES fuzz_x.cpp)
#
# Each source defines `extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t)`.
# First target: TC-SRC-008 (task 5.2).
function(notes_add_fuzzer)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_SOURCES)
        message(FATAL_ERROR "notes_add_fuzzer: NAME and SOURCES are required")
    endif()
    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE notes_test_common)
    target_link_options(${ARG_NAME} PRIVATE -fsanitize=fuzzer)
endfunction()
