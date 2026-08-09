cmake_minimum_required(VERSION 3.24)

project(SappKeys VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SAPPKEYS_BUILD_PLUGIN "Build JUCE plugin + standalone targets" ON)
option(SAPPKEYS_BUILD_TESTS  "Build unit tests"                       ON)
option(SAPPKEYS_BUILD_CLI    "Build the sappkeys agent CLI"           ON)

include(cmake/CompilerWarnings.cmake)
include(FetchContent)

# ------------------------------------------------------------- SappSounds ---
# Local development uses the sibling checkout; CI or isolated builds may set
# SAPPSOUNDS_DIR or fall back to FetchContent from the public repo.
set(SAPPSOUNDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sappsounds" CACHE PATH
    "Path to a local SappSounds checkout (sibling directory by default)")

if(EXISTS "${SAPPSOUNDS_DIR}/CMakeLists.txt")
  add_subdirectory("${SAPPSOUNDS_DIR}" "${CMAKE_BINARY_DIR}/SappSounds")
else()
  FetchContent_Declare(SappSounds
    GIT_REPOSITORY https://github.com/localteampartners/sappsounds.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(SappSounds)
endif()

# ------------------------------------------------------------------- core ---
# Keys policy on top of Sapp::Sounds. Framework-independent (no JUCE):
# consumed by the plugin, the CLI, and the tests.
add_library(sappkeys_core STATIC
  src/core/KeysEngine.cpp
  src/core/KeysInstrument.cpp
  src/core/KeysRender.cpp
  src/core/SappLinkCCMap.cpp
)
target_include_directories(sappkeys_core PUBLIC src)
target_link_libraries(sappkeys_core PUBLIC Sapp::Sounds)
sappkeys_set_warnings(sappkeys_core)

# -------------------------------------------------------------------- cli ---
if(SAPPKEYS_BUILD_CLI)
  add_executable(sappkeys-cli src/cli/main.cpp)
  set_target_properties(sappkeys-cli PROPERTIES OUTPUT_NAME sappkeys)
  target_link_libraries(sappkeys-cli PRIVATE sappkeys_core)
  sappkeys_set_warnings(sappkeys-cli)
endif()

# ------------------------------------------------------------------ tests ---
if(SAPPKEYS_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(SappKeysTests
    tests/unit/test_instrument_state.cpp
    tests/unit/test_keys_engine.cpp
    tests/unit/test_resonance.cpp
    tests/unit/test_room.cpp
    tests/unit/test_render.cpp
    tests/unit/test_safety.cpp
    tests/unit/test_sapplink.cpp
  )
  target_link_libraries(SappKeysTests PRIVATE sappkeys_core Catch2::Catch2WithMain)
  target_compile_definitions(SappKeysTests PRIVATE
    SAPPKEYS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  sappkeys_set_warnings(SappKeysTests)

  enable_testing()
  add_test(NAME unit COMMAND SappKeysTests)
endif()

# ----------------------------------------------------------------- plugin ---
if(SAPPKEYS_BUILD_PLUGIN)
  # Same pinned JUCE tag as SappSynth / SappOrchestra. Local builds reuse the
  # SappSynth checkout: configure with
  #   -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/apps/sappsynth/build/_deps/juce-src
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(JUCE)

  juce_add_plugin(SappKeysPlugin
    PRODUCT_NAME                "SappKeys"
    COMPANY_NAME                "LocalTeamPartners"
    BUNDLE_ID                   com.localteampartners.sappkeys
    PLUGIN_MANUFACTURER_CODE    Ltpr
    PLUGIN_CODE                 Skys
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    FORMATS                     Standalone VST3 AU
  )
  if(APPLE)
    juce_enable_copy_plugin_step(SappKeysPlugin)
  endif()

  target_sources(SappKeysPlugin PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/plugin/FactoryPresets.cpp
    src/plugin/UserPresets.cpp
    src/plugin/SoundsPanel.cpp
  )
  target_link_libraries(SappKeysPlugin PRIVATE
    sappkeys_core
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
  )
  target_compile_definitions(SappKeysPlugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
  )

  if(APPLE)
    # Offscreen editor snapshot tool (UI verification without screen capture).
    juce_add_gui_app(SappKeysUiShot PRODUCT_NAME "SappKeysUiShot")
    target_sources(SappKeysUiShot PRIVATE
      tools/uishot/main.cpp
      src/plugin/PluginProcessor.cpp
      src/plugin/PluginEditor.cpp
      src/plugin/FactoryPresets.cpp
      src/plugin/UserPresets.cpp
      src/plugin/SoundsPanel.cpp
    )
    target_include_directories(SappKeysUiShot PRIVATE src/plugin)
    target_link_libraries(SappKeysUiShot PRIVATE
      sappkeys_core
      juce::juce_audio_utils
      juce::juce_dsp
      juce::juce_recommended_config_flags
    )
    target_compile_definitions(SappKeysUiShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  endif()
endif()
