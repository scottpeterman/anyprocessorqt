cmake_minimum_required(VERSION 3.19)

project(anyprocessorqt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)  # sources contain UTF-8 literals (bullets, dashes)
endif()

# Qt 6.4 is the floor: Debian bookworm (ChromeOS Linux container) ships 6.4.2.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)

option(ANYPROCESSORQT_BUILD_TESTS "Build the headless DOCX test runner" ON)

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
    src/docx/Units.h
    src/docx/DocxReader.cpp
    src/docx/DocxReader.h
    src/docx/DocxWriter.cpp
    src/docx/DocxWriter.h
)
target_include_directories(anyprocessor_core PUBLIC src)
target_link_libraries(anyprocessor_core PUBLIC Qt6::Core Qt6::Gui)

add_executable(anyprocessorqt
    src/main.cpp
    src/ui/PageEdit.cpp
    src/ui/PageEdit.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/InsertTableDialog.cpp
    src/ui/InsertTableDialog.h
)
target_compile_definitions(anyprocessorqt PRIVATE ANYPROCESSORQT_VERSION="${PROJECT_VERSION}")
target_link_libraries(anyprocessorqt PRIVATE anyprocessor_core Qt6::Widgets Qt6::PrintSupport)

if(WIN32)
    set_target_properties(anyprocessorqt PROPERTIES WIN32_EXECUTABLE ON)
elseif(APPLE)
    set_target_properties(anyprocessorqt PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.scottpeterman.anyprocessorqt
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()

if(ANYPROCESSORQT_BUILD_TESTS)
    add_executable(anyprocessorqt_tests tests/docx_tests.cpp)
    target_link_libraries(anyprocessorqt_tests PRIVATE anyprocessor_core)
endif()

install(TARGETS anyprocessorqt RUNTIME DESTINATION bin BUNDLE DESTINATION .)
