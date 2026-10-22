cmake_minimum_required(VERSION 3.21)
project(Tessera VERSION 0.10.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core CorePrivate Gui Widgets Sql Pdf Charts Svg Test)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
endif()

# Data layer, importers, exporters and QDPX: no widget dependencies, shared by app and tests.
qt_add_library(tessera_core STATIC
    src/core/Model.h
    src/core/Project.h src/core/Project.cpp
    src/core/Importers.h src/core/Importers.cpp
    src/core/Exporters.h src/core/Exporters.cpp
    src/core/Qdpx.h src/core/Qdpx.cpp
    src/core/TextUtils.h src/core/TextUtils.cpp
    src/core/Retrieval.h src/core/Retrieval.cpp
    src/core/Cooccurrence.h src/core/Cooccurrence.cpp
    src/core/Stats.h src/core/Stats.cpp
    src/core/VariableFilter.h src/core/VariableFilter.cpp
    src/core/DemoProject.h src/core/DemoProject.cpp
)
target_include_directories(tessera_core PUBLIC src)
target_compile_definitions(tessera_core PUBLIC TESSERA_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(tessera_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Sql
    PRIVATE Qt6::CorePrivate Qt6::Pdf)

option(TESSERA_BUILD_APP "Build the desktop application (off = core library and tests only)" ON)

if(TESSERA_BUILD_APP)
qt_add_library(tessera_ui STATIC
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/DocumentTree.h src/ui/DocumentTree.cpp
    src/ui/CodeTree.h src/ui/CodeTree.cpp
    src/ui/DocumentView.h src/ui/DocumentView.cpp
    src/ui/CodingStripe.h src/ui/CodingStripe.cpp
    src/ui/SegmentsPanel.h src/ui/SegmentsPanel.cpp
    src/ui/MemoDialog.h src/ui/MemoDialog.cpp
    src/ui/MemoManager.h src/ui/MemoManager.cpp
    src/ui/LinksDialog.h src/ui/LinksDialog.cpp
    src/ui/VariablesDialog.h src/ui/VariablesDialog.cpp
    src/ui/SearchDialog.h src/ui/SearchDialog.cpp
    src/ui/MatrixDialog.h src/ui/MatrixDialog.cpp
    src/ui/RetrievalDialog.h src/ui/RetrievalDialog.cpp
    src/ui/CooccurrenceDialog.h src/ui/CooccurrenceDialog.cpp
    src/ui/StatsDialog.h src/ui/StatsDialog.cpp
    src/ui/VariableFilterDialog.h src/ui/VariableFilterDialog.cpp
    src/ui/UiUtils.h src/ui/UiUtils.cpp
)
target_link_libraries(tessera_ui PUBLIC tessera_core Qt6::Widgets Qt6::Charts Qt6::Svg)

qt_add_executable(Tessera WIN32 src/main.cpp)
target_link_libraries(Tessera PRIVATE tessera_ui)
set_target_properties(Tessera PROPERTIES OUTPUT_NAME "Tessera")
endif()

enable_testing()
qt_add_executable(tessera_tests tests/test_core.cpp)
target_link_libraries(tessera_tests PRIVATE tessera_core Qt6::Test Qt6::CorePrivate)
add_test(NAME tessera_tests COMMAND tessera_tests)

if(TESSERA_BUILD_APP)
    qt_add_executable(tessera_ui_tests tests/test_ui.cpp)
    target_link_libraries(tessera_ui_tests PRIVATE tessera_ui Qt6::Test)
    add_test(NAME tessera_ui_tests COMMAND tessera_ui_tests -platform offscreen)
endif()
