# melee-pet additions, included from the root CMakeLists.txt after the melee
# target. Our code lives in pet/; upstream files only get small hooks.
target_sources(melee PRIVATE pet/src/pet.c)
target_include_directories(melee PRIVATE pet/src)
# The game code (built by GCC) calls pet_hide_stage() and friends.
target_include_directories(melee_game PRIVATE pet/src)

if (APPLE AND NOT (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    target_sources(melee PRIVATE pet/src/overlay_mac.m)
    target_link_libraries(melee PRIVATE "-framework AppKit")
endif ()
