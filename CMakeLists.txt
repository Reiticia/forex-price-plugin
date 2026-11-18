cmake_minimum_required(VERSION 3.21)

project(FxPricePlugin VERSION 0.4.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FxPricePlugin 仅支持 Windows。")
endif()

add_library(FxPricePlugin SHARED
    src/MarketData.cpp
    src/FxPricePlugin.cpp
    src/Config.cpp
    src/Quote.cpp
    src/BiquoteClient.cpp
    src/OptionsWindow.cpp
    src/Tooltip.cpp
    src/dllmain.cpp
)

target_include_directories(FxPricePlugin PRIVATE src)
target_compile_features(FxPricePlugin PRIVATE cxx_std_17)
target_compile_definitions(FxPricePlugin PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
if(MSVC)
    target_compile_options(FxPricePlugin PRIVATE /utf-8 /W4 /permissive- /EHsc)
endif()
target_link_libraries(FxPricePlugin PRIVATE winhttp user32)
set_target_properties(FxPricePlugin PROPERTIES
    OUTPUT_NAME "FxPricePlugin"
    PREFIX ""
)

include(CTest)
if(BUILD_TESTING)
    add_executable(FxPriceTests
        tests/Tests.cpp
        src/Config.cpp
        src/Tooltip.cpp
        src/MarketData.cpp
        src/Quote.cpp
        src/BiquoteClient.cpp
    )
    target_include_directories(FxPriceTests PRIVATE src)
    target_compile_features(FxPriceTests PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(FxPriceTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    target_link_libraries(FxPriceTests PRIVATE winhttp)
    add_test(NAME FxPriceTests COMMAND FxPriceTests)

    add_executable(FxPriceOptionsTests
        tests/OptionsTests.cpp
        src/OptionsWindow.cpp
        src/MarketData.cpp
        src/Quote.cpp
        src/BiquoteClient.cpp
    )
    target_include_directories(FxPriceOptionsTests PRIVATE src)
    target_compile_features(FxPriceOptionsTests PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceOptionsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(FxPriceOptionsTests PRIVATE winhttp user32 gdi32)
    if(MSVC)
        target_compile_options(FxPriceOptionsTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME FxPriceOptionsTests COMMAND FxPriceOptionsTests)
    set_tests_properties(FxPriceOptionsTests PROPERTIES TIMEOUT 30)

    add_executable(FxPriceRuntimeTests
        tests/RuntimeTests.cpp src/FxPricePlugin.cpp src/OptionsWindow.cpp
        src/Config.cpp src/Tooltip.cpp src/MarketData.cpp src/Quote.cpp src/BiquoteClient.cpp)
    target_include_directories(FxPriceRuntimeTests PRIVATE src)
    target_compile_features(FxPriceRuntimeTests PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceRuntimeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(FxPriceRuntimeTests PRIVATE winhttp user32 gdi32 ws2_32)
    if(MSVC)
        target_compile_options(FxPriceRuntimeTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME FxPriceRuntimeTests COMMAND FxPriceRuntimeTests)
    set_tests_properties(FxPriceRuntimeTests PROPERTIES TIMEOUT 30)

    add_executable(FxPriceLiveSmoke
        tests/LiveSmoke.cpp
        src/MarketData.cpp
        src/Quote.cpp
        src/BiquoteClient.cpp
    )
    target_include_directories(FxPriceLiveSmoke PRIVATE src)
    target_compile_features(FxPriceLiveSmoke PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceLiveSmoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(FxPriceLiveSmoke PRIVATE winhttp)
    if(MSVC)
        target_compile_options(FxPriceLiveSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()

    add_executable(FxPriceOptionsPreview tests/OptionsPreview.cpp)
    target_include_directories(FxPriceOptionsPreview PRIVATE src)
    target_compile_features(FxPriceOptionsPreview PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceOptionsPreview PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(FxPriceOptionsPreview PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()

    add_executable(FxPricePluginLiveSmoke tests/PluginLiveSmoke.cpp)
    target_include_directories(FxPricePluginLiveSmoke PRIVATE src)
    target_compile_features(FxPricePluginLiveSmoke PRIVATE cxx_std_17)
    target_compile_definitions(FxPricePluginLiveSmoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(FxPricePluginLiveSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()

    add_library(FxPriceLifecycleFixture SHARED tests/LifecycleFixture.cpp
        src/FxPricePlugin.cpp src/OptionsWindow.cpp src/Config.cpp src/Tooltip.cpp
        src/MarketData.cpp src/Quote.cpp src/BiquoteClient.cpp)
    target_include_directories(FxPriceLifecycleFixture PRIVATE src)
    target_compile_features(FxPriceLifecycleFixture PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceLifecycleFixture PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(FxPriceLifecycleFixture PRIVATE winhttp user32 gdi32)
    if(MSVC)
        target_compile_options(FxPriceLifecycleFixture PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_executable(FxPriceLifecycleTests tests/LifecycleTests.cpp)
    target_include_directories(FxPriceLifecycleTests PRIVATE src)
    target_compile_features(FxPriceLifecycleTests PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceLifecycleTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(FxPriceLifecycleTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_dependencies(FxPriceLifecycleTests FxPricePlugin FxPriceLifecycleFixture)
    add_test(NAME FxPriceLifecycleTests COMMAND FxPriceLifecycleTests $<TARGET_FILE:FxPricePlugin>)
    add_test(NAME FxPriceControlledUnload COMMAND FxPriceLifecycleTests $<TARGET_FILE:FxPriceLifecycleFixture>)
    set_tests_properties(FxPriceControlledUnload PROPERTIES TIMEOUT 10)
    set_tests_properties(FxPriceLifecycleTests PROPERTIES TIMEOUT 35)

    add_executable(FxPricePluginSmoke tests/PluginSmoke.cpp)
    target_include_directories(FxPricePluginSmoke PRIVATE src)
    target_compile_features(FxPricePluginSmoke PRIVATE cxx_std_17)
    target_compile_definitions(FxPricePluginSmoke PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(FxPricePluginSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME FxPricePluginSmoke COMMAND FxPricePluginSmoke $<TARGET_FILE:FxPricePlugin>)
endif()
