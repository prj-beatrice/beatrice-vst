cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 20)

set(major_version 2)
set(minor_version 0)
set(patch_version 0)
set(prerelease_version "-rc.2")
execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
math(EXPR build_number 0x${GIT_HASH})
message(STATUS "build_number: ${build_number}")

set(BEATRICE_PROCESSOR_UID "0x06659064, 0x6f594d89, 0xb4a2aa65, 0xf13c17b2" CACHE STRING "VST3 processor UUID")
set(BEATRICE_CONTROLLER_UID "0x3c46dde3, 0xa03045f8, 0x80688f81, 0x88408160" CACHE STRING "VST3 controller UUID")
message(STATUS "BEATRICE_PROCESSOR_UID: ${BEATRICE_PROCESSOR_UID}")
message(STATUS "BEATRICE_CONTROLLER_UID: ${BEATRICE_CONTROLLER_UID}")

project(beatrice VERSION ${major_version}.${minor_version}.${patch_version})
option(BEATRICE_DEV_VERSION "Development version" ON)
if(BEATRICE_DEV_VERSION)
    set(full_version_str ${major_version}.${minor_version}.${patch_version}${prerelease_version}+dev.${GIT_HASH})
else()
    set(full_version_str ${major_version}.${minor_version}.${patch_version}${prerelease_version})
endif()
set(target beatrice_${full_version_str})

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/MT>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/MT>")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/arch:AVX2>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>")

set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES 0)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES 0)
set(SMTG_ENABLE_VSTGUI_SUPPORT 1)

set(vst3sdk_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/vst3sdk)

add_subdirectory(lib/vst3sdk)

smtg_enable_vst3_sdk()

configure_file(src/vst/metadata.h.in src/vst/metadata.h @ONLY)
configure_file(resource/Info.plist.in resource/Info.plist @ONLY)
set(SMTG_PACKAGE_ICON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resource/icon.ico)

smtg_add_vst3plugin(${target}
    src/common/parameter_schema.cc
    src/common/parameter_state.cc
    src/common/processor_core_0.cc
    src/common/processor_core_1.cc
    src/common/processor_core_2.cc
    src/common/processor_proxy.cc
    src/vst/controller.cc
    src/vst/editor.cc
    src/vst/factory.cc
    src/vst/parameter.cc
    src/vst/processor.cc
)

add_library(beatricelib STATIC IMPORTED)
if(SMTG_WIN)
    set(BEATRICE_LIBRARY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/beatricelib/beatrice.lib)
elseif(SMTG_MAC)
    set(BEATRICE_LIBRARY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/beatricelib/libbeatrice.a)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()
set_property(
    TARGET beatricelib
    PROPERTY IMPORTED_LOCATION ${BEATRICE_LIBRARY_PATH}
)

target_include_directories(${target}
    PRIVATE src
    PRIVATE lib
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src/vst
)

target_link_libraries(${target}
    PRIVATE beatricelib
    PRIVATE sdk
    PRIVATE vstgui_support
)

if(SMTG_MAC)
    set(platform mac)
    smtg_target_add_plugin_resource(${target} resource/logo.png)
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "com.prj-beatrice.vst3.beatrice2"
        INFOPLIST "${CMAKE_CURRENT_BINARY_DIR}/resource/Info.plist" PREPROCESS
    )
elseif(SMTG_WIN)
    set(platform win)
    target_sources(${target}
        PRIVATE resource/beatrice.rc
    )
endif()

# ----------------------------------------------------------------

set(ARTIFACT_ROOT ${CMAKE_CURRENT_BINARY_DIR}/dist)
file(MAKE_DIRECTORY ${ARTIFACT_ROOT})
get_target_property(PLUGIN_PACKAGE_PATH ${target} SMTG_PLUGIN_PACKAGE_PATH)

set(PARAPHERNALIA_COMMANDS)
if(NOT BEATRICE_DEV_VERSION)
    set(PARAPHERNALIA_DIR ${CMAKE_CURRENT_BINARY_DIR}/beatrice_paraphernalia_jvs)
    file(MAKE_DIRECTORY ${PARAPHERNALIA_DIR})
    set(PARAPHERNALIA_BASE_URL "https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/rc.0/rc.0/beatrice_paraphernalia_jvs_156_03000000")
    set(PARAPHERNALIA_FILES
        beatrice_paraphernalia_jvs.toml
        phone_extractor.bin
        pitch_estimator.bin
        speaker_embeddings.bin
        waveform_generator.bin
        embedding_setter.bin
        noimage.png
    )
    foreach(file ${PARAPHERNALIA_FILES})
        file(DOWNLOAD ${PARAPHERNALIA_BASE_URL}/${file} ${PARAPHERNALIA_DIR}/${file})
    endforeach()
    list(APPEND PARAPHERNALIA_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PARAPHERNALIA_DIR} ${target}/beatrice_paraphernalia_jvs
    )
endif()

add_custom_target(distribution
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${target}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${target}_${platform}.zip
    COMMAND ${CMAKE_COMMAND} -E make_directory ${target}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PLUGIN_PACKAGE_PATH} ${target}/${target}.vst3
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt ${target}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSES_BUNDLED.txt ${target}
    ${PARAPHERNALIA_COMMANDS}
    COMMAND ${CMAKE_COMMAND} -E tar cfv ${target}_${platform}.zip --format=zip ${target}
    WORKING_DIRECTORY ${ARTIFACT_ROOT}
)
add_dependencies(distribution ${target})
