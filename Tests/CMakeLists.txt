# Offline test runner: every plugin compiled into one console app (LTP_TEST_BUILD), fed
# test signals, checked for NaN/blow-ups/latency and basic model behaviour. It can also
# render each editor to a PNG (--snapshots <dir>) for a quick visual check.
juce_add_console_app(LiveTestTests PRODUCT_NAME "LiveTestTests")

set(plugin_sources "")
set(test_ids ${LTP_ALL_IDS})
if(LTP_ONLY)
    set(test_ids ${LTP_ONLY})
endif()
foreach(id ${test_ids})
    list(APPEND plugin_sources ${CMAKE_SOURCE_DIR}/Plugins/${id}/${id}.cpp)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/Plugins/${id}/${id}.cpp PROPERTIES COMPILE_DEFINITIONS "${LTP_DEFS_${id}}")
endforeach()

target_sources(LiveTestTests PRIVATE TestMain.cpp ${plugin_sources} ${LTP_SHARED_SOURCES})
target_include_directories(LiveTestTests PRIVATE ${CMAKE_SOURCE_DIR}/Shared ${CMAKE_SOURCE_DIR}/Plugins)
target_compile_definitions(LiveTestTests PRIVATE
    LTP_TEST_BUILD=1
    LTP_VERSION="${LTP_SUITE_VERSION}"
    JucePlugin_Name="LiveTestTests"
    ${LTP_COMMON_DEFINES})
target_link_libraries(LiveTestTests
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
