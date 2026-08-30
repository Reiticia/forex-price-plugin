cmake_minimum_required(VERSION 3.20)

project(FxPricePlugin VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "FxPricePlugin 仅支持 Windows。")
endif()

add_library(FxPricePlugin SHARED
    src/Config.cpp
    src/dllmain.cpp
    src/FxPricePlugin.cpp
    src/MarketData.cpp
    src/OptionsWindow.cpp
    src/Quote.cpp
    src/BiquoteClient.cpp
)
target_include_directories(FxPricePlugin PRIVATE src)
target_compile_features(FxPricePlugin PRIVATE cxx_std_17)
target_compile_definitions(FxPricePlugin PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(FxPricePlugin PRIVATE winhttp user32)
set_target_properties(FxPricePlugin PROPERTIES PREFIX "" OUTPUT_NAME "FxPricePlugin")
if(MSVC)
    target_compile_options(FxPricePlugin PRIVATE /utf-8 /W4 /permissive- /EHsc)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(FxPriceTests
        tests/Tests.cpp
        src/Config.cpp
        src/MarketData.cpp
        src/Quote.cpp
        src/BiquoteClient.cpp
    )
    target_include_directories(FxPriceTests PRIVATE src)
    target_compile_features(FxPriceTests PRIVATE cxx_std_17)
    target_compile_definitions(FxPriceTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(FxPriceTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(FxPriceTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME FxPriceTests COMMAND FxPriceTests)

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

    add_executable(FxPricePluginSmoke tests/PluginSmoke.cpp)
    target_include_directories(FxPricePluginSmoke PRIVATE src)
    target_compile_features(FxPricePluginSmoke PRIVATE cxx_std_17)
    target_compile_definitions(FxPricePluginSmoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(FxPricePluginSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    endif()
    add_test(NAME FxPricePluginSmoke
             COMMAND FxPricePluginSmoke $<TARGET_FILE:FxPricePlugin>)
endif()
