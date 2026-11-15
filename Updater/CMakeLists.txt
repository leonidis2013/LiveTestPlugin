# LiveTest Updater: a small desktop app that installs, updates and removes the LiveTest
# plugins from the latest GitHub release (it reads the release's manifest.json).
juce_add_gui_app(LiveTestUpdater
    PRODUCT_NAME "LiveTest Updater"
    VERSION ${LTP_UPDATER_VERSION}
    COMPANY_NAME "LiveTest Audio"
    BUNDLE_ID "com.livetest.updater")

target_sources(LiveTestUpdater PRIVATE Main.cpp Elevate.cpp)
target_include_directories(LiveTestUpdater PRIVATE ${CMAKE_SOURCE_DIR}/Shared)
target_compile_definitions(LiveTestUpdater PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LiveTestUpdater,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LiveTestUpdater,JUCE_VERSION>"
    LTP_UPDATER_VERSION="${LTP_UPDATER_VERSION}"
    LTP_REPO="leonidis2013/LiveTestPlugin"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ${LTP_EXTRA_DEFINES})

target_link_libraries(LiveTestUpdater
    PRIVATE
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(WIN32)
    target_link_libraries(LiveTestUpdater PRIVATE shell32)
endif()
