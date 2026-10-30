configure_file(app/Config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app/Config.h" @ONLY)

# Storage, crypto, services, sync. No widgets, so tests can link it without a GUI.
add_library(notesapp_core STATIC
    app/AppContext.cpp
    app/AppContext.h
    app/AppIdentity.cpp
    app/AppIdentity.h
    app/IClock.h
    app/SingleInstance.cpp
    app/SingleInstance.h
    app/SystemClock.cpp
    app/SystemClock.h
    crypto/Sodium.cpp
    crypto/Sodium.h
    models/Attachment.h
    models/DateGrouping.cpp
    models/DateGrouping.h
    models/FakeNoteListModel.cpp
    models/FakeNoteListModel.h
    models/FakeSidebarModel.cpp
    models/FakeSidebarModel.h
    models/Folder.h
    models/Note.h
    models/NoteSummary.h
    models/NoteListRoles.h
    models/SidebarRoles.h
    models/SyncState.h
    models/Tag.h
    models/Uuid.cpp
    models/Uuid.h
    storage/Database.cpp
    storage/Database.h
    storage/DbResult.h
    storage/DbThread.cpp
    storage/DbThread.h
    storage/FolderRepository.cpp
    storage/FolderRepository.h
    storage/Migrator.cpp
    storage/Migrator.h
    storage/NoteRepository.cpp
    storage/NoteRepository.h
    storage/SqlCipher.cpp
    storage/SqlCipher.h
    storage/SqliteError.cpp
    storage/SqliteError.h
    storage/Statement.cpp
    storage/Statement.h
    storage/Transaction.cpp
    storage/Transaction.h)
# Schema migrations (task 2.4), read by storage::Migrator from :/migrations.
# Like the UI resources below, qt_add_resources() on a static library needs no
# Q_INIT_RESOURCE() in the executables that link it.
file(GLOB migration_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/storage/migrations/*.sql")
qt_add_resources(notesapp_core "migrations"
    PREFIX "/migrations"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/storage/migrations"
    FILES ${migration_files})

target_include_directories(notesapp_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(notesapp_core
    PUBLIC notes_compile_options Qt6::Core
    PRIVATE Qt6::Network PkgConfig::SQLCipher PkgConfig::Sodium)

# Widgets, plus the QApplication bootstrap.
add_library(notesapp_ui STATIC
    app/Application.cpp
    app/Application.h
    ui/ActionRegistry.cpp
    ui/ActionRegistry.h
    ui/dialogs/AboutDialog.cpp
    ui/dialogs/AboutDialog.h
    ui/EditorView.cpp
    ui/EditorView.h
    ui/Fonts.cpp
    ui/Fonts.h
    ui/Icons.cpp
    ui/Icons.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/NoteListDelegate.cpp
    ui/NoteListDelegate.h
    ui/NoteListView.cpp
    ui/NoteListView.h
    ui/NoteTextEdit.cpp
    ui/NoteTextEdit.h
    ui/Sidebar.cpp
    ui/Sidebar.h
    ui/SidebarDelegate.cpp
    ui/SidebarDelegate.h
    ui/ThemeManager.cpp
    ui/ThemeManager.h)
target_link_libraries(notesapp_ui PUBLIC notesapp_core Qt6::Widgets PRIVATE Qt6::Svg)

# Bundled assets (resources/SOURCES.md). qt_add_resources() on a static library
# propagates the resource objects to every executable that links it, so no
# Q_INIT_RESOURCE() is needed in the app or the tests.
set(resource_dir "${PROJECT_SOURCE_DIR}/resources")

# rcc only compresses files that shrink by more than 70%, so fonts (~58% with
# zstd) would be stored raw: +2.6 MB instead of +1.1 MB (Plan §10: binary < 3 MB).
qt_add_resources(notesapp_ui "fonts"
    PREFIX "/fonts"
    BASE "${resource_dir}/fonts"
    OPTIONS --compress-algo zstd --compress 19 --threshold 30
    FILES
        "${resource_dir}/fonts/Inter-Regular.ttf"
        "${resource_dir}/fonts/Inter-Italic.ttf"
        "${resource_dir}/fonts/Inter-SemiBold.ttf"
        "${resource_dir}/fonts/Inter-Bold.ttf"
        "${resource_dir}/fonts/Inter-BoldItalic.ttf"
        "${resource_dir}/fonts/JetBrainsMono-Regular.ttf"
        "${resource_dir}/fonts/JetBrainsMono-Bold.ttf")

# tst_icons checks that this set matches the Icon enum exactly.
file(GLOB icon_files CONFIGURE_DEPENDS "${resource_dir}/icons/*.svg")
qt_add_resources(notesapp_ui "icons"
    PREFIX "/icons"
    BASE "${resource_dir}/icons"
    FILES ${icon_files})

qt_add_resources(notesapp_ui "themes"
    PREFIX "/themes"
    BASE "${resource_dir}/themes"
    FILES "${resource_dir}/themes/theme.qss")

file(GLOB license_files CONFIGURE_DEPENDS "${resource_dir}/licenses/*.txt")
qt_add_resources(notesapp_ui "licenses"
    PREFIX "/licenses"
    BASE "${resource_dir}/licenses"
    FILES ${license_files})

add_executable(notesapp main.cpp)
target_link_libraries(notesapp PRIVATE notesapp_ui)
target_link_options(notesapp PRIVATE $<$<CONFIG:Release>:-s>)

install(TARGETS notesapp)
