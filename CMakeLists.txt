cmake_minimum_required(VERSION 3.20)
project(CipherSNES VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CIPHERSNES_BUILD_GTK "Build cross-platform GTK3/SDL2 launcher" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(CIPHERSNES_BUILD_X11 "Build existing Linux X11 launcher" ON)
else()
    option(CIPHERSNES_BUILD_X11 "Build existing Linux X11 launcher" OFF)
endif()
option(CIPHERSNES_BUILD_SHARED "Build shared core in addition to static core" OFF)

find_package(LibArchive REQUIRED)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/util/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")

add_library(CipherSNESCore STATIC ${CORE_SOURCES})
target_include_directories(CipherSNESCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(CipherSNESCore PUBLIC LibArchive::LibArchive)
set_target_properties(CipherSNESCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CIPHERSNES_BUILD_SHARED)
    add_library(CipherSNESCoreShared SHARED ${CORE_SOURCES})
    target_include_directories(CipherSNESCoreShared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(CipherSNESCoreShared PRIVATE LibArchive::LibArchive)
    set_target_properties(CipherSNESCoreShared PROPERTIES OUTPUT_NAME CipherSNESCore)
    install(TARGETS CipherSNESCoreShared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(CIPHERSNES_BUILD_X11)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The X11 frontend is supported only on Linux; enable CIPHERSNES_BUILD_GTK instead")
    endif()
    find_package(X11 REQUIRED)
    add_executable(CipherSNES src/frontend_x11.cpp)
    target_include_directories(CipherSNES PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(CipherSNES PRIVATE CipherSNESCore X11::X11 ${CMAKE_DL_LIBS})
    add_executable(CipherSNES-NativePicker src/frontend_native_picker.cpp)
    target_link_libraries(CipherSNES-NativePicker PRIVATE ${CMAKE_DL_LIBS})
    install(TARGETS CipherSNES CipherSNES-NativePicker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(CIPHERSNES_BUILD_GTK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    file(GLOB GTK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/*.cpp")
    add_executable(CipherSNES-GTK ${GTK_SOURCES})
    target_include_directories(CipherSNES-GTK PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(CipherSNES-GTK PRIVATE CipherSNESCore PkgConfig::GTK3 PkgConfig::SDL2)
    if(WIN32)
        # Explorer, taskbar and executable-file icons (GTK window icon set at runtime).
        enable_language(RC)
        target_include_directories(CipherSNES-GTK PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        target_sources(CipherSNES-GTK PRIVATE assets/CipherSNES.rc)
    endif()
    if(APPLE)
        set_target_properties(CipherSNES-GTK PROPERTIES MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "CipherSNES"
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.HodgkinsStudios.CipherSNES"
            MACOSX_BUNDLE_ICON_FILE "CipherSNES.icns"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
        install(TARGETS CipherSNES-GTK BUNDLE DESTINATION .)
        install(FILES assets/CipherSNES_icon.png assets/CipherSNES.icns
            DESTINATION "CipherSNES-GTK.app/Contents/Resources")
    else()
        install(TARGETS CipherSNES-GTK RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

install(TARGETS CipherSNESCore ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES assets/CipherSNES_icon.png assets/CipherSNES_about.ppm
    DESTINATION ${CMAKE_INSTALL_DATADIR}/ciphersnes)
install(FILES assets/CipherSNES_icon_256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME io.github.HodgkinsStudios.CipherSNES.png)
install(FILES packaging/io.github.HodgkinsStudios.CipherSNES.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/io.github.HodgkinsStudios.CipherSNES.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

# Minimal cross-platform synthetic ROM regression. Generates its own test data.
include(CTest)
if(BUILD_TESTING)
    add_executable(CipherSNES-PortableSmoke tests/portable_smoke.cpp)
    target_link_libraries(CipherSNES-PortableSmoke PRIVATE CipherSNESCore)
    add_test(NAME CipherSNES-portable-rom-and-zip COMMAND CipherSNES-PortableSmoke)
    set_tests_properties(CipherSNES-portable-rom-and-zip PROPERTIES TIMEOUT 90)
endif()
