cmake_minimum_required(VERSION 3.6)

# Use QTDIR environment variable with find_package,
#  e.g. set QTDIR=/home/user/Qt/5.9.6/gcc_64/
if(NOT "$ENV{QTDIR}" STREQUAL "")
  set(QTDIR $ENV{QTDIR})
  list(APPEND CMAKE_PREFIX_PATH $ENV{QTDIR})
elseif(QTDIR)
  list(APPEND CMAKE_PREFIX_PATH ${QTDIR})
endif()

# Set the default build type to release
if( NOT CMAKE_BUILD_TYPE )
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")

project(Projecteur LANGUAGES CXX)

add_compile_options(-Wall -Wextra -Werror)
#set(CMAKE_CXX_CLANG_TIDY clang-tidy-12)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(GitVersion)
include(Translation)

set(QtVersionOptions "Auto" "5" "6")
set(PROJECTEUR_QT_VERSION "Auto" CACHE STRING "Choose the Qt version")
set_property(CACHE PROJECTEUR_QT_VERSION PROPERTY STRINGS ${QtVersionOptions})

list(FIND QtVersionOptions ${PROJECTEUR_QT_VERSION} index)
if(index EQUAL -1)
    message(FATAL_ERROR "PROJECTEUR_QT_VERSION must be one of ${QtVersionOptions}")
endif()

if ("${PROJECTEUR_QT_VERSION}" STREQUAL "Auto")
  find_package(QT NAMES Qt6 Qt5 RCOMPONENTS Core REQUIRED)
else()
  set(QT_VERSION_MAJOR ${PROJECTEUR_QT_VERSION})
endif()

find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
set(QT_PACKAGE_NAME Qt${QT_VERSION_MAJOR})

message(STATUS "Using Qt version: ${Qt${QT_VERSION_MAJOR}_VERSION}")

if(${QT_PACKAGE_NAME}_VERSION VERSION_LESS "6.0")
  set(CMAKE_CXX_STANDARD 14)
else()
  set(CMAKE_CXX_STANDARD 17)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(${QT_PACKAGE_NAME} 5.7 REQUIRED COMPONENTS Core Gui Quick Widgets)

if(${QT_PACKAGE_NAME}_VERSION VERSION_LESS "6.0")
  find_package(${QT_PACKAGE_NAME} QUIET COMPONENTS X11Extras)
  set(HAS_Qt_X11Extras ${${QT_PACKAGE_NAME}_FOUND})
else()
  set(HAS_Qt_X11Extras 0)
endif()

find_package(${QT_PACKAGE_NAME} QUIET COMPONENTS DBus)
set(HAS_Qt_DBus ${${QT_PACKAGE_NAME}_FOUND})
find_package(${QT_PACKAGE_NAME} QUIET COMPONENTS QuickCompiler)
set(HAS_Qt_QuickCompiler ${${QT_PACKAGE_NAME}_FOUND})

# Qt 5.8 seems to have issues with the way Projecteur shows the full screen overlay window,
# let's warn the user about it.
if(Qt5_VERSION VERSION_EQUAL "5.8"
    OR (Qt5_VERSION VERSION_GREATER "5.8" AND Qt5_VERSION VERSION_LESS "5.9"))
  message(WARNING "There are known issues when using Projecteur with Qt Version 5.8, "
                  "please use a different Qt Version.")
endif()

if (HAS_Qt_QuickCompiler)
  # Off by default, since this ties the application strictly to the Qt version
  # it is built with, see https://doc.qt.io/qt-5.12/qtquick-deployment.html#compiling-qml-ahead-of-time
  option(USE_QTQUICK_COMPILER "Use the QtQuickCompiler" OFF)
else()
  set(USE_QTQUICK_COMPILER OFF)
endif()

if (USE_QTQUICK_COMPILER)
  message(STATUS "Using QtQuick Compiler.")
  qtquick_compiler_add_resources(RESOURCES qml/qml.qrc)
  # Avoid CMake policy CMP0071 warning
  foreach(resfile IN LISTS RESOURCES)
    set_property(SOURCE "${resfile}" PROPERTY SKIP_AUTOMOC ON)
  endforeach()
else()
  if(${QT_PACKAGE_NAME}_VERSION VERSION_LESS "6.0")
    qt5_add_resources(RESOURCES qml/qml.qrc)
  else()
    qt6_add_resources(RESOURCES qml/qml-qt6.qrc)
  endif()
endif()

if(${QT_PACKAGE_NAME}_VERSION VERSION_LESS "6.0")
  qt5_add_resources(RESOURCES resources.qrc)
else()
  qt6_add_resources(RESOURCES resources.qrc)
endif()

