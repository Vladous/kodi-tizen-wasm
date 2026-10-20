# generate the C++ wrappers of the Tizen wayland extensions Kodi uses
set(TIZEN_PROTOCOL_XMLS "${CMAKE_SOURCE_DIR}/xbmc/platform/linux/tizen/tizen-extension.xml")
add_custom_command(OUTPUT "${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-tizen-protocols.hpp" "${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-tizen-protocols.cpp"
                  COMMAND wayland::waylandppscanner
                  ARGS ${TIZEN_PROTOCOL_XMLS} "${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-tizen-protocols.hpp" "${WAYLAND_EXTRA_PROTOCOL_GENERATED_DIR}/wayland-tizen-protocols.cpp"
                  DEPENDS wayland::waylandppscanner ${TIZEN_PROTOCOL_XMLS}
                  COMMENT "Generating wayland-tizen C++ wrappers")
add_custom_target(generate-wayland-tizen-protocols DEPENDS wayland-tizen-protocols.hpp)

add_dependencies(lib${APP_NAME_LC} generate-wayland-tizen-protocols)
