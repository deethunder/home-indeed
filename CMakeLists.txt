cmake_minimum_required(VERSION 3.28...3.30)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network Gui REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Gui)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# Integrated libraries
add_subdirectory(vendor/whisper.cpp)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
  src/plugin-main.cpp
  src/audio/homein-audio.cpp
  src/stt/homein-stt.cpp
  src/dock/homein-dock.cpp
  src/database/homein-db.cpp
  src/database/homein-lyrics-db.cpp
  src/database/homein-importer.cpp
  src/detection/homein-ref-parser.cpp
  src/detection/homein-lyrics-engine.cpp
  src/updater/homein-updater.cpp
  src/renderer/homein-renderer.cpp
  vendor/sqlite3/sqlite3.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE 
  src
  vendor/whisper.cpp/include
  vendor/sqlite3
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE whisper)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# --- Packaging Logic (Phase 5: Polish) ---
set(CPACK_PACKAGE_NAME "Home Indeed")
set(CPACK_PACKAGE_VENDOR "Home Indeed Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AI-powered Bible & Lyrics for OBS")
set(CPACK_PACKAGE_VERSION ${_version})

# Generators per OS
if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Home Indeed OBS Plugin")
    set(CPACK_NSIS_MODIFY_PATH ON)
elseif(APPLE)
    set(CPACK_GENERATOR "productbuild;ZIP")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;ZIP")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Home Indeed Team")
endif()

include(CPack)

# --- Testing Framework (v1.1) ---
option(BUILD_TESTS "Build automated tests" OFF)
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    # For Windows: prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(homein-tests 
        tests/test-resampler.cpp
        tests/test-database.cpp
        src/audio/homein-audio.cpp
        src/database/homein-db.cpp
        src/database/homein-lyrics-db.cpp
    )
    target_link_libraries(homein-tests PRIVATE gtest_main libobs Qt6::Core Qt6::Network)
    target_include_directories(homein-tests PRIVATE src/audio src/database src)
    
    add_test(NAME HomeInAudioTests COMMAND homein-tests)
endif()

# --- Automated Installer Build (v1.0) ---
if(WIN32 AND ENABLE_QT)
    set(INSTALLER_SOURCES
        src/installer/homein-installer.cpp
        src/installer/resources.qrc
    )
    
    add_executable(home-indeed-installer WIN32 ${INSTALLER_SOURCES})
    
    target_link_libraries(home-indeed-installer PRIVATE 
        Qt6::Widgets 
        Qt6::Core 
        Qt6::Gui 
        shlwapi 
        ole32 
        shell32 
        advapi32
    )

    set_target_properties(home-indeed-installer PROPERTIES 
        AUTOMOC ON 
        AUTORCC ON 
        OUTPUT_NAME "Home-Indeed-Installer"
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
    
    # Add dependency to ensure the plugin is built before the installer (optional but good practice)
    add_dependencies(home-indeed-installer ${CMAKE_PROJECT_NAME})

    # --- Uninstaller (Phase 5) ---
    set(UNINSTALLER_SOURCES
        src/installer/homein-uninstaller.cpp
    )
    add_executable(home-indeed-uninstaller WIN32 ${UNINSTALLER_SOURCES})
    target_link_libraries(home-indeed-uninstaller PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui)
    set_target_properties(home-indeed-uninstaller PROPERTIES 
        AUTOMOC ON 
        OUTPUT_NAME "Home-Indeed-Uninstaller"
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )

    # --- Copy databases and data folder to Release output ---
    # This ensures the installer can find and bundle them alongside the .exe
    add_custom_command(TARGET home-indeed-installer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/data/homein-bible.db"
            "$<TARGET_FILE_DIR:home-indeed-installer>/homein-bible.db"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/data/homein-lyrics.db"
            "$<TARGET_FILE_DIR:home-indeed-installer>/homein-lyrics.db"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:home-indeed-installer>/data"
        COMMENT "Bundling databases alongside installer..."
    )
endif()
