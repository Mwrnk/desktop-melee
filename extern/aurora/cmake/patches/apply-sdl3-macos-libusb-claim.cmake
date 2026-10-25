# On macOS, SDL's libusb HIDAPI backend refuses to open a device whose
# interface already has a driver attached unless it can detach that driver
# first. macOS only allows the detach with a special entitlement, so it always
# fails there. That locks out the official GameCube adapter (WUP-028) whenever
# GCAdapterDriver is installed: that DriverKit extension is a placeholder that
# exists precisely to keep IOHIDFamily off the adapter, and it leaves the
# interface claimable by libusb (Dolphin/Slippi rely on this). So on macOS a
# failed detach is not fatal: try the claim, which fails cleanly if the
# interface really is held.
if (NOT DEFINED SDL_SOURCE_DIR)
  message(FATAL_ERROR "SDL_SOURCE_DIR is required")
endif ()

set(_hid "${SDL_SOURCE_DIR}/src/hidapi/libusb/hid.c")
if (NOT EXISTS "${_hid}")
  message(FATAL_ERROR "SDL libusb HIDAPI source is missing: ${_hid}")
endif ()

file(READ "${_hid}" _source)

string(FIND "${_source}" "apply-sdl3-macos-libusb-claim" _already_patched)
if (NOT _already_patched EQUAL -1)
  message(STATUS "SDL3 macOS libusb claim patch already applied")
  return()
endif ()

set(_old "		if (res < 0) {
			LOG(\"Unable to detach Kernel Driver: (%d) %s\\n\", res, libusb_error_name(res));
			return 0;
		}")

set(_new "		if (res < 0) {
			LOG(\"Unable to detach Kernel Driver: (%d) %s\\n\", res, libusb_error_name(res));
#ifndef __APPLE__ /* see apply-sdl3-macos-libusb-claim.cmake */
			return 0;
#endif
		}")

string(FIND "${_source}" "${_old}" _patch_site)
if (_patch_site EQUAL -1)
  message(FATAL_ERROR "Failed to apply SDL3 macOS libusb claim patch: expected detach check not found")
endif ()

string(REPLACE "${_old}" "${_new}" _patched_source "${_source}")
file(WRITE "${_hid}" "${_patched_source}")
message(STATUS "Applied SDL3 macOS libusb claim patch")
