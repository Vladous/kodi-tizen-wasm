# Assembles the Tizen package directory, run by "make bundle" (see Install.cmake) with
# APP_PACKAGE_DIR, PACKAGING_SOURCE, MANIFEST, BINARY, STRIP, DEPENDS_PATH, PYTHON_VERSION,
# HOST_PYTHON, ADDON_REPOSITORY_URL, ADDON_REPOSITORY_TEMPLATE, BUNDLED_ADDONS, INSTALL_DIRS and
# EXTRA_LIBS (lists separated by "|").

string(REPLACE "|" ";" INSTALL_DIRS "${INSTALL_DIRS}")
string(REPLACE "|" ";" EXTRA_LIBS "${EXTRA_LIBS}")
string(REPLACE "|" ";" BUNDLED_ADDONS "${BUNDLED_ADDONS}")

file(REMOVE_RECURSE ${APP_PACKAGE_DIR})
file(INSTALL ${PACKAGING_SOURCE}/KodiHost.csproj ${PACKAGING_SOURCE}/Program.cs
             ${PACKAGING_SOURCE}/shared ${MANIFEST}
     DESTINATION ${APP_PACKAGE_DIR})

file(MAKE_DIRECTORY ${APP_PACKAGE_DIR}/lib)
execute_process(COMMAND ${STRIP} -o ${APP_PACKAGE_DIR}/lib/kodi.bin ${BINARY}
                COMMAND_ERROR_IS_FATAL ANY)
foreach(lib IN LISTS EXTRA_LIBS)
  get_filename_component(name ${lib} NAME)
  execute_process(COMMAND ${STRIP} -o ${APP_PACKAGE_DIR}/lib/${name} ${lib}
                  COMMAND_ERROR_IS_FATAL ANY)
endforeach()

file(INSTALL ${INSTALL_DIRS} DESTINATION ${APP_PACKAGE_DIR}/res/kodi)
file(INSTALL ${DEPENDS_PATH}/lib/python${PYTHON_VERSION} DESTINATION ${APP_PACKAGE_DIR}/lib
     PATTERN __pycache__ EXCLUDE
     PATTERN test EXCLUDE
     PATTERN tests EXCLUDE
     PATTERN idlelib EXCLUDE
     PATTERN tkinter EXCLUDE
     PATTERN turtledemo EXCLUDE
     PATTERN ensurepip EXCLUDE
     # what installs packages, the help and the interactive console, of no use on a TV
     PATTERN setuptools EXCLUDE
     PATTERN pkg_resources EXCLUDE
     PATTERN *.dist-info EXCLUDE
     PATTERN *.egg-info EXCLUDE
     PATTERN EGG-INFO EXCLUDE
     PATTERN pydoc_data EXCLUDE
     PATTERN _pyrepl EXCLUDE
     PATTERN config-${PYTHON_VERSION}-* EXCLUDE)
# The package is read-only on a TV, so Python would compile every module it imports again
# each time Kodi starts. The bytecode takes the place of the sources, which saves the room
# they take on a TV, and does not check them.
execute_process(COMMAND ${HOST_PYTHON} -m compileall -q -b -j 0 --invalidation-mode unchecked-hash
                        ${APP_PACKAGE_DIR}/lib/python${PYTHON_VERSION}
                COMMAND_ERROR_IS_FATAL ANY)
file(GLOB_RECURSE python_sources ${APP_PACKAGE_DIR}/lib/python${PYTHON_VERSION}/*.py)
foreach(source IN LISTS python_sources)
  if(EXISTS ${source}c)
    file(REMOVE ${source})
  endif()
endforeach()

# Binary add-ons Kodi comes with: the library their addon.xml names, and their data next to
# Kodi's. The others of the depends are installed from the add-on repository.
foreach(addon IN LISTS BUNDLED_ADDONS)
  set(addon_dir ${DEPENDS_PATH}/share/kodi/addons/${addon})
  if(NOT EXISTS ${addon_dir}/addon.xml)
    message(FATAL_ERROR "Binary add-on ${addon} is not built, see tools/depends/target/binary-addons")
  endif()
  file(INSTALL ${addon_dir} DESTINATION ${APP_PACKAGE_DIR}/res/kodi/addons)
  file(READ ${addon_dir}/addon.xml addon_xml)
  if(addon_xml MATCHES "library_linux=\"([^\"]+)\"")
    file(MAKE_DIRECTORY ${APP_PACKAGE_DIR}/lib/kodi/addons/${addon})
    execute_process(COMMAND ${STRIP} -o ${APP_PACKAGE_DIR}/lib/kodi/addons/${addon}/${CMAKE_MATCH_1}
                            ${DEPENDS_PATH}/lib/kodi/addons/${addon}/${CMAKE_MATCH_1}
                    COMMAND_ERROR_IS_FATAL ANY)
  endif()
endforeach()

# The add-on repository for Tizen, as tools/tizen/repository/make-repository.py makes it
if(ADDON_REPOSITORY_URL)
  set(repository_dir ${APP_PACKAGE_DIR}/res/kodi/addons/repository.kodi-tizen)
  file(READ ${ADDON_REPOSITORY_TEMPLATE}/addon.xml.in repository_xml)
  string(REPLACE "@URL@" "${ADDON_REPOSITORY_URL}" repository_xml "${repository_xml}")
  file(WRITE ${repository_dir}/addon.xml "${repository_xml}")
  file(INSTALL ${ADDON_REPOSITORY_TEMPLATE}/ DESTINATION ${repository_dir}
       PATTERN addon.xml.in EXCLUDE)

  # Enabled from the start as an optional system add-on, as add-ons Kodi comes with are
  # disabled otherwise.
  set(manifest ${APP_PACKAGE_DIR}/res/kodi/system/addon-manifest.xml)
  file(READ ${manifest} manifest_xml)
  string(REPLACE "</addons>" "  <addon optional=\"true\">repository.kodi-tizen</addon>\n</addons>"
                 manifest_xml "${manifest_xml}")
  file(WRITE ${manifest} "${manifest_xml}")
endif()

# Development builds
if(DEFINED ENV{KODI_LOG_URL})
  file(WRITE ${APP_PACKAGE_DIR}/res/kodi-log-url "$ENV{KODI_LOG_URL}\n")
endif()
if(DEFINED ENV{KODI_ARGS})
  file(WRITE ${APP_PACKAGE_DIR}/res/kodi-args "$ENV{KODI_ARGS}\n")
endif()
if(DEFINED ENV{KODI_DEV_WEBSERVER})
  file(WRITE ${APP_PACKAGE_DIR}/res/dev-guisettings.xml
"<settings version=\"2\">
    <setting id=\"services.webserver\">true</setting>
    <setting id=\"services.webserverport\">8095</setting>
    <setting id=\"services.webserverusername\">kodi</setting>
    <setting id=\"services.webserverpassword\">$ENV{KODI_DEV_WEBSERVER}</setting>
    <setting id=\"services.webserverauthentication\">true</setting>
</settings>
")
endif()
