cmake_minimum_required(VERSION 3.20)
project(ShackLog
    VERSION 0.4.0
    DESCRIPTION "Standalone ham radio logbook with TCI integration"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Qt 6.2+ for QSqlDatabase, QWebSocket, modern QString APIs.
# Network is required by shacklog-server (QTcpServer); a Qt::Sql transitive
# dep on most platforms but listed here for explicitness.
find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Sql
    Network
    WebSockets
)

# Source layout — keep flat under src/ so includes are simple.
set(SHACKLOG_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/TciClient.cpp
    src/TciClient.h
    src/LogbookModel.cpp
    src/LogbookModel.h
    src/AdifReader.cpp
    src/AdifReader.h
    src/AwardsDialog.cpp
    src/AwardsDialog.h
    src/SectionMapDialog.cpp
    src/SectionMapDialog.h
    src/LotwDialog.cpp
    src/LotwDialog.h
    src/server/WsjtxAdifReceiver.cpp
    src/server/WsjtxAdifReceiver.h
    src/Qso.h
    src/EditDialog.cpp
    src/EditDialog.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    # ── Spot autofill (Phase 2: DX cluster, Phase 3: POTA) ──
    src/SpotData.h
    src/SpotIndex.cpp
    src/SpotIndex.h
    src/AetherSettingsReader.cpp
    src/AetherSettingsReader.h
    src/DxClusterClient.cpp
    src/DxClusterClient.h
    src/PotaClient.cpp
    src/PotaClient.h
    # ── Callsign lookup (worked-before / cty.dat / QRZ-HamQTH-callook) ──
    src/CtyDat.cpp
    src/CtyDat.h
    src/CallsignLookup.cpp
    src/CallsignLookup.h
    # ── APRS over AetherSDR's KISS-over-TCP TNC (activity + messaging) ──
    src/AprsDecode.cpp
    src/AprsDecode.h
    src/AprsKissClient.cpp
    src/AprsKissClient.h
    src/AprsStationModel.cpp
    src/AprsStationModel.h
    src/AprsActivityDialog.cpp
    src/AprsActivityDialog.h
    data/cty.qrc
)

# Windows: link as a GUI app so no console window appears at launch.
# Adding shacklog.rc compiles in the app icon (logbook.ico) so Explorer
# / taskbar / Alt-Tab show it for ShackLog.exe.
if(WIN32)
    add_executable(ShackLog WIN32 ${SHACKLOG_SOURCES} src/shacklog.rc)
elseif(APPLE)
    add_executable(ShackLog MACOSX_BUNDLE ${SHACKLOG_SOURCES})
else()
    add_executable(ShackLog ${SHACKLOG_SOURCES})
endif()

target_include_directories(ShackLog PRIVATE src)

target_link_libraries(ShackLog PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::WebSockets
)

# Keep MSVC quiet about Qt's deprecated-but-not-fatal API surface, and
# enable common warnings.
if(MSVC)
    target_compile_options(ShackLog PRIVATE /W3 /permissive-)
    target_compile_definitions(ShackLog PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(ShackLog PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Windows: run windeployqt after every build so Qt's DLLs and the
# platforms\qwindows.dll plugin get copied next to ShackLog.exe.  This
# lets the binary launch by double-click without Qt on PATH.
#
# We locate windeployqt via the bin/ directory next to the qmake we used,
# which gives a portable path regardless of how Qt was found.
if(WIN32)
    get_target_property(_qt6_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt6_bin "${_qt6_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt6_bin}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET ShackLog POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    $<IF:$<CONFIG:Debug>,--debug,--release>
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    --compiler-runtime
                    "$<TARGET_FILE:ShackLog>"
            COMMENT "Running windeployqt to bundle Qt runtime DLLs"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found in ${_qt6_bin} — "
                        "ShackLog.exe will need Qt DLLs on PATH to launch")
    endif()
endif()

# On macOS the app needs a sensible bundle identifier.
if(APPLE)
    set_target_properties(ShackLog PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME           "ShackLog"
        MACOSX_BUNDLE_GUI_IDENTIFIER        "com.g0jkn.shacklog"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
    )
endif()

# ────────────────────────────────────────────────────────────────────
#  shacklog-server — headless web + N3FJP-compat server (Phase 0 spike)
# ────────────────────────────────────────────────────────────────────
# Target host is shack-hub (Linux); also built on Windows + macOS for
# dev convenience.  No GUI/Widgets — QCoreApplication only.  Reuse of
# the desktop's LogbookModel etc. lands in Phase 1; for now the spike
# is self-contained under src/server/.
set(SHACKLOG_SERVER_SOURCES
    src/server/main.cpp
    src/server/HttpServer.cpp
    src/server/HttpServer.h
    src/server/N3fjpServer.cpp
    src/server/N3fjpServer.h
    src/server/N3fjpClient.cpp
    src/server/N3fjpClient.h
    src/server/WsjtxAdifReceiver.cpp
    src/server/WsjtxAdifReceiver.h
    # ── Domain layer reused from the desktop binary (see design doc §16) ──
    src/Qso.h
    src/LogbookModel.cpp
    src/LogbookModel.h
    src/AdifReader.cpp
    src/AdifReader.h
)

add_executable(shacklog-server ${SHACKLOG_SERVER_SOURCES})

target_include_directories(shacklog-server PRIVATE src src/server)

target_link_libraries(shacklog-server PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

if(MSVC)
    target_compile_options(shacklog-server PRIVATE /W3 /permissive-)
    target_compile_definitions(shacklog-server PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(shacklog-server PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Tests (opt-in: -DSHACKLOG_TESTS=ON) ─────────────────────────────────
option(SHACKLOG_TESTS "Build ShackLog unit tests" OFF)
if(SHACKLOG_TESTS)
    enable_testing()

    add_executable(aprs_decode_test
        tests/aprs_decode_test.cpp
        src/AprsDecode.cpp
        src/AprsDecode.h
    )
    target_include_directories(aprs_decode_test PRIVATE src)
    target_link_libraries(aprs_decode_test PRIVATE Qt6::Core)
    add_test(NAME aprs_decode_test COMMAND aprs_decode_test)
endif()