add_executable(projecteur
  src/main.cc               src/enum-helper.h
  src/aboutdlg.cc           src/aboutdlg.h
  src/actiondelegate.cc     src/actiondelegate.h
  src/colorselector.cc      src/colorselector.h
  src/device.cc             src/device.h
  src/device-hidpp.cc       src/device-hidpp.h
  src/device-key-lookup.cc  src/device-key-lookup.h
  src/device-vibration.cc   src/device-vibration.h
  src/deviceinput.cc        src/deviceinput.h
  src/devicescan.cc         src/devicescan.h
  src/deviceswidget.cc      src/deviceswidget.h
  src/hidpp.cc              src/hidpp.h
  src/linuxdesktop.cc       src/linuxdesktop.h
  src/iconwidgets.cc        src/iconwidgets.h
  src/imageitem.cc          src/imageitem.h
  src/inputmapconfig.cc     src/inputmapconfig.h
  src/inputseqedit.cc       src/inputseqedit.h
  src/logging.cc            src/logging.h
  src/nativekeyseqedit.cc   src/nativekeyseqedit.h
  src/preferencesdlg.cc     src/preferencesdlg.h
  src/projecteurapp.cc      src/projecteurapp.h
  src/runguard.cc           src/runguard.h
  src/settings.cc           src/settings.h
  src/spotlight.cc          src/spotlight.h
  src/spotshapes.cc         src/spotshapes.h
  src/virtualdevice.h       src/virtualdevice.cc
  ${RESOURCES})

target_include_directories(projecteur PRIVATE src)

target_link_libraries(projecteur
  PRIVATE ${QT_PACKAGE_NAME}::Core ${QT_PACKAGE_NAME}::Quick ${QT_PACKAGE_NAME}::Widgets
)

if(HAS_Qt_X11Extras)
  if(${QT_PACKAGE_NAME}_VERSION VERSION_LESS "6.0")
    target_link_libraries(projecteur PRIVATE ${QT_PACKAGE_NAME}::X11Extras)
  endif()
  target_compile_definitions(projecteur PRIVATE HAS_Qt_X11Extras=1)
else()
  message(STATUS "Compiling without Qt5::X11Extras.")
endif()

if(HAS_Qt_DBus)
  target_link_libraries(projecteur PRIVATE ${QT_PACKAGE_NAME}::DBus)
  target_compile_definitions(projecteur PRIVATE HAS_Qt_DBus=1)
else()
  message(STATUS "Compiling without Qt5::DBus.")
endif()

target_compile_options(projecteur
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra>
)

target_compile_definitions(projecteur PRIVATE
  CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID} CXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION})

# Set version project properties for builds not from a git repository (e.g. created with git archive)
# If creating the version number via git information fails, the following target properties
# will be used. IMPORTANT - when creating a release tag with git flow:
#  Update this information - the version numbers and the version type.
#  VERSION_TYPE must be either 'release' or 'develop'
set_target_properties(projecteur PROPERTIES
  VERSION_MAJOR 1
  VERSION_MINOR 0
  VERSION_PATCH 0
  VERSION_TYPE develop
)
add_version_info(projecteur "${CMAKE_CURRENT_SOURCE_DIR}")

# Create files containing generated version strings, helping package maintainers
get_target_property(PROJECTEUR_VERSION_STRING projecteur VERSION_STRING)
# Arch Linux = PKGBUILD/makepkg: '-' is not allowed in version number
string(REPLACE "-" "" PROJECTEUR_VERSION_STRING_ARCHLINUX "${PROJECTEUR_VERSION_STRING}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version-string" "${PROJECTEUR_VERSION_STRING}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version-string.archlinux" "${PROJECTEUR_VERSION_STRING_ARCHLINUX}")

# Translation
list(APPEND languages de fr es)
set(ts_directories "${CMAKE_CURRENT_SOURCE_DIR}/i18n")
add_translations_target("projecteur" "${CMAKE_CURRENT_BINARY_DIR}" "${ts_directories}" "${languages}")
add_translation_update_task("projecteur" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/i18n" "${languages}")

# Add target with non-source files for convenience when using IDEs like QtCreator and others
add_custom_target(non-sources SOURCES README.md LICENSE.md doc/CHANGELOG.md devices.conf
                                      src/extra-devices.cc.in 55-projecteur.rules.in
                                      cmake/templates/Projecteur.desktop.in)

# Install
#---------------------------------------------------------------------------------------------------
# Set default directory permissions with CMake >= 3.11, avoids some
# lintian 'non-standard-dir-perm' errors for deb packages.
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE
)

install(TARGETS projecteur DESTINATION bin)
set(PROJECTEUR_INSTALL_PATH "${CMAKE_INSTALL_PREFIX}/bin/projecteur") #used in desktop file template

