include(${CMAKE_SOURCE_DIR}/cmake/platform/${CORE_SYSTEM_NAME}/wayland.cmake)

# Kodi draws with its own Wayland windowing, so wayland is a platform as well.
list(APPEND CORE_PLATFORM_NAME_LC wayland)

list(APPEND PLATFORM_OPTIONAL_DEPS SMB2)
list(APPEND ARCH_DEFINES -DTARGET_TIZEN)

set(PLATFORM_OPTIONAL_DEPS_EXCLUDE CEC Alsa)
set(ENABLE_PULSEAUDIO ON CACHE BOOL "" FORCE)
set(TARGET_TIZEN TRUE)

# The sysroot has the libraries Kodi links against, not all that they need in turn,
# which are on the TV.
string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--allow-shlib-undefined")
