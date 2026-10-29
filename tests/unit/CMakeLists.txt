notes_add_test(NAME tst_smoke SOURCES tst_smoke.cpp LABELS unit)
notes_add_test(NAME tst_appcontext SOURCES tst_appcontext.cpp LABELS unit)
notes_add_test(NAME tst_singleinstance SOURCES tst_singleinstance.cpp LABELS unit LIBS Qt6::Network)
