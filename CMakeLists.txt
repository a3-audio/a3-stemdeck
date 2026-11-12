cmake_minimum_required(VERSION 3.22)

project(StemDeck VERSION 1.0.0)

# ccache, when it is installed (2026-10-03): a fresh worktree rebuilt all of
# JUCE and every source -- some twenty minutes on the rig. The base directory
# makes the paths of different worktrees compare equal, so one worktree's
# compile is another's cache hit.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    # The checkout is the base: seen from its build/ every source path is the
    # same in every worktree, and JUCE, outside it, stays absolute and equal.
    set(CCACHE_LAUNCHER "${CMAKE_COMMAND}" -E env "CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}"
        "CCACHE_NOHASHDIR=true"
        # What ccache needs to cache a compile that reads a precompiled header.
        "CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime"
        "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_LAUNCHER})
endif()

# Round as x86-64 does on every machine: GCC on aarch64 (a Raspberry Pi 5)
# fuses a*b+c by default, and IEM's StereoEncoder then lands a rounding step
# off its stored quaternion, so its listeners run -- and allocate -- on the
# audio thread (tools/tests/test_float_contraction.py).
add_compile_options(-ffp-contract=off)

find_package(JUCE CONFIG REQUIRED)

# Distro JUCE packages (e.g. Debian's "+ds" build) strip the bundled FLAC/Ogg
# codecs and expect the system libraries to be linked instead.
find_package(PkgConfig REQUIRED)
pkg_check_modules(AUDIO_CODECS REQUIRED IMPORTED_TARGET flac vorbisenc vorbisfile vorbis ogg)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
# Key lock: Rubber Band's real-time stretcher (librubberband-dev; at run time librubberband3 on
# forky, librubberband2 on trixie -- the package names it through shlibs).
pkg_check_modules(RUBBERBAND REQUIRED IMPORTED_TARGET rubberband)
# NI Stems (.stem.mp4): AAC or ALAC, which JUCE does not decode on Linux -- FFmpeg's libraries
# (libavformat-dev, libavcodec-dev; at run time the package names them through shlibs).
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

# The engine's IEM nodes (spatial audio), compiled into this binary by its own
# recipe from a pinned submodule. IEM is built against the same JUCE as the app.
set(STEMDECK_A3_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/a3-core
    CACHE PATH "a3-core checkout (submodule) whose IEM nodes StemDeck compiles")
if(NOT EXISTS ${STEMDECK_A3_CORE_DIR}/third_party/IEMPluginSuite/MultiEncoder/Source/PluginProcessor.cpp)
    message(FATAL_ERROR "third_party/a3-core is not initialised: git submodule update --init third_party/a3-core, "
        "then in it (not --recursive): git submodule update --init third_party/IEMPluginSuite")
endif()

# JUCE's way for module code outside an app: a library of its own that carries
# the modules and their definitions -- the one JUCE of the app, the IEM nodes
# and the spatial tests. The modules are PRIVATE: a juce:: target hands its
# module .cpp files to every target that links it publicly, so each consumer
# would compile its own copy of JUCE.
add_library(stemdeck-juce STATIC)
target_link_libraries(stemdeck-juce
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_cryptography
        juce::juce_osc
        juce::juce_dsp
        juce::juce_opengl
        juce::juce_recommended_config_flags
        PkgConfig::AUDIO_CODECS)
# JUCE_MODAL_LOOPS_PERMITTED is IEM's global definition; it changes module
# code, so it has to be on the library that compiles the modules.
target_compile_definitions(stemdeck-juce PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0 # JACK output is handled by JackOutput (fixed 8 ports)
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_compile_definitions(stemdeck-juce INTERFACE $<TARGET_PROPERTY:stemdeck-juce,COMPILE_DEFINITIONS>)
target_include_directories(stemdeck-juce INTERFACE $<TARGET_PROPERTY:stemdeck-juce,INCLUDE_DIRECTORIES>)

include(${STEMDECK_A3_CORE_DIR}/cmake/A3IemNodes.cmake)
include(${STEMDECK_A3_CORE_DIR}/third_party/IEMPluginSuite/Versions.cmake)   # only set()s the plug-in versions
a3_add_iem_nodes("-sd" stemdeck-juce)

juce_add_gui_app(StemDeck
    PRODUCT_NAME "StemDeck")

juce_generate_juce_header(StemDeck)

# A precompiled JuceHeader.h (2026-10-03): every source parsed all of JUCE's
# headers anew. JUCE's own module units are left out: they set JUCE's
# configuration before they include it, and a header put in front of them
# would come first.
get_target_property(juce_core_sources juce::juce_core INTERFACE_SOURCES)
list(GET juce_core_sources 0 juce_core_unit)
get_filename_component(juce_modules_dir "${juce_core_unit}" DIRECTORY)
get_filename_component(juce_modules_dir "${juce_modules_dir}" DIRECTORY)
file(GLOB juce_module_units
    "${juce_modules_dir}/*/*.cpp" "${juce_modules_dir}/*/*.c" "${juce_modules_dir}/*/*.mm")
set_source_files_properties(${juce_module_units} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
get_target_property(stemdeck_library_code StemDeck JUCE_GENERATED_SOURCES_DIRECTORY)
target_precompile_headers(StemDeck PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${stemdeck_library_code}/JuceHeader.h>")

target_sources(StemDeck PRIVATE
    Source/Main.cpp
    Source/LaunchOptions.cpp
    Source/DeckInstance.cpp
    Source/MainComponent.cpp
    Source/OscTruth.cpp
    Source/OscTruthFile.cpp
    Source/Remote.cpp
    Source/RemoteLink.cpp
    Source/DeckLink.cpp
    Source/RemotePointer.cpp
    Source/PointerInjector.cpp
    Source/TruthKeeperLink.cpp
    Source/Theme.cpp
    Source/StemSet.cpp
    Source/NiStems.cpp
    Source/NiStemInfo.cpp
    Source/NiStemReader.cpp
    Source/StemReader.cpp
    Source/StemDeckPlayer.cpp
    Source/KeyLock.cpp
    Source/KeyLockStretcher.cpp
    Source/Waveforms.cpp
    Source/TitleLayout.cpp
    Source/EventMarkerLayout.cpp
    Source/EventMarkerView.cpp
    Source/EventStrip.cpp
    Source/DeckPanel.cpp
    Source/JogWheel.cpp
    Source/TempoAnalysis.cpp
    Source/Sections.cpp
    Source/Preview.cpp
    Source/CueLight.cpp
    Source/CuePlay.cpp
    Source/DeckLoop.cpp
    Source/SectionAnalysis.cpp
    Source/BarPhase.cpp
    Source/CachedGrid.cpp
    Source/MixerPanel.cpp
    Source/StemLibrary.cpp
    Source/SettingsPanel.cpp
    Source/FolderChoice.cpp
    Source/FolderStep.cpp
    Source/AudioPanel.cpp
    Source/Workspaces.cpp
    Source/SpawnOutput.cpp
    Source/KeyboardPolicy.cpp
    Source/OnScreenKeyboard.cpp
    Source/OnScreenKeys.cpp
    Source/WorkspacePanel.cpp
    Source/WorkspaceSwitch.cpp
    Source/JackOutput.cpp
    Source/ProLinkPackets.cpp
    Source/PioneerClock.cpp
    Source/FollowLeader.cpp
    Source/DeckFollow.cpp
    Source/OneDeckSync.cpp
    Source/ProLinkAnnouncer.cpp
    Source/ProLinkTempoMaster.cpp
    Source/ProLinkReceiver.cpp
    Source/LinkInterface.cpp
    Source/MasterDeck.cpp
    Source/SyncLabels.cpp
    Source/ProLinkSender.cpp
    Source/BeatScheduler.cpp
    Source/LibraryPath.cpp
    Source/StemNames.cpp
    Source/StemJob.cpp
    Source/StemCreator.cpp
    Source/StemCreatorLock.cpp
    Source/FileLock.cpp
    Source/ProcessGroup.cpp
    Source/MeterBallistics.cpp
    Source/Session.cpp
    Source/GridEdit.cpp
    Source/AutoDj.cpp
    Source/MotionLite.cpp
    Source/StemEvents.cpp
    Source/StemEventsFile.cpp
    Source/EventRecorder.cpp
    Source/TrackEvents.cpp
    Source/EventScheduler.cpp
    Source/EventBank.cpp
    Source/StemHandover.cpp
    Source/StemLevels.cpp
    Source/Recorder.cpp
    Source/RecordTarget.cpp
    Source/DataPaths.cpp
    Source/Scs3d.cpp
    Source/Scs3dDevice.cpp
    Source/SurfaceLayout.cpp
    Source/LibraryFolders.cpp
    Source/Tips.cpp
    Source/PanelProtocol.cpp
    Source/PanelMap.cpp
    Source/PanelMapOneDeck.cpp
    Source/PlayMode.cpp
    Source/AuxPots.cpp
    Source/PanelOwnership.cpp
    Source/PanelFollowsWorkspace.cpp
    Source/PanelWorkspacePoller.cpp
    Source/PanelLink.cpp
    Source/PanelDevice.cpp
    Source/SpatialLaw.cpp
    Source/SteadyBed.cpp
    Source/DeckSpatial.cpp
    Source/DeckSpatialPorts.cpp
    Source/DeckHost.cpp
    Source/OneDeckAudio.cpp
    Source/AppSettings.cpp
    Source/AudioOutput.cpp
    Source/RecorderControls.cpp
    Source/StemCreatorLink.cpp
    Source/CoreTruth.cpp
    Source/OneDeckComponent.cpp
    Source/StemSpatialPopup.cpp
    Source/OneShotKeys.cpp
    Source/OrbitSwipe.cpp
    Source/StemColumn.cpp
    Source/OneDeckView.cpp
    Source/LevelMeter.cpp
    Source/DeckMotion.cpp
    Source/StemViewer.cpp
    third_party/a3-core/src/a3-engine/Laws.cpp
    third_party/a3-core/src/a3-engine/PurestGain.cpp
    Source/DeckLinks.cpp
    third_party/a3-core/src/a3-links/LinkMap.cpp
    third_party/a3-core/src/a3-links/LinkPlan.cpp
    third_party/a3-core/src/a3-links/JackLinker.cpp)
target_include_directories(StemDeck PRIVATE third_party/a3-core/src)

target_compile_definitions(StemDeck PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:StemDeck,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:StemDeck,JUCE_VERSION>")

target_link_libraries(StemDeck
    PRIVATE
        stemdeck-juce
        a3-iem-sd
        PkgConfig::AUDIO_CODECS
        PkgConfig::JACK
        PkgConfig::RUBBERBAND
        PkgConfig::LIBAV
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(STEMDECK_TESTS "Build the unit tests" OFF)
if(STEMDECK_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tools/downbeat-eval)
    add_subdirectory(tools/spatial-timing)
    add_subdirectory(tools/keylock-bench)
endif()
