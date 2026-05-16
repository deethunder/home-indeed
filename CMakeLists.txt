cmake_minimum_required(VERSION 3.28...3.30)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(ENABLE_QT ON CACHE BOOL "Force Enable Qt" FORCE)
set(ENABLE_FRONTEND_API ON CACHE BOOL "Force Enable Frontend API" FORCE)
set(OBS_WARNINGS_AS_ERRORS OFF CACHE BOOL "Force Disable Warnings as Errors" FORCE)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF CACHE BOOL "Force Disable Warnings as Errors" FORCE)

set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_EXPLICIT_MODULES "NO")

if(APPLE)
    add_compile_options(-Wno-error -Wno-shorten-64-to-32)
elseif(UNIX AND NOT APPLE)
    add_compile_options(-Wno-error)
endif()

if(NOT MSVC)
    add_compile_options(-Wno-error -Wno-shorten-64-to-32)
endif()

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
  find_package(Qt6 COMPONENTS Widgets Core Network Gui Concurrent Svg REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Gui Qt6::Concurrent Qt6::Svg)
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
# Integrated libraries
set(WHISPER_METAL OFF CACHE BOOL "Force disable Metal to fix Apple ARC errors" FORCE)
set(GGML_METAL OFF CACHE BOOL "Force disable Metal to fix Apple ARC errors" FORCE)
add_subdirectory(vendor/whisper.cpp)

# VOSK C++ API LIBRARY
if(WIN32)
  include(FetchContent)
  FetchContent_Declare(
      vosk_api
      URL "https://github.com/alphacep/vosk-api/releases/download/v0.3.42/vosk-win64-0.3.42.zip"
  )
  FetchContent_MakeAvailable(vosk_api)
  include_directories(${vosk_api_SOURCE_DIR})
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
  src/plugin-main.cpp
  src/audio/homein-audio.cpp
  src/stt/homein-stt.cpp
  src/stt/homein-whisper.cpp
  src/stt/homein-deepgram.cpp
  src/dock/homein-dock.cpp
  src/tabs/homein-transcript-tab.cpp
  src/tabs/homein-bible-tab.cpp
  src/tabs/homein-lyrics-tab.cpp
  src/tabs/homein-queue-tab.cpp
  src/tabs/homein-settings-tab.cpp
  src/database/homein-db.cpp
  src/database/homein-lyrics-db.cpp
  src/database/homein-importer.cpp
  src/detection/homein-ref-parser.cpp
  src/detection/homein-lyrics-engine.cpp
  src/updater/homein-updater.cpp
  src/renderer/homein-renderer.cpp
  src/network/homein-http.cpp
  vendor/sqlite3/sqlite3.c
  src/resources.qrc
)

if (WIN32)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
    src/stt/homein-vosk.cpp
  )

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE 
  src
  vendor/whisper.cpp/include
  vendor/sqlite3
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE whisper)

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE 
    winhttp
    "${vosk_api_SOURCE_DIR}/libvosk.lib"
  )
endif()

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE 
  SQLITE_ENABLE_FTS5=1
  SQLITE_ENABLE_FTS4=1
  SQLITE_ENABLE_FTS3=1
  SQLITE_ENABLE_FTS3_PARENTHESIS=1
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})


# PHASE 5: CPack Installation Logic
# 1. Embed the SQLite Databases
install(FILES
    "${CMAKE_SOURCE_DIR}/data/homein-bible.db"
    "${CMAKE_SOURCE_DIR}/data/homein-lyrics.db"
    DESTINATION "data/obs-plugins/${CMAKE_PROJECT_NAME}"
)

# 2. Embed the AI Model Folder
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/models"
    DESTINATION "data/obs-plugins/${CMAKE_PROJECT_NAME}"
)

# 3. EXPLICITLY Embed Vosk Dependencies in OBS's Main Execution Directory!
if(WIN32)
  install(FILES 
      "${vosk_api_SOURCE_DIR}/libvosk.dll"
      "${vosk_api_SOURCE_DIR}/libgcc_s_seh-1.dll"
      "${vosk_api_SOURCE_DIR}/libstdc++-6.dll"
      "${vosk_api_SOURCE_DIR}/libwinpthread-1.dll"
      DESTINATION "bin/64bit"
  )
endif()

# --- CPack Configuration ---
set(CPACK_PACKAGE_NAME "Home-Indeed")
set(CPACK_PACKAGE_VENDOR "Home Indeed Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AI-powered Bible & Lyrics for OBS")
set(CPACK_PACKAGE_VERSION ${_version})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")

    # Set the default installation directory to "Program Files" for 64-bit systems
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "obs-studio")

    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/installer-logo.ico")
    set(CPACK_NSIS_PACKAGE_NAME "Home Indeed OBS Plugin")
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${_version}-windows-x64-Installer")
    set(CPACK_NSIS_MODIFY_PATH OFF)

elseif(APPLE)
    set(CPACK_GENERATOR "productbuild;ZIP")

elseif(UNIX)
    set(CPACK_GENERATOR "DEB;ZIP")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Home Indeed Team")
endif()

include(CPack)