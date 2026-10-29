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
    ui/MainWindow.cpp
    ui/MainWindow.h)
target_link_libraries(notesapp_ui PUBLIC notesapp_core Qt6::Widgets)

add_executable(notesapp main.cpp)
target_link_libraries(notesapp PRIVATE notesapp_ui)
target_link_options(notesapp PRIVATE $<$<CONFIG:Release>:-s>)

install(TARGETS notesapp)
