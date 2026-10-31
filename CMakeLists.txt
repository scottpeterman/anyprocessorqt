cmake_minimum_required(VERSION 3.19)

project(anyprocessorqt VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    add_compile_options(/utf-8)  # sources contain UTF-8 literals (bullets, dashes)
endif()

# Qt 6.4 is the floor: Debian bookworm (ChromeOS Linux container) ships 6.4.2.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)

option(ANYPROCESSORQT_BUILD_TESTS "Build the headless DOCX test runner" ON)

# Hunspell, vendored (third_party/README.md), built static.
file(GLOB HUNSPELL_SOURCES CONFIGURE_DEPENDS third_party/hunspell/src/*.cxx)
add_library(hunspell_static STATIC ${HUNSPELL_SOURCES})
target_include_directories(hunspell_static PUBLIC third_party/hunspell/src)
target_compile_definitions(hunspell_static PUBLIC HUNSPELL_STATIC PRIVATE BUILDING_LIBHUNSPELL)
set_target_properties(hunspell_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(hunspell_static PRIVATE /W0 /wd4244 /wd4267)
else()
    target_compile_options(hunspell_static PRIVATE -w)
endif()

# Spell checking: Hunspell wrapper and as-you-type highlighter (no widgets).
add_library(anyprocessor_spell STATIC
    src/spell/SpellChecker.cpp
    src/spell/SpellChecker.h
    src/spell/SpellHighlighter.cpp
    src/spell/SpellHighlighter.h
    resources/dictionaries.qrc
)
target_include_directories(anyprocessor_spell PUBLIC src)
target_link_libraries(anyprocessor_spell PUBLIC Qt6::Core Qt6::Gui PRIVATE hunspell_static)

# DOCX/zip core: no widgets, shared by the app and the tests.
add_library(anyprocessor_core STATIC
    src/zip/Zip.cpp
    src/zip/Zip.h
    src/docx/Xml.cpp
    src/docx/Xml.h
    src/docx/Props.cpp
    src/docx/Props.h
    src/docx/Styles.cpp
    src/docx/Styles.h
    src/docx/Numbering.cpp
    src/docx/Numbering.h
    src/docx/Package.cpp
    src/docx/Package.h
    src/docx/Convert.cpp
    src/docx/Convert.h
    src/docx/CodeStyles.cpp
    src/docx/CodeStyles.h
    src/docx/Units.h
    src/docx/DocxReader.cpp
    src/docx/DocxReader.h
    src/docx/DocxWriter.cpp
    src/docx/DocxWriter.h
)
target_include_directories(anyprocessor_core PUBLIC src)
target_link_libraries(anyprocessor_core PUBLIC Qt6::Core Qt6::Gui)

set(APP_ICON_SOURCES resources/resources.qrc)
if(WIN32)
    list(APPEND APP_ICON_SOURCES packaging/windows/anyprocessorqt.rc)
elseif(APPLE)
    set(MACOS_ICON ${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/anyprocessorqt.icns)
    set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND APP_ICON_SOURCES ${MACOS_ICON})
endif()

add_executable(anyprocessorqt
    ${APP_ICON_SOURCES}
    src/main.cpp
    src/ui/PageEdit.cpp
    src/ui/PageEdit.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/InsertTableDialog.cpp
    src/ui/InsertTableDialog.h
    src/ui/FindBar.cpp
    src/ui/FindBar.h
    src/ui/ZoomView.cpp
    src/ui/ZoomView.h
    src/ui/Theme.cpp
    src/ui/Theme.h
)
target_compile_definitions(anyprocessorqt PRIVATE ANYPROCESSORQT_VERSION="${PROJECT_VERSION}")
target_link_libraries(anyprocessorqt PRIVATE anyprocessor_core anyprocessor_spell Qt6::Widgets Qt6::PrintSupport)

if(WIN32)
    set_target_properties(anyprocessorqt PROPERTIES WIN32_EXECUTABLE ON)
elseif(APPLE)
    set_target_properties(anyprocessorqt PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.scottpeterman.anyprocessorqt
        MACOSX_BUNDLE_BUNDLE_NAME anyprocessorqt
        MACOSX_BUNDLE_ICON_FILE anyprocessorqt.icns
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()

if(ANYPROCESSORQT_BUILD_TESTS)
    add_executable(anyprocessorqt_tests tests/docx_tests.cpp src/ui/PageEdit.cpp src/ui/PageEdit.h src/ui/FindBar.cpp src/ui/FindBar.h src/ui/ZoomView.cpp src/ui/ZoomView.h src/ui/Theme.cpp src/ui/Theme.h)
    target_link_libraries(anyprocessorqt_tests PRIVATE anyprocessor_core anyprocessor_spell Qt6::Widgets)
endif()

install(TARGETS anyprocessorqt RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/anyprocessorqt.desktop DESTINATION share/applications)
    foreach(size 16 32 48 64 128 256 512)
        install(FILES packaging/icons/anyprocessorqt-${size}.png
                DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME anyprocessorqt.png)
    endforeach()
endif()
