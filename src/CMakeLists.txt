configure_file(app/Config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/app/Config.h" @ONLY)

# Storage, crypto, services, sync. No widgets, so tests can link it without a GUI.
add_library(notesapp_core STATIC
    app/AppContext.cpp
    app/AppContext.h
    app/AppIdentity.cpp
    app/AppIdentity.h
    app/SingleInstance.cpp
    app/SingleInstance.h
    crypto/Sodium.cpp
    crypto/Sodium.h
    models/DateGrouping.cpp
    models/DateGrouping.h
    models/FakeNoteListModel.cpp
    models/FakeNoteListModel.h
    models/FakeSidebarModel.cpp
    models/FakeSidebarModel.h
    models/NoteListRoles.h
    models/SidebarRoles.h
    storage/SqlCipher.cpp
    storage/SqlCipher.h)
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
    ui/Sidebar.cpp
    ui/Sidebar.h
    ui/SidebarDelegate.cpp
    ui/SidebarDelegate.h)
target_link_libraries(notesapp_ui PUBLIC notesapp_core Qt6::Widgets)

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

file(GLOB license_files CONFIGURE_DEPENDS "${resource_dir}/licenses/*.txt")
qt_add_resources(notesapp_ui "licenses"
    PREFIX "/licenses"
    BASE "${resource_dir}/licenses"
    FILES ${license_files})

add_executable(notesapp main.cpp)
target_link_libraries(notesapp PRIVATE notesapp_ui)
target_link_options(notesapp PRIVATE $<$<CONFIG:Release>:-s>)

install(TARGETS notesapp)
