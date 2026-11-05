# Launches the real notesapp binary as a child process.
notes_add_test(NAME tst_singleinstance_process SOURCES tst_singleinstance_process.cpp
               LABELS integration LIBS Qt6::Network)
target_compile_definitions(tst_singleinstance_process PRIVATE
    NOTESAPP_BINARY="$<TARGET_FILE:notesapp>")
add_dependencies(tst_singleinstance_process notesapp)

# TC-STO-006: the DB job queue under load from several threads.
notes_add_test(NAME tst_dbthread SOURCES tst_dbthread.cpp LABELS integration)

# TC-STO-021: services on a real DbThread and database.
notes_add_test(NAME tst_services SOURCES tst_services.cpp LABELS integration)

# TC-EDT-021, TC-EDT-025: images stored inside the database (task 3.7).
notes_add_test(NAME tst_attachments SOURCES tst_attachments.cpp LABELS integration)
