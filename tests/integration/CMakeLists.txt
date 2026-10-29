# Launches the real notesapp binary as a child process.
notes_add_test(NAME tst_singleinstance_process SOURCES tst_singleinstance_process.cpp
               LABELS integration LIBS Qt6::Network)
target_compile_definitions(tst_singleinstance_process PRIVATE
    NOTESAPP_BINARY="$<TARGET_FILE:notesapp>")
add_dependencies(tst_singleinstance_process notesapp)

# TC-STO-006: the DB job queue under load from several threads.
notes_add_test(NAME tst_dbthread SOURCES tst_dbthread.cpp LABELS integration)
