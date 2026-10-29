set(NOTES_TEST_LABELS unit integration ui bench slow)

# Header-only helpers and fakes shared by all tests (tests/common/).
add_library(notes_test_common INTERFACE)
target_include_directories(notes_test_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/common")
target_link_libraries(notes_test_common INTERFACE notesapp_core)

# Adds one Qt Test executable and registers it with CTest.
#
#   notes_add_test(NAME tst_x SOURCES tst_x.cpp LABELS unit [LIBS extra_target ...])
#
# Links notesapp_core, Qt6::Test and tests/common automatically. UI tests add
# `LIBS notesapp_ui`. Every test runs headless with QT_QPA_PLATFORM=offscreen, on
# a 1920x1080 screen (Qt's default offscreen screen is 800x600, smaller than
# the app's default window, so window geometry would be clamped).
function(notes_add_test)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES;LABELS;LIBS" ${ARGN})
    if(NOT ARG_NAME OR NOT ARG_SOURCES OR NOT ARG_LABELS)
        message(FATAL_ERROR "notes_add_test: NAME, SOURCES and LABELS are required")
    endif()
    foreach(label IN LISTS ARG_LABELS)
        if(NOT label IN_LIST NOTES_TEST_LABELS)
            message(FATAL_ERROR "notes_add_test(${ARG_NAME}): unknown label '${label}'. "
                                "Use one of: ${NOTES_TEST_LABELS}")
        endif()
    endforeach()

    add_executable(${ARG_NAME} ${ARG_SOURCES})
    target_link_libraries(${ARG_NAME} PRIVATE notes_test_common Qt6::Test ${ARG_LIBS})
    add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
    set_tests_properties(${ARG_NAME} PROPERTIES
        LABELS "${ARG_LABELS}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/common/offscreen-screen.json"
        TIMEOUT 120)
endfunction()

if(NOTES_ENABLE_TESTS)
    add_subdirectory(unit)
    add_subdirectory(integration)
    add_subdirectory(ui)
endif()
if(NOTES_ENABLE_BENCH)
    add_subdirectory(bench)
endif()
if(NOTES_ENABLE_FUZZ)
    add_subdirectory(fuzz)
endif()
