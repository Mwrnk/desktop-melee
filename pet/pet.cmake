# melee-pet additions, included from the root CMakeLists.txt after the melee
# target. Our code lives in pet/; upstream files only get small hooks.
target_sources(melee PRIVATE pet/src/pet.c pet/src/assets.c pet/src/gctex.c
        pet/src/disc_check.cpp)

# The disc check's command-line tester (pet/tests/disc_check_test.sh builds it).
# Same sources and links as upstream's launcher_data_test.
add_executable(pet_disc_check EXCLUDE_FROM_ALL pet/tools/disc_check_main.c
        pet/src/disc_check.cpp src/pc/launcher_data.cpp src/pc/disc_open.c)
target_include_directories(pet_disc_check PRIVATE src pet/src)
target_link_libraries(pet_disc_check PRIVATE aurora::dvd ${MELEE_CRYPTO_LIBS})
target_include_directories(melee PRIVATE pet/src)
# The game code (built by GCC) calls pet_hide_stage() and friends, and
# includes our game mode (GM_PET), which is built as game code too.
target_include_directories(melee_game PRIVATE pet/src pet/game)
target_sources(melee_game PRIVATE pet/game/gmpetmode.c pet/game/petassets.c)

if (APPLE AND NOT (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    target_sources(melee PRIVATE pet/src/overlay_mac.m)
    target_link_libraries(melee PRIVATE "-framework AppKit" "-framework ImageIO")

    # The menu bar app (SwiftUI), as a static library linked into the game.
    enable_language(Swift)
    add_library(pet_ui STATIC
            pet/ui/MenuBar.swift
            pet/ui/Panel.swift
            pet/ui/MatchTab.swift
            pet/ui/PlayersTab.swift
            pet/ui/ControlsTab.swift
            pet/ui/DisplayTab.swift
            pet/ui/GameAssets.swift
            pet/ui/Emblem.swift
            pet/ui/DiscMemory.swift)
    set_target_properties(pet_ui PROPERTIES
            Swift_MODULE_NAME PetUI
            Swift_LANGUAGE_VERSION 5)
    # The project's directory-wide C flags (-fsigned-char, ...) mean nothing to
    # swiftc; start the Swift target from a clean slate. PetBridge is the C
    # interface the UI calls (module map in pet/ui/bridge).
    set_property(TARGET pet_ui PROPERTY COMPILE_OPTIONS
            "SHELL:-I ${CMAKE_CURRENT_SOURCE_DIR}/pet/ui/bridge")
    set_property(TARGET pet_ui PROPERTY COMPILE_DEFINITIONS "")
    target_link_libraries(melee PRIVATE pet_ui
            "-framework SwiftUI" "-framework Combine")
    # The game links with clang++, not swiftc (which CMake would pick for a
    # target with a Swift dependency), so point it at the Swift runtime the
    # Swift objects autolink against.
    set_target_properties(melee PROPERTIES LINKER_LANGUAGE CXX)
    execute_process(COMMAND xcrun --find swiftc OUTPUT_VARIABLE PET_SWIFTC
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(PET_SWIFT_BIN "${PET_SWIFTC}" DIRECTORY)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE PET_SDK
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    target_link_directories(melee PRIVATE
            "${PET_SWIFT_BIN}/../lib/swift/macosx"
            "${PET_SDK}/usr/lib/swift")
    target_link_options(melee PRIVATE "LINKER:-rpath,/usr/lib/swift")
endif ()
