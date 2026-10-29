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
