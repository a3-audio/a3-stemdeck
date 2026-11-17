# Analyses a whole stem library the way StemDeck does when a set is loaded
# (tempo grid, the bar's one, section features) and keeps the results in an
# analysis cache. Built with the tests, like the other tools.
#   stemdeck-analyse [--cache FILE] [--dry-run] [--only NAME] <stems folder>
juce_add_console_app(stemdeck-analyse PRODUCT_NAME "stemdeck-analyse")
juce_generate_juce_header(stemdeck-analyse)
set_target_properties(stemdeck-analyse PROPERTIES EXCLUDE_FROM_ALL ON)

target_sources(stemdeck-analyse PRIVATE
    Main.cpp
    ${CMAKE_SOURCE_DIR}/Source/TempoAnalysis.cpp
    ${CMAKE_SOURCE_DIR}/Source/SectionAnalysis.cpp
    ${CMAKE_SOURCE_DIR}/Source/Sections.cpp
    ${CMAKE_SOURCE_DIR}/Source/FileLock.cpp
    ${CMAKE_SOURCE_DIR}/Source/BarPhase.cpp
    ${CMAKE_SOURCE_DIR}/Source/CachedGrid.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemSet.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemReader.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStemReader.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStemInfo.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStems.cpp
    ${CMAKE_SOURCE_DIR}/Source/LibraryPath.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemNames.cpp)
target_include_directories(stemdeck-analyse PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_definitions(stemdeck-analyse PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(stemdeck-analyse
    PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
        PkgConfig::AUDIO_CODECS
        PkgConfig::LIBAV
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
