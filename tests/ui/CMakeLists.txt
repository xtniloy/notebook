notes_add_test(NAME tst_mainwindow SOURCES tst_mainwindow.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_toolbar SOURCES tst_toolbar.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_sidebar SOURCES tst_sidebar.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_notelistview SOURCES tst_notelistview.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_editorview SOURCES tst_editorview.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_thememanager SOURCES tst_thememanager.cpp LABELS ui LIBS notesapp_ui)
notes_add_test(NAME tst_windowchrome SOURCES tst_windowchrome.cpp LABELS ui LIBS notesapp_ui)

# TC-STO-025, TC-STO-026, TC-STO-027, TC-STO-028: autosave in the window (task 2.9).
notes_add_test(NAME tst_autosave SOURCES tst_autosave.cpp LABELS ui integration LIBS notesapp_ui)
