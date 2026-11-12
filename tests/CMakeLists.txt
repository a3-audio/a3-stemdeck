find_package(GTest REQUIRED)
add_executable(stemdeck-tests
    Smoke.cpp
    PlayMode.cpp
    ${CMAKE_SOURCE_DIR}/Source/PlayMode.cpp
    CueLight.cpp
    ${CMAKE_SOURCE_DIR}/Source/CueLight.cpp
    CuePlay.cpp
    ${CMAKE_SOURCE_DIR}/Source/CuePlay.cpp
    DeckLoop.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckLoop.cpp
    WaveformScale.cpp
    WaveformColumns.cpp
    OverviewGesture.cpp
    BarPhase.cpp
    ${CMAKE_SOURCE_DIR}/Source/BarPhase.cpp
    CachedGrid.cpp
    ${CMAKE_SOURCE_DIR}/Source/CachedGrid.cpp
    Buses.cpp
    Outputs.cpp
    GridEdit.cpp
    AutoDj.cpp
    MotionLite.cpp
    ${CMAKE_SOURCE_DIR}/Source/MotionLite.cpp
    OrbitSwipe.cpp
    ${CMAKE_SOURCE_DIR}/Source/OrbitSwipe.cpp
    StemEvents.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemEvents.cpp
    StemEventsFile.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemEventsFile.cpp
    EventRecorder.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventRecorder.cpp
    TrackEvents.cpp
    ${CMAKE_SOURCE_DIR}/Source/TrackEvents.cpp
    EventScheduler.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventScheduler.cpp
    EventBank.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventBank.cpp
    TitleLayout.cpp
    ${CMAKE_SOURCE_DIR}/Source/TitleLayout.cpp
    EventMarkerLayout.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventMarkerLayout.cpp
    ThumbnailLoad.cpp
    StemHandover.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemHandover.cpp
    RecordingName.cpp
    RecordTarget.cpp
    ${CMAKE_SOURCE_DIR}/Source/RecordTarget.cpp
    Scs3d.cpp
    ${CMAKE_SOURCE_DIR}/Source/Scs3d.cpp
    ${CMAKE_SOURCE_DIR}/Source/AutoDj.cpp
    ${CMAKE_SOURCE_DIR}/Source/GridEdit.cpp
    ProLinkPackets.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkPackets.cpp
    ProLinkAnnouncer.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkAnnouncer.cpp
    ProLinkPresence.cpp
    ProLinkTempoMaster.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkTempoMaster.cpp
    OscTruth.cpp
    ${CMAKE_SOURCE_DIR}/Source/OscTruth.cpp
    Remote.cpp
    RemotePointer.cpp
    ${CMAKE_SOURCE_DIR}/Source/RemotePointer.cpp
    ${CMAKE_SOURCE_DIR}/Source/OscTruthFile.cpp
    TruthKeeper.cpp
    ${CMAKE_SOURCE_DIR}/Source/Remote.cpp
    PioneerClock.cpp
    ${CMAKE_SOURCE_DIR}/Source/PioneerClock.cpp
    FollowLeader.cpp
    ${CMAKE_SOURCE_DIR}/Source/FollowLeader.cpp
    MasterDeck.cpp
    ${CMAKE_SOURCE_DIR}/Source/MasterDeck.cpp
    SyncLabels.cpp
    ${CMAKE_SOURCE_DIR}/Source/SyncLabels.cpp
    BeatScheduler.cpp
    ${CMAKE_SOURCE_DIR}/Source/BeatScheduler.cpp
    LibraryPath.cpp
    ${CMAKE_SOURCE_DIR}/Source/LibraryPath.cpp
    StemNames.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemNames.cpp
    StemJob.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemJob.cpp
    ProcessGroup.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProcessGroup.cpp
    JackFormat.cpp
    MeterBallistics.cpp
    ${CMAKE_SOURCE_DIR}/Source/MeterBallistics.cpp
    Workspaces.cpp
    ${CMAKE_SOURCE_DIR}/Source/Workspaces.cpp
    SpawnOutput.cpp
    ${CMAKE_SOURCE_DIR}/Source/SpawnOutput.cpp
    KeyboardPolicy.cpp
    ${CMAKE_SOURCE_DIR}/Source/KeyboardPolicy.cpp
    FolderChoice.cpp
    ${CMAKE_SOURCE_DIR}/Source/FolderChoice.cpp
    FolderStep.cpp
    ${CMAKE_SOURCE_DIR}/Source/FolderStep.cpp
    DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/Source/DataPaths.cpp
    NiStems.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStems.cpp
    NiStemInfo.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStemInfo.cpp
    SectionFeatures.cpp
    Sections.cpp
    Preview.cpp
    ${CMAKE_SOURCE_DIR}/Source/Preview.cpp
    ${CMAKE_SOURCE_DIR}/Source/Sections.cpp
    SurfaceLayout.cpp
    ${CMAKE_SOURCE_DIR}/Source/SurfaceLayout.cpp
    LibraryFolders.cpp
    Tips.cpp
    PanelProtocol.cpp
    ${CMAKE_SOURCE_DIR}/Source/PanelProtocol.cpp
    PanelMap.cpp
    ${CMAKE_SOURCE_DIR}/Source/PanelMap.cpp
    PanelLink.cpp
    ${CMAKE_SOURCE_DIR}/Source/PanelLink.cpp
    DoublePress.cpp
    DeleteArm.cpp
    PanelMapOneDeck.cpp
    ${CMAKE_SOURCE_DIR}/Source/PanelMapOneDeck.cpp
    PanelOwnership.cpp
    PanelFollowsWorkspace.cpp
    ${CMAKE_SOURCE_DIR}/Source/PanelFollowsWorkspace.cpp
    AppSettings.cpp
    ${CMAKE_SOURCE_DIR}/Source/AppSettings.cpp
    ${CMAKE_SOURCE_DIR}/Source/PanelOwnership.cpp
    ${CMAKE_SOURCE_DIR}/Source/Tips.cpp
    ${CMAKE_SOURCE_DIR}/Source/LibraryFolders.cpp
    KeyLock.cpp
    ${CMAKE_SOURCE_DIR}/Source/KeyLock.cpp
    KeyLockStretcher.cpp
    ${CMAKE_SOURCE_DIR}/Source/KeyLockStretcher.cpp
    Session.cpp
    ${CMAKE_SOURCE_DIR}/Source/Session.cpp
    LaunchOptions.cpp
    ${CMAKE_SOURCE_DIR}/Source/LaunchOptions.cpp
    LinkInterface.cpp
    ${CMAKE_SOURCE_DIR}/Source/LinkInterface.cpp
    DeckInstance.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckInstance.cpp
    SpatialLaw.cpp
    ${CMAKE_SOURCE_DIR}/Source/SpatialLaw.cpp
    ${CMAKE_SOURCE_DIR}/third_party/a3-core/src/a3-engine/Laws.cpp
    ${CMAKE_SOURCE_DIR}/third_party/a3-core/src/a3-engine/PurestGain.cpp
    DeckLinks.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckLinks.cpp
    ${CMAKE_SOURCE_DIR}/third_party/a3-core/src/a3-links/LinkMap.cpp
    ${CMAKE_SOURCE_DIR}/third_party/a3-core/src/a3-links/LinkPlan.cpp)
