# Test executables compile the plugin sources directly (no plugin wrapper).
# Source/PluginEntry.cpp holds createPluginFilter() and is excluded here.
set(AXSYNTH_TEST_SOURCES ${AXSYNTH_SOURCES})
list(FILTER AXSYNTH_TEST_SOURCES EXCLUDE REGEX ".*/PluginEntry\\.cpp$")

function(axsynth_add_test_app name)
    juce_add_console_app(${name} PRODUCT_NAME ${name})
    target_sources(${name} PRIVATE ${AXSYNTH_TEST_SOURCES} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/Source ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE ${AXSYNTH_DEFINES}
        JUCE_STANDALONE_APPLICATION=1 AXSYNTH_TESTING=1)
    target_link_libraries(${name} PRIVATE ${AXSYNTH_MODULES} ${AXSYNTH_SYSLIBS} ws2_32
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(AXSYNTH_WARNINGS_AS_ERRORS AND MSVC)
        target_compile_options(${name} PRIVATE /WX)
    endif()
endfunction()

# Unit tests + smoke test (JUCE UnitTest framework).
file(GLOB AXSYNTH_UNIT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
axsynth_add_test_app(AxSynthTests ${AXSYNTH_UNIT_TEST_SOURCES})

# Realtime safety: global operator new hook, processBlock 10 000x.
axsynth_add_test_app(AxSynthRealtimeTest ${CMAKE_CURRENT_SOURCE_DIR}/realtime/RealtimeTest.cpp)
