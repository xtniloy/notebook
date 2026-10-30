notes_add_test(NAME tst_mainwindow SOURCES tst_mainwindow.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_toolbar SOURCES tst_toolbar.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_sidebar SOURCES tst_sidebar.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_notelistview SOURCES tst_notelistview.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_editorview SOURCES tst_editorview.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_thememanager SOURCES tst_thememanager.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_windowchrome SOURCES tst_windowchrome.cpp LABELS ui LIBS notesapp_ui)

# TC-STO-025, TC-STO-026, TC-STO-027, TC-STO-028: autosave in the window (task 2.9).
notes_add_test(NAME tst_autosave SOURCES tst_autosave.cpp LABELS ui integration LIBS notesapp_ui)
# Task 2.10: Recently Deleted in the window.
notes_add_test(NAME tst_recentlydeleted SOURCES tst_recentlydeleted.cpp LABELS ui LIBS notesapp_ui)
# TC-UI-021 .. TC-UI-024: the window on real data (task 2.11).
notes_add_test(NAME tst_wiring SOURCES tst_wiring.cpp LABELS ui integration LIBS notesapp_ui)
