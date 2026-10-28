# desktop-melee additions, included from the root CMakeLists.txt after the melee
# target. Our code lives in desktop-melee/; upstream files only get small hooks.
target_sources(melee PRIVATE desktop-melee/src/desktop_melee.c desktop-melee/src/assets.c
        desktop-melee/src/gctex.c desktop-melee/src/disc_check.cpp)

# The disc check's command-line tester (desktop-melee/tests/disc_check_test.sh builds it).
# Same sources and links as upstream's launcher_data_test.
add_executable(desktop_melee_disc_check EXCLUDE_FROM_ALL desktop-melee/tools/disc_check_main.c
        desktop-melee/src/disc_check.cpp src/pc/launcher_data.cpp src/pc/disc_open.c)
target_include_directories(desktop_melee_disc_check PRIVATE src desktop-melee/src)
target_link_libraries(desktop_melee_disc_check PRIVATE aurora::dvd ${MELEE_CRYPTO_LIBS})
target_include_directories(melee PRIVATE desktop-melee/src)
# The game code (built by GCC) calls desktop_melee_hide_stage() and friends, and
# includes our game mode (GM_DESKTOP_MELEE), which is built as game code too.
target_include_directories(melee_game PRIVATE desktop-melee/src desktop-melee/game)
target_sources(melee_game PRIVATE desktop-melee/game/gmdesktopmelee.c
        desktop-melee/game/desktop_melee_assets.c)

# One implementation of desktop-melee/src/platform.h per system
# (docs/desktop-melee/porting.md). Windows and Linux are skeletons that run
# the game in a normal window; the Linux one is plain SDL, so every other
# system (Android, iOS) builds it too.
set(DESKTOP_MELEE_MACOS OFF)
if (APPLE AND NOT (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    set(DESKTOP_MELEE_MACOS ON)
elseif (WIN32)
    target_sources(melee PRIVATE desktop-melee/src/platform_windows.c)
else ()
    target_sources(melee PRIVATE desktop-melee/src/platform_linux.c)
endif ()

if (DESKTOP_MELEE_MACOS)
    target_sources(melee PRIVATE desktop-melee/src/overlay_mac.m)
    target_link_libraries(melee PRIVATE "-framework AppKit" "-framework ImageIO")

    # The menu bar app (SwiftUI), as a static library linked into the game.
    enable_language(Swift)
    add_library(desktop_melee_ui STATIC
            desktop-melee/ui/MenuBar.swift
            desktop-melee/ui/Panel.swift
            desktop-melee/ui/MatchTab.swift
            desktop-melee/ui/PlayersTab.swift
            desktop-melee/ui/ControlsTab.swift
            desktop-melee/ui/DisplayTab.swift
            desktop-melee/ui/GameAssets.swift
            desktop-melee/ui/Emblem.swift
            desktop-melee/ui/DiscMemory.swift
            desktop-melee/ui/Welcome.swift
            desktop-melee/ui/WelcomeSteps.swift
            desktop-melee/ui/Hotkey.swift
            desktop-melee/ui/Loading.swift
            desktop-melee/ui/Settings.swift)
    set_target_properties(desktop_melee_ui PROPERTIES
            Swift_MODULE_NAME DesktopMeleeUI
            Swift_LANGUAGE_VERSION 5)
    # The project's directory-wide C flags (-fsigned-char, ...) mean nothing to
    # swiftc; start the Swift target from a clean slate. DesktopMeleeBridge is the C
    # interface the UI calls (module map in desktop-melee/ui/bridge).
    set_property(TARGET desktop_melee_ui PROPERTY COMPILE_OPTIONS
            "SHELL:-I ${CMAKE_CURRENT_SOURCE_DIR}/desktop-melee/ui/bridge")
    set_property(TARGET desktop_melee_ui PROPERTY COMPILE_DEFINITIONS "")
    target_link_libraries(melee PRIVATE desktop_melee_ui
            "-framework SwiftUI" "-framework Combine" "-framework Carbon")
    # The game links with clang++, not swiftc (which CMake would pick for a
    # target with a Swift dependency), so point it at the Swift runtime the
    # Swift objects autolink against.
    set_target_properties(melee PROPERTIES LINKER_LANGUAGE CXX)
    execute_process(COMMAND xcrun --find swiftc OUTPUT_VARIABLE DESKTOP_MELEE_SWIFTC
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(DESKTOP_MELEE_SWIFT_BIN "${DESKTOP_MELEE_SWIFTC}" DIRECTORY)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE DESKTOP_MELEE_SDK
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_link_directories(melee PRIVATE
            "${DESKTOP_MELEE_SWIFT_BIN}/../lib/swift/macosx"
            "${DESKTOP_MELEE_SDK}/usr/lib/swift")
    target_link_options(melee PRIVATE "LINKER:-rpath,/usr/lib/swift")
endif ()