target_include_directories(stemdeck-tests PRIVATE ${CMAKE_SOURCE_DIR}/Source ${CMAKE_SOURCE_DIR}/third_party/a3-core/src)
target_compile_definitions(stemdeck-tests PRIVATE STEMDECK_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# The session is XML: juce_core, and nothing else of JUCE, comes into the
# tests -- as a library of its own, JUCE's way for module code outside an app.
add_library(stemdeck-tests-juce STATIC)
target_link_libraries(stemdeck-tests-juce
    PRIVATE juce::juce_core juce::juce_data_structures
    PUBLIC juce::juce_recommended_config_flags)
target_compile_definitions(stemdeck-tests-juce PUBLIC JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=0)
target_compile_definitions(stemdeck-tests-juce INTERFACE $<TARGET_PROPERTY:stemdeck-tests-juce,COMPILE_DEFINITIONS>)
target_include_directories(stemdeck-tests-juce INTERFACE $<TARGET_PROPERTY:stemdeck-tests-juce,INCLUDE_DIRECTORIES>)

# Debian keeps upstream file times, so make cannot see that libgtest-dev was
# upgraded and links old test objects against the new library (stemdeck#1).
# The stamp header holds a hash of the library and gtest.h; every test object
# includes it, so a different googletest rebuilds them. It is checked on every
# build, which costs a hash, not a rebuild.
get_target_property(gtest_include GTest::gtest INTERFACE_INCLUDE_DIRECTORIES)
set(gtest_stamp_header ${CMAKE_CURRENT_BINARY_DIR}/gtest_stamp.h)
add_custom_target(stemdeck-gtest-stamp
    COMMAND ${CMAKE_COMMAND} -DOUT=${gtest_stamp_header}
            "-DFILES=$<TARGET_FILE:GTest::gtest>$<SEMICOLON>${gtest_include}/gtest/gtest.h"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gtest-stamp.cmake
    BYPRODUCTS ${gtest_stamp_header}
    VERBATIM)
add_dependencies(stemdeck-tests stemdeck-gtest-stamp)
target_compile_options(stemdeck-tests PRIVATE -include ${gtest_stamp_header})
target_compile_features(stemdeck-tests PRIVATE cxx_std_17)
target_link_libraries(stemdeck-tests PRIVATE GTest::gtest GTest::gtest_main PkgConfig::RUBBERBAND stemdeck-tests-juce)
include(GoogleTest)
gtest_discover_tests(stemdeck-tests)

# The injector hands a touch to a real window, so these tests need JUCE's GUI
# and an X server. Without a DISPLAY ctest starts them under xvfb-run (never
# by unsetting DISPLAY: JUCE then falls back to :0.0, the rig's screen).
add_executable(stemdeck-gui-tests
    PointerInjector.cpp
    StemSpatialPopup.cpp
    DeckMotion.cpp
    WorkspaceSwitch.cpp
    ${CMAKE_SOURCE_DIR}/Source/WorkspaceSwitch.cpp
    ${CMAKE_SOURCE_DIR}/Source/WorkspacePanel.cpp
    ${CMAKE_SOURCE_DIR}/Source/Workspaces.cpp
    ${CMAKE_SOURCE_DIR}/Source/SpawnOutput.cpp
    StemViewer.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemSpatialPopup.cpp
    ${CMAKE_SOURCE_DIR}/Source/OneShotKeys.cpp
    ${CMAKE_SOURCE_DIR}/Source/OrbitSwipe.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemViewer.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckMotion.cpp
    ${CMAKE_SOURCE_DIR}/Source/MotionLite.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventScheduler.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventBank.cpp
    ${CMAKE_SOURCE_DIR}/Source/PointerInjector.cpp
    ${CMAKE_SOURCE_DIR}/Source/RemotePointer.cpp)
target_include_directories(stemdeck-gui-tests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
target_compile_features(stemdeck-gui-tests PRIVATE cxx_std_17)
target_compile_definitions(stemdeck-gui-tests PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(stemdeck-gui-tests PRIVATE GTest::gtest GTest::gtest_main
    juce::juce_gui_basics juce::juce_audio_basics juce::juce_recommended_config_flags)
add_dependencies(stemdeck-gui-tests stemdeck-gtest-stamp)
target_compile_options(stemdeck-gui-tests PRIVATE -include ${gtest_stamp_header})

# Always under xvfb-run when it exists, even with a DISPLAY set: on the rig
# that would be the live screen, and the test opens a window.
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    set_target_properties(stemdeck-gui-tests PROPERTIES CROSSCOMPILING_EMULATOR "${XVFB_RUN};-a")
endif()
gtest_discover_tests(stemdeck-gui-tests)

# The engine's IEM nodes, compiled into StemDeck's own JUCE (stemdeck-juce).
# One deck's spatial engine beside them; the engine's SpatialConfig reads the
# engine's own bed files for the SteadyBed reference.
add_executable(stemdeck-spatial-tests
    spatial/IemInStemDeck.cpp
    spatial/SteadyBed.cpp
    spatial/DeckSpatial.cpp
    spatial/DeckSpatialAllocation.cpp
    spatial/DeckSpatialTiming.cpp
    spatial/DeckSpatialPorts.cpp
    spatial/DeckSpatialMotion.cpp
    spatial/DeckHost.cpp
    spatial/OneDeck.cpp
    spatial/OneDeckSync.cpp
    spatial/PlayerTempo.cpp
    spatial/PlayerLoop.cpp
    spatial/PlayerContinue.cpp
    spatial/PlayerKeyLockEngine.cpp
    spatial/StemCreatorLock.cpp
    spatial/AnalysisCache.cpp
    spatial/Recorder.cpp
    spatial/RecorderControls.cpp
    spatial/EmptySlots.cpp
    spatial/WaveformTaps.cpp
    spatial/EventStrip.cpp
    spatial/NiStemReader.cpp
    spatial/OnScreenKeys.cpp
    ${CMAKE_SOURCE_DIR}/Source/OnScreenKeys.cpp
    ${CMAKE_SOURCE_DIR}/Source/OnScreenKeyboard.cpp
    ${CMAKE_SOURCE_DIR}/Source/KeyboardPolicy.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemLibrary.cpp
    ${CMAKE_SOURCE_DIR}/Source/FolderStep.cpp
    ${CMAKE_SOURCE_DIR}/Source/LibraryFolders.cpp
    ${CMAKE_SOURCE_DIR}/Source/Recorder.cpp
    ${CMAKE_SOURCE_DIR}/Source/RecorderControls.cpp
    ${CMAKE_SOURCE_DIR}/Source/RecordTarget.cpp
    ${CMAKE_SOURCE_DIR}/Source/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/Source/SteadyBed.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckSpatial.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckSpatialPorts.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckHost.cpp
    ${CMAKE_SOURCE_DIR}/Source/TrackEvents.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventRecorder.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemEvents.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemEventsFile.cpp
    ${CMAKE_SOURCE_DIR}/Source/OneDeckAudio.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemCreator.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemCreatorLock.cpp
    ${CMAKE_SOURCE_DIR}/Source/FileLock.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemJob.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProcessGroup.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemNames.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemSet.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStems.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStemInfo.cpp
    ${CMAKE_SOURCE_DIR}/Source/NiStemReader.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemReader.cpp
    ${CMAKE_SOURCE_DIR}/Source/LibraryPath.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemLevels.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemHandover.cpp
    ${CMAKE_SOURCE_DIR}/Source/TempoAnalysis.cpp
    ${CMAKE_SOURCE_DIR}/Source/SectionAnalysis.cpp
    ${CMAKE_SOURCE_DIR}/Source/Sections.cpp
    ${CMAKE_SOURCE_DIR}/Source/BarPhase.cpp
    ${CMAKE_SOURCE_DIR}/Source/CachedGrid.cpp
    ${CMAKE_SOURCE_DIR}/Source/FollowLeader.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckFollow.cpp
    ${CMAKE_SOURCE_DIR}/Source/OneDeckSync.cpp
    ${CMAKE_SOURCE_DIR}/Source/MasterDeck.cpp
    ${CMAKE_SOURCE_DIR}/Source/PioneerClock.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkPackets.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkAnnouncer.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkTempoMaster.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkReceiver.cpp
    ${CMAKE_SOURCE_DIR}/Source/LinkInterface.cpp
    ${CMAKE_SOURCE_DIR}/Source/ProLinkSender.cpp
    ${CMAKE_SOURCE_DIR}/Source/BeatScheduler.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckInstance.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckMotion.cpp
    ${CMAKE_SOURCE_DIR}/Source/MotionLite.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventScheduler.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventBank.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemDeckPlayer.cpp
    ${CMAKE_SOURCE_DIR}/Source/KeyLockStretcher.cpp
    ${CMAKE_SOURCE_DIR}/Source/KeyLock.cpp
    ${CMAKE_SOURCE_DIR}/Source/Remote.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckLink.cpp
    ${CMAKE_SOURCE_DIR}/Source/OscTruthFile.cpp
    ${CMAKE_SOURCE_DIR}/Source/OscTruth.cpp
    ${CMAKE_SOURCE_DIR}/Source/GridEdit.cpp
    ${CMAKE_SOURCE_DIR}/Source/SpatialLaw.cpp
    ${CMAKE_SOURCE_DIR}/Source/OneDeckView.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemColumn.cpp
    ${CMAKE_SOURCE_DIR}/Source/AuxPots.cpp
    ${CMAKE_SOURCE_DIR}/Source/OneShotKeys.cpp
    ${CMAKE_SOURCE_DIR}/Source/OrbitSwipe.cpp
    ${CMAKE_SOURCE_DIR}/Source/StemViewer.cpp
    ${CMAKE_SOURCE_DIR}/Source/LevelMeter.cpp
    ${CMAKE_SOURCE_DIR}/Source/MeterBallistics.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckPanel.cpp
    ${CMAKE_SOURCE_DIR}/Source/PlayMode.cpp
    ${CMAKE_SOURCE_DIR}/Source/Waveforms.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventMarkerLayout.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventMarkerView.cpp
    ${CMAKE_SOURCE_DIR}/Source/EventStrip.cpp
    ${CMAKE_SOURCE_DIR}/Source/JogWheel.cpp
    ${CMAKE_SOURCE_DIR}/Source/CueLight.cpp
    ${CMAKE_SOURCE_DIR}/Source/CuePlay.cpp
    ${CMAKE_SOURCE_DIR}/Source/DeckLoop.cpp
    ${CMAKE_SOURCE_DIR}/Source/SyncLabels.cpp
    ${CMAKE_SOURCE_DIR}/Source/SurfaceLayout.cpp
    ${CMAKE_SOURCE_DIR}/Source/Theme.cpp
    ${CMAKE_SOURCE_DIR}/third_party/a3-core/src/a3-engine/Laws.cpp
    ${CMAKE_SOURCE_DIR}/third_party/a3-core/src/a3-engine/PurestGain.cpp
    ${CMAKE_SOURCE_DIR}/third_party/a3-core/src/a3-engine/SpatialConfig.cpp)
target_include_directories(stemdeck-spatial-tests PRIVATE ${CMAKE_SOURCE_DIR}/Source ${CMAKE_SOURCE_DIR}/third_party/a3-core/src)
target_compile_definitions(stemdeck-spatial-tests PRIVATE
    STEMDECK_SPATIAL_DATA="${CMAKE_SOURCE_DIR}/third_party/a3-core/data/spatial")
target_compile_features(stemdeck-spatial-tests PRIVATE cxx_std_17)
target_link_libraries(stemdeck-spatial-tests PRIVATE GTest::gtest GTest::gtest_main a3-iem-sd PkgConfig::RUBBERBAND PkgConfig::LIBAV)
# The player (Stage S2) includes the app's JuceHeader.h: StemDeck generates it.
get_target_property(stemdeck_juce_header_dir StemDeck JUCE_GENERATED_SOURCES_DIRECTORY)
target_include_directories(stemdeck-spatial-tests PRIVATE ${stemdeck_juce_header_dir})
add_dependencies(stemdeck-spatial-tests StemDeck)
add_dependencies(stemdeck-spatial-tests stemdeck-gtest-stamp)
target_compile_options(stemdeck-spatial-tests PRIVATE -include ${gtest_stamp_header})
# The one-deck screen's tests (S4.4b) build components: under xvfb-run too,
# never on the display the rig's screen is.
if(XVFB_RUN)
    set_target_properties(stemdeck-spatial-tests PROPERTIES CROSSCOMPILING_EMULATOR "${XVFB_RUN};-a")
endif()
# The link's loopback socket tests share their ports between the two
# "instances" in one test: one at a time within a ctest run.
gtest_discover_tests(stemdeck-spatial-tests TEST_FILTER "-ProLinkOnOneHost.*:DeckLinkOnOneHost.*")
gtest_discover_tests(stemdeck-spatial-tests TEST_FILTER "ProLinkOnOneHost.*"
    PROPERTIES RESOURCE_LOCK stemdeck-prolink-loopback)
gtest_discover_tests(stemdeck-spatial-tests TEST_FILTER "DeckLinkOnOneHost.*"
    PROPERTIES RESOURCE_LOCK stemdeck-decklink-loopback)
