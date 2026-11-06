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
# TC-UI-025, TC-UI-026: drag & drop (task 2.12).
notes_add_test(NAME tst_dragdrop SOURCES tst_dragdrop.cpp LABELS ui LIBS notesapp_ui)
# UI review: menus drawn by AppStyle (rounded, shadow, right-aligned shortcuts).
notes_add_test(NAME tst_appstyle SOURCES tst_appstyle.cpp LABELS ui LIBS notesapp_ui)
# TC-EDT-002, TC-EDT-003, TC-EDT-005: paragraph styles in the window (task 3.2).
notes_add_test(NAME tst_paragraphstyles SOURCES tst_paragraphstyles.cpp LABELS ui LIBS notesapp_ui)
# TC-EDT-008, TC-EDT-009: inline formats in the window (task 3.4).
notes_add_test(NAME tst_inlineformats SOURCES tst_inlineformats.cpp LABELS ui LIBS notesapp_ui)
# TC-EDT-011 .. TC-EDT-013: lists in the window (task 3.5).
notes_add_test(NAME tst_lists SOURCES tst_lists.cpp LABELS ui LIBS notesapp_ui)
# TC-EDT-015 .. TC-EDT-018: checklists in the window (task 3.6).
notes_add_test(NAME tst_checklists SOURCES tst_checklists.cpp LABELS ui LIBS notesapp_ui)
# TC-EDT-020, TC-EDT-026: images in the window (task 3.7).
notes_add_test(NAME tst_images SOURCES tst_images.cpp LABELS ui integration LIBS notesapp_ui)
# TC-EDT-027, TC-EDT-029, TC-EDT-052, TC-EDT-054: safe paste in the window (task 3.8).
notes_add_test(NAME tst_paste SOURCES tst_paste.cpp LABELS ui LIBS notesapp_ui Qt6::Network)
# TC-EDT-030, TC-EDT-031: links typed, drawn and opened in the editor (task 3.9).
notes_add_test(NAME tst_linkediting SOURCES tst_linkediting.cpp LABELS ui LIBS notesapp_ui)
# Code drawn on a gray background in the editor (follow-up to task 3.4).
notes_add_test(NAME tst_codelook SOURCES tst_codelook.cpp LABELS ui LIBS notesapp_ui)
# Undo and redo for every editor operation, per note (task 3.10).
notes_add_test(NAME tst_undoredo SOURCES tst_undoredo.cpp LABELS ui LIBS notesapp_ui)
