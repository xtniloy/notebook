notes_add_test(NAME tst_smoke SOURCES tst_smoke.cpp LABELS unit)
notes_add_test(NAME tst_appcontext SOURCES tst_appcontext.cpp LABELS unit)
notes_add_test(NAME tst_singleinstance SOURCES tst_singleinstance.cpp LABELS unit LIBS Qt6::Network)
notes_add_test(NAME tst_fonts SOURCES tst_fonts.cpp LABELS unit LIBS notesapp_ui)
notes_add_test(NAME tst_icons SOURCES tst_icons.cpp LABELS unit LIBS notesapp_ui)
notes_add_test(NAME tst_actionregistry SOURCES tst_actionregistry.cpp LABELS unit LIBS notesapp_ui)
# Qt6::Gui: QAbstractItemModelTester checks decorations as QPixmap/QIcon, which
# needs a QGuiApplication (QTEST_MAIN picks one when QtGui is linked).
notes_add_test(NAME tst_fakesidebarmodel SOURCES tst_fakesidebarmodel.cpp LABELS unit LIBS Qt6::Gui)
notes_add_test(NAME tst_dategrouping SOURCES tst_dategrouping.cpp LABELS unit)
notes_add_test(NAME tst_fakenotelistmodel SOURCES tst_fakenotelistmodel.cpp LABELS unit LIBS Qt6::Gui)
notes_add_test(NAME tst_clock SOURCES tst_clock.cpp LABELS unit)
notes_add_test(NAME tst_models SOURCES tst_models.cpp LABELS unit)
notes_add_test(NAME tst_database SOURCES tst_database.cpp LABELS unit)
notes_add_test(NAME tst_migrator SOURCES tst_migrator.cpp LABELS unit)
notes_add_test(NAME tst_folderrepository SOURCES tst_folderrepository.cpp LABELS unit)
notes_add_test(NAME tst_noterepository SOURCES tst_noterepository.cpp LABELS unit)
notes_add_test(NAME tst_notetext SOURCES tst_notetext.cpp LABELS unit)

# TC-STO-022, TC-STO-023, TC-STO-024: the database-backed models (task 2.8).
# Qt6::Gui for QAbstractItemModelTester, as above.
notes_add_test(NAME tst_notelistmodel SOURCES tst_notelistmodel.cpp LABELS unit LIBS Qt6::Gui)
notes_add_test(NAME tst_foldertreemodel SOURCES tst_foldertreemodel.cpp LABELS unit LIBS Qt6::Gui)

# Task 2.9: autosave on its own (the window's TC-STO-025..028 are in tests/ui).
notes_add_test(NAME tst_autosave_unit SOURCES tst_autosave_unit.cpp LABELS unit)

# TC-EDT-001: the note body format's round-trip harness (task 3.1).
notes_add_test(NAME tst_noteformat SOURCES tst_noteformat.cpp LABELS unit LIBS notesapp_ui)

# TC-EDT-002, TC-EDT-004, TC-EDT-005 on the document: paragraph styles (task 3.2).
notes_add_test(NAME tst_stylemanager SOURCES tst_stylemanager.cpp LABELS unit LIBS notesapp_ui)

# TC-EDT-022, TC-EDT-023, TC-EDT-024: images coming into a note (task 3.7).
notes_add_test(NAME tst_imageimport SOURCES tst_imageimport.cpp LABELS unit LIBS notesapp_ui)

# TC-EDT-028, TC-EDT-053: cleaning pasted HTML and Markdown (task 3.8).
notes_add_test(NAME tst_pastecleaner SOURCES tst_pastecleaner.cpp LABELS unit LIBS notesapp_ui)
# TC-EDT-031: which links open, and finding a typed address (task 3.9).
notes_add_test(NAME tst_links SOURCES tst_links.cpp LABELS unit LIBS notesapp_ui)
