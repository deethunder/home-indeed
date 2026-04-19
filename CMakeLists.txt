cmake_minimum_required(VERSION 3.28...3.30)

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
  src/detection/homein-ref-parser.cpp
  src/detection/homein-lyrics-engine.cpp
  src/updater/homein-updater.cpp
  src/renderer/homein-renderer.cpp
  vendor/sqlite3/sqlite3.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE 
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