# Use udev.pc pkg-config file to set the dir path
if (NOT CMAKE_INSTALL_UDEVRULESDIR)
  set (UDEVDIR /lib/udev)
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PKGCONFIG_UDEV udev)
    if(PKGCONFIG_UDEV_FOUND)
      execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=udevdir udev
        OUTPUT_VARIABLE PKGCONFIG_UDEVDIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      if(PKGCONFIG_UDEVDIR)
        file(TO_CMAKE_PATH "${PKGCONFIG_UDEVDIR}" UDEVDIR)
      endif(PKGCONFIG_UDEVDIR)
    endif(PKGCONFIG_UDEV_FOUND)
  endif(PKG_CONFIG_FOUND)
endif(NOT CMAKE_INSTALL_UDEVRULESDIR)
set (CMAKE_INSTALL_UDEVDIR ${UDEVDIR} CACHE PATH "Udev base dir.")
mark_as_advanced(CMAKE_INSTALL_UDEVDIR)
set (CMAKE_INSTALL_UDEVRULESDIR ${UDEVDIR}/rules.d CACHE PATH "Where to install udev rules")
mark_as_advanced(CMAKE_INSTALL_UDEVRULESDIR)

# Configure and install files
set(OUTDIR "${CMAKE_CURRENT_BINARY_DIR}")
set(TMPLDIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates")

# Read devices.conf file
set(idRegex "0x([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])")
set(lineRegex "^[ \t]*${idRegex}[ \t]*,[ \t]*${idRegex}[ \t]*,[ \t]*(usb|bt)[ \t]*,[ \t]*(.*)[ \t]*")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/devices.conf" CONFLINES REGEX "${lineRegex}")
foreach(line ${CONFLINES})
  #message(STATUS "## ${line}")
  if(line MATCHES "${lineRegex}")
    # message(STATUS "vendorId: ${CMAKE_MATCH_1}, productId: ${CMAKE_MATCH_2}, ${CMAKE_MATCH_3}, '${CMAKE_MATCH_4}'")
    set(vendorId "${CMAKE_MATCH_1}")
    set(productId "${CMAKE_MATCH_2}")

    if("${CMAKE_MATCH_3}" STREQUAL "usb")
      string(APPEND EXTRA_USB_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_USB_UDEV_RULES "\nSUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"${vendorId}\"")
      string(APPEND EXTRA_USB_UDEV_RULES ", ATTRS{idProduct}==\"${productId}\", MODE=\"0660\", TAG+=\"uaccess\"")
      string(APPEND SUPPORTED_EXTRA_DEVICES "\n    {0x${vendorId}, 0x${productId}, false, \"${CMAKE_MATCH_4}\"}, // ${CMAKE_MATCH_4}")
    elseif("${CMAKE_MATCH_3}" STREQUAL "bt")
      string(APPEND SUPPORTED_EXTRA_DEVICES "\n    {0x${vendorId}, 0x${productId}, true, \"${CMAKE_MATCH_4}\"}, // ${CMAKE_MATCH_4}")
      if("${vendorId}" MATCHES "0*([0-9a-fA-F]+)")
        set(vendorId "${CMAKE_MATCH_1}")
      endif()
      if("${productId}" MATCHES "0*([0-9a-fA-F]+)")
        set(productId "${CMAKE_MATCH_1}")
      endif()
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\n## Extra-Device: ${CMAKE_MATCH_4}")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "\nSUBSYSTEMS==\"input\", ")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "ENV{LIBINPUT_DEVICE_GROUP}=\"5/${vendorId}/${productId}*\", ")
      string(APPEND EXTRA_BLUETOOTH_UDEV_RULES "MODE=\"0660\", TAG+=\"uaccess\"")
    endif()
  endif()
endforeach()

configure_file("src/extra-devices.cc.in" "src/extra-devices.cc" @ONLY)
set_property(TARGET projecteur APPEND PROPERTY SOURCES "${CMAKE_CURRENT_BINARY_DIR}/src/extra-devices.cc")

configure_file("55-projecteur.rules.in" "55-projecteur.rules" @ONLY)
install(FILES "${OUTDIR}/55-projecteur.rules" DESTINATION ${CMAKE_INSTALL_UDEVRULESDIR}/)

install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/48x48/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/64x64/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/128x128/apps/ RENAME projecteur.svg)
install(FILES icons/projecteur-tray.svg DESTINATION share/icons/hicolor/256x256/apps/ RENAME projecteur.svg)

# Set variables for file configurations
get_target_property(VERSION_STRING projecteur VERSION_STRING)
get_target_property(VERSION_DATE_MONTH_YEAR projecteur VERSION_DATE_MONTH_YEAR)
set(HOMEPAGE "https://github.com/jahnf/Projecteur")

configure_file("${TMPLDIR}/Projecteur.desktop.in" "projecteur.desktop" @ONLY)
install(FILES "${OUTDIR}/projecteur.desktop" DESTINATION share/applications/)

# Configure man page and gzip it.
option(COMPRESS_MAN_PAGE "Compress the man page" ON)
configure_file("${TMPLDIR}/projecteur.1" "${OUTDIR}/projecteur.1" @ONLY)

if(COMPRESS_MAN_PAGE)
  find_program(GZIP_EXECUTABLE gzip)
  add_custom_command(
    OUTPUT ${OUTDIR}/projecteur.1.gz
    COMMAND ${GZIP_EXECUTABLE} -9f -n "${OUTDIR}/projecteur.1"
    WORKING_DIRECTORY ${OUTDIR}
  )
  add_custom_target(gzip-manpage ALL DEPENDS "${OUTDIR}/projecteur.1.gz")
  install(FILES "${OUTDIR}/projecteur.1.gz" DESTINATION share/man/man1/)
else()
  install(FILES "${OUTDIR}/projecteur.1" DESTINATION share/man/man1/)
endif()

configure_file("${TMPLDIR}/projecteur.metainfo.xml" "projecteur.metainfo.xml" @ONLY)
install(FILES "${OUTDIR}/projecteur.metainfo.xml" DESTINATION share/metainfo/)

configure_file("${TMPLDIR}/projecteur.bash-completion" "projecteur.bash-completion" @ONLY)
install(FILES "${OUTDIR}/projecteur.bash-completion"
        DESTINATION share/bash-completion/completions/
        RENAME projecteur)

configure_file("${TMPLDIR}/preinst.in" "pkg/scripts/preinst" @ONLY)
configure_file("${TMPLDIR}/postinst.in" "pkg/scripts/postinst" @ONLY)

# --- Linux packaging ---
include(LinuxPackaging)

option(PACKAGE_TARGETS "Create packaging build targets" ON)

if(PACKAGE_TARGETS)
  # Add 'source-archive' target
  add_source_archive_target(projecteur)

  # Add 'dist-package' target: Creates a deb/rpm/tgz package depending on the current Linux distribution
  add_dist_package_target(
    PROJECT "${CMAKE_PROJECT_NAME}"
    TARGET projecteur
    DESCRIPTION_BRIEF "Linux/X11 application for the Logitech Spotlight device."
    DESCRIPTION_FULL
"Projecteur is a virtual laser pointer for use with inertial pointers such as
 the Logitech Spotlight. Projecteur can show a colored dot, a highlighted
 circle or a zoom effect to act as a pointer. The location of the pointer
 moves in response to moving the handheld pointer device. The effect is much
 like that of a traditional laser pointer, except that it is captured by
 recording software and works across multiple screens."
    CONTACT "Jahn Fuchs <projecteur@jahn.textmo.de>"
    HOMEPAGE "${HOMEPAGE}"
    DEBIAN_SECTION "utils"
    # PREINST_SCRIPT "${OUTDIR}/pkg/scripts/preinst"
    POSTINST_SCRIPT "${OUTDIR}/pkg/scripts/postinst"
  )
  add_dependencies(dist-package projecteur)
  if(TARGET gzip-manpage)
    add_dependencies(dist-package gzip-manpage)
  endif()

  # Additional files for debian packages, adhering to some debian rules,
  # see https://manpages.debian.org/buster/lintian/lintian.1.en.html
  if ("${PKG_TYPE}" STREQUAL "DEB")
    # TODO Lintian expects the the copyright file in /usr/share/doc/projecteur
    #      This clashes with the default CMAKE_INSTALL_PREFIX /usr/local
    #      Need to check if this would clash with packages from the debian/ubuntu repos.
    #configure_file("${TMPLDIR}/copyright.in" "pkg/copyright" @ONLY)
    #install(FILES "${OUTDIR}/pkg/copyright" DESTINATION /usr/share/doc/projecteur/)

    ## -- prevent additional lintian warnings 'non-standard-dir-perm' for the cmake install prefix
    set(DIR_TO_INSTALL "${CMAKE_INSTALL_PREFIX}")
    while(NOT "${DIR_TO_INSTALL}" STREQUAL "/" AND NOT "${DIR_TO_INSTALL}" STREQUAL "")
      install(DIRECTORY DESTINATION "${DIR_TO_INSTALL}")
      get_filename_component(DIR_TO_INSTALL "${DIR_TO_INSTALL}" PATH)
    endwhile()
  endif()
endif()

option(ENABLE_IWYU "Enable Include-What-You-Use" OFF)
find_program(iwyu_path NAMES include-what-you-use iwyu)
if(ENABLE_IWYU AND iwyu_path)
  set_property(TARGET projecteur PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
endif()

