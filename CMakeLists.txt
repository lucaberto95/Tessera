cmake_minimum_required(VERSION 3.21)
project(Tessera VERSION 0.29.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core CorePrivate Gui Widgets Sql Pdf Charts Svg Concurrent Network Test)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
endif()

# Data layer, importers, exporters and QDPX: no widget dependencies, shared by app and tests.
qt_add_library(tessera_core STATIC
    src/core/Model.h
    src/core/Project.h src/core/Project.cpp
    src/core/Importers.h src/core/Importers.cpp
    src/core/Images.h src/core/Images.cpp
    src/core/Pdfs.h src/core/Pdfs.cpp
    src/core/Exporters.h src/core/Exporters.cpp
    src/core/Qdpx.h src/core/Qdpx.cpp
    src/core/TextUtils.h src/core/TextUtils.cpp
    src/core/RichText.h src/core/RichText.cpp
    src/core/Retrieval.h src/core/Retrieval.cpp
    src/core/Agreement.h src/core/Agreement.cpp
    src/core/Cooccurrence.h src/core/Cooccurrence.cpp
    src/core/Stats.h src/core/Stats.cpp
    src/core/Summaries.h src/core/Summaries.cpp
    src/core/Report.h src/core/Report.cpp
    src/core/Merge.h src/core/Merge.cpp
    src/core/VariableFilter.h src/core/VariableFilter.cpp
    src/core/Xlsx.h src/core/Xlsx.cpp
    src/core/Survey.h src/core/Survey.cpp
    src/core/Dictionaries.h src/core/Dictionaries.cpp
    src/core/WordFrequencies.h src/core/WordFrequencies.cpp
    src/core/DemoProject.h src/core/DemoProject.cpp
    src/core/Updater.h src/core/Updater.cpp
    src/core/ai/AiConfig.h src/core/ai/AiConfig.cpp
    src/core/ai/AiClient.h src/core/ai/AiClient.cpp
    src/core/ai/AiAssist.h src/core/ai/AiAssist.cpp
    src/core/ai/AiCodes.h src/core/ai/AiCodes.cpp
)
# Prompts of the AI functions (issue #40), embedded so tests and the app find them anywhere.
qt_add_resources(tessera_core ai_prompts PREFIX "/ai" BASE "${CMAKE_SOURCE_DIR}/resources/ai" FILES
    resources/ai/summarize_document.txt
    resources/ai/summarize_segments.txt
    resources/ai/explain.txt
    resources/ai/translate.txt
    resources/ai/combine.txt
    resources/ai/suggest_codes.txt
    resources/ai/suggest_subcodes.txt
    resources/ai/group_codes.txt
)
target_include_directories(tessera_core PUBLIC src)
target_compile_definitions(tessera_core PUBLIC TESSERA_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(tessera_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Pdf Qt6::Network
    PRIVATE Qt6::CorePrivate)
if(WIN32)
    target_link_libraries(tessera_core PRIVATE advapi32) # Credential Manager (API keys)
endif()

option(TESSERA_BUILD_APP "Build the desktop application (off = core library and tests only)" ON)

if(TESSERA_BUILD_APP)
qt_add_library(tessera_ui STATIC
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/DocumentTree.h src/ui/DocumentTree.cpp
    src/ui/CodeTree.h src/ui/CodeTree.cpp
    src/ui/DocumentView.h src/ui/DocumentView.cpp
    src/ui/CodingStripe.h src/ui/CodingStripe.cpp
    src/ui/ImageView.h src/ui/ImageView.cpp
    src/ui/PdfView.h src/ui/PdfView.cpp
    src/ui/SegmentsPanel.h src/ui/SegmentsPanel.cpp
    src/ui/MemoDialog.h src/ui/MemoDialog.cpp
    src/ui/MemoManager.h src/ui/MemoManager.cpp
    src/ui/LinksDialog.h src/ui/LinksDialog.cpp
    src/ui/VariablesDialog.h src/ui/VariablesDialog.cpp
    src/ui/SearchDialog.h src/ui/SearchDialog.cpp
    src/ui/MatrixDialog.h src/ui/MatrixDialog.cpp
    src/ui/RetrievalDialog.h src/ui/RetrievalDialog.cpp
    src/ui/AgreementDialog.h src/ui/AgreementDialog.cpp
    src/ui/CooccurrenceDialog.h src/ui/CooccurrenceDialog.cpp
    src/ui/StatsDialog.h src/ui/StatsDialog.cpp
    src/ui/SummaryGridDialog.h src/ui/SummaryGridDialog.cpp
    src/ui/SummaryTablesDialog.h src/ui/SummaryTablesDialog.cpp
    src/ui/ReportDialog.h src/ui/ReportDialog.cpp
    src/ui/MergeDialog.h src/ui/MergeDialog.cpp
    src/ui/VariableFilterDialog.h src/ui/VariableFilterDialog.cpp
    src/ui/SurveyImportDialog.h src/ui/SurveyImportDialog.cpp
    src/ui/DictionaryDialog.h src/ui/DictionaryDialog.cpp
    src/ui/WordFrequencyDialog.h src/ui/WordFrequencyDialog.cpp
    src/ui/AiSettingsDialog.h src/ui/AiSettingsDialog.cpp
    src/ui/AiAssistDialog.h src/ui/AiAssistDialog.cpp
    src/ui/AiCodesDialog.h src/ui/AiCodesDialog.cpp
    src/ui/AiUi.h src/ui/AiUi.cpp
    src/ui/UiUtils.h src/ui/UiUtils.cpp
)
target_link_libraries(tessera_ui PUBLIC tessera_core Qt6::Widgets Qt6::Charts Qt6::Svg Qt6::Concurrent)

qt_add_executable(Tessera WIN32 src/main.cpp src/tessera.rc)
qt_add_resources(Tessera icons PREFIX "/" BASE "${CMAKE_SOURCE_DIR}/installer" FILES installer/tessera.svg)

# Regenerates installer/*.ico from the SVG sources (not built by default).
qt_add_executable(tessera_makeicon EXCLUDE_FROM_ALL tools/makeicon.cpp)
target_link_libraries(tessera_makeicon PRIVATE Qt6::Gui Qt6::Svg)
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
    target_compile_definitions(tessera_ui_tests PRIVATE TESSERA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME tessera_ui_tests COMMAND tessera_ui_tests -platform offscreen)
endif()
