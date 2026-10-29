# Third-party dependencies. Minimum versions are set by docs/adr/0001-minimum-platform.md.

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network NetworkAuth Svg)
if(NOTES_ENABLE_TESTS OR NOTES_ENABLE_BENCH)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()

# Not linked yet; found here so a missing package fails at configure time,
# not months later in Phase 4.
find_package(Qt6Keychain REQUIRED)

find_package(PkgConfig REQUIRED)
# SQLCipher, never plain SQLite (task 2.1, ADR 0002). Its pkg-config file puts
# /usr/include/sqlcipher first on the include path, so <sqlite3.h> is SQLCipher's.
# No version constraint: sqlcipher.pc reports the bundled SQLite version, not
# SQLCipher's. tst_smoke checks `PRAGMA cipher_version` instead.
pkg_check_modules(SQLCipher REQUIRED IMPORTED_TARGET sqlcipher)
pkg_check_modules(Sodium REQUIRED IMPORTED_TARGET libsodium>=1.0.18)
