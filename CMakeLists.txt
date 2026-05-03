cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)

set(major_version 2)
set(minor_version 0)
set(patch_version 0)
set(prerelease_version "-rc.1")
execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
math(EXPR build_number 0x${GIT_HASH})
message(STATUS "build_number: ${build_number}")

if(DEFINED ENV{CI})
    execute_process(
        COMMAND python -c "import uuid; u=uuid.uuid4().hex; print(*[f'0x{u[8*i:8*i+8]}' for i in range(4)], sep=', ')"
        OUTPUT_VARIABLE processor_uid
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND python -c "import uuid; u=uuid.uuid4().hex; print(*[f'0x{u[8*i:8*i+8]}' for i in range(4)], sep=', ')"
        OUTPUT_VARIABLE controller_uid
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "processor_uid: ${processor_uid}")
    message(STATUS "controller_uid: ${controller_uid}")
else()
    set(processor_uid "0x06659064, 0x6f594d89, 0xb4a2aa65, 0xf13c17b2")
    set(controller_uid "0x3c46dde3, 0xa03045f8, 0x80688f81, 0x88408160")
endif()

project(beatrice VERSION ${major_version}.${minor_version}.${patch_version})
option(BEATRICE_DEV_VERSION "Development version" ON)
option(BEATRICE_ENABLE_AUV2 "Build a macOS AUv2 component wrapper around the VST3 plug-in" OFF)
option(BEATRICE_ENABLE_BENCHMARKS "Build local processor benchmark tools" OFF)
option(BEATRICE_ENABLE_LOADER_LOG "Write macOS runtime loader diagnostics to /tmp/beatrice-vst-loader.log" OFF)
set(BEATRICE_MAC_RUNTIME "" CACHE FILEPATH "Path to the macOS Beatrice runtime bundle, e.g. beatrice/v20rc0.abi3.so from VCClient")
if(APPLE AND BEATRICE_ENABLE_AUV2)
    enable_language(OBJCXX)
endif()
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
if(APPLE)
    set(SMTG_CREATE_PLUGIN_LINK 0 CACHE BOOL "Do not auto-link build outputs into the user's VST3 folder" FORCE)
endif()

set(vst3sdk_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/vst3sdk)

add_subdirectory(lib/vst3sdk)

if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(vstgui_target vstgui vstgui_support)
        if(TARGET ${vstgui_target})
            target_compile_options(${vstgui_target}
                PRIVATE -Wno-error=unused-but-set-variable
                PRIVATE -Wno-error=deprecated-declarations
            )
        endif()
    endforeach()
endif()

smtg_enable_vst3_sdk()

configure_file(src/vst/metadata.h.in src/vst/metadata.h)
set(SMTG_PACKAGE_ICON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resource/icon.ico)

set(BEATRICE_COMMON_SOURCES
    src/common/parameter_schema.cc
    src/common/parameter_state.cc
    src/common/processor_core_0.cc
    src/common/processor_core_1.cc
    src/common/processor_core_2.cc
    src/common/processor_proxy.cc
)

smtg_add_vst3plugin(${target}
    ${BEATRICE_COMMON_SOURCES}
    src/vst/controller.cc
    src/vst/editor.cc
    src/vst/factory.cc
    src/vst/parameter.cc
    src/vst/processor.cc
)

target_include_directories(${target}
    PRIVATE src
    PRIVATE lib
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/src/vst
)

target_link_libraries(${target}
    PRIVATE sdk
    PRIVATE vstgui_support
)

if(SMTG_WIN)
    add_library(beatricelib STATIC IMPORTED)
    set_property(
        TARGET beatricelib
        PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/beatricelib/beatrice.lib
    )
    target_link_libraries(${target}
        PRIVATE beatricelib
    )
    target_sources(${target}
        PRIVATE resource/beatrice.rc
    )
elseif(APPLE)
    if(NOT BEATRICE_MAC_RUNTIME)
        message(FATAL_ERROR "BEATRICE_MAC_RUNTIME is required on macOS. Pass the path to VCClient's beatrice/v20rc0.abi3.so.")
    endif()
    if(NOT EXISTS "${BEATRICE_MAC_RUNTIME}")
        message(FATAL_ERROR "BEATRICE_MAC_RUNTIME does not exist: ${BEATRICE_MAC_RUNTIME}")
    endif()
    get_filename_component(BEATRICE_MAC_RUNTIME_FILENAME "${BEATRICE_MAC_RUNTIME}" NAME)
    target_sources(${target}
        PRIVATE src/common/beatricelib_dynamic_loader.cc
    )
	    target_compile_definitions(${target}
	        PRIVATE BEATRICE_MAC_RUNTIME_FILENAME="${BEATRICE_MAC_RUNTIME_FILENAME}"
	    )
	    if(BEATRICE_ENABLE_LOADER_LOG)
	        target_compile_definitions(${target}
	            PRIVATE BEATRICE_ENABLE_LOADER_LOG=1
	        )
	    endif()
    target_link_libraries(${target}
        PRIVATE ${CMAKE_DL_LIBS}
    )
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/../Resources/beatrice"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BEATRICE_MAC_RUNTIME}" "$<TARGET_FILE_DIR:${target}>/../Resources/beatrice/${BEATRICE_MAC_RUNTIME_FILENAME}"
        COMMAND codesign --force --sign - "$<TARGET_FILE_DIR:${target}>/../Resources/beatrice/${BEATRICE_MAC_RUNTIME_FILENAME}"
        COMMAND codesign --force --sign - "$<TARGET_FILE_DIR:${target}>/../.."
        COMMENT "Copying Beatrice macOS runtime"
    )
else()
    message(WARNING "Unsupported platform")
endif()

if(BEATRICE_ENABLE_BENCHMARKS)
    add_executable(beatrice_processor_bench
        tools/bench_processor.cc
        ${BEATRICE_COMMON_SOURCES}
    )
    target_include_directories(beatrice_processor_bench
        PRIVATE src
        PRIVATE lib
    )
    if(SMTG_WIN)
        target_link_libraries(beatrice_processor_bench
            PRIVATE beatricelib
        )
    elseif(APPLE)
        target_sources(beatrice_processor_bench
            PRIVATE src/common/beatricelib_dynamic_loader.cc
        )
	    target_compile_definitions(beatrice_processor_bench
	        PRIVATE BEATRICE_MAC_RUNTIME_FILENAME="${BEATRICE_MAC_RUNTIME_FILENAME}"
	    )
	    if(BEATRICE_ENABLE_LOADER_LOG)
	        target_compile_definitions(beatrice_processor_bench
	            PRIVATE BEATRICE_ENABLE_LOADER_LOG=1
	        )
	    endif()
        target_link_libraries(beatrice_processor_bench
            PRIVATE ${CMAKE_DL_LIBS}
        )
    endif()
endif()

if(APPLE AND BEATRICE_ENABLE_AUV2)
    if(NOT XCODE)
        message(FATAL_ERROR "BEATRICE_ENABLE_AUV2 requires the Xcode CMake generator.")
    endif()
    if(NOT SMTG_COREAUDIO_SDK_PATH)
        message(FATAL_ERROR "BEATRICE_ENABLE_AUV2 requires SMTG_COREAUDIO_SDK_PATH, e.g. a Steinberg-compatible external.apple.coreaudio checkout.")
    endif()

    set(auv2_target ${target}_au)
    set(auv2_bundle_name Beatrice)
    string(TIMESTAMP CocoaIdStamp "%Y%j%H%M%S")
    string(MAKE_C_IDENTIFIER ${CocoaIdStamp} CocoaId)

    add_library(${auv2_target} MODULE
        ${public_sdk_SOURCE_DIR}/source/vst/auwrapper/aucocoaview.mm
        ${public_sdk_SOURCE_DIR}/source/vst/auwrapper/aucocoaview.h
        ${public_sdk_SOURCE_DIR}/source/vst/auwrapper/ausdk.mm
        ${public_sdk_SOURCE_DIR}/source/vst/auwrapper/auwrapper.mm
        ${public_sdk_SOURCE_DIR}/source/vst/auwrapper/auwrapper.h
        ${public_sdk_SOURCE_DIR}/source/vst/auwrapper/NSDataIBStream.mm
        ${public_sdk_SOURCE_DIR}/source/vst/auwrapper/NSDataIBStream.h
    )
    add_dependencies(${auv2_target} ${target})
    smtg_target_setup_universal_binary(${auv2_target})
    smtg_target_codesign(${auv2_target})
    smtg_target_set_bundle(${auv2_target}
        INFOPLIST ${CMAKE_CURRENT_SOURCE_DIR}/resource/au-info.plist
        EXTENSION component
    )
    target_compile_features(${auv2_target}
        PUBLIC cxx_std_17
    )
    target_compile_definitions(${auv2_target}
        PRIVATE SMTG_AUCocoaUIBase_CLASS_NAME=SMTG_AUCocoaUIBase_${auv2_bundle_name}${CocoaId}
        PRIVATE CA_USE_AUDIO_PLUGIN_ONLY=0
    )
    target_include_directories(${auv2_target}
        PRIVATE "${SMTG_COREAUDIO_SDK_PATH}"
        PRIVATE "${SMTG_COREAUDIO_SDK_PATH}/AudioUnits"
        PRIVATE "${SMTG_COREAUDIO_SDK_PATH}/PublicUtility"
    )
    target_link_libraries(${auv2_target}
        PRIVATE sdk_hosting
        PRIVATE "-framework AudioUnit"
        PRIVATE "-framework CoreMIDI"
        PRIVATE "-framework AudioToolbox"
        PRIVATE "-framework CoreFoundation"
        PRIVATE "-framework Carbon"
        PRIVATE "-framework Cocoa"
        PRIVATE "-framework CoreAudio"
    )

    set(VST3_OUTPUT_DIR ${CMAKE_BINARY_DIR}/VST3)
    set(outputdir ${VST3_OUTPUT_DIR}/$<CONFIG>)
    set_target_properties(${auv2_target}
        PROPERTIES
            XCODE_ATTRIBUTE_GENERATE_PKGINFO_FILE "YES"
            XCODE_ATTRIBUTE_PRODUCT_NAME "${auv2_bundle_name}"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.prj-beatrice.beatrice.auv2"
            LIBRARY_OUTPUT_DIRECTORY ${VST3_OUTPUT_DIR}
    )
    add_custom_command(
        TARGET ${auv2_target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${outputdir}/${auv2_bundle_name}.component/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${outputdir}/${auv2_bundle_name}.component/Contents/Resources/plugin.vst3"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${outputdir}/$<TARGET_FILE_NAME:${target}>.vst3" "${outputdir}/${auv2_bundle_name}.component/Contents/Resources/plugin.vst3"
        COMMAND codesign --force --sign - "${outputdir}/${auv2_bundle_name}.component"
        COMMENT "Linking VST3 bundle into AUv2 wrapper"
    )
endif()

# ----------------------------------------------------------------

set(DISTRIBUTION_DIR ${CMAKE_CURRENT_BINARY_DIR}/beatrice_${full_version_str})
add_custom_target(distribution
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DISTRIBUTION_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_FILE_DIR:${target}>/../../.. ${DISTRIBUTION_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt ${DISTRIBUTION_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/LICENSES_BUNDLED.txt ${DISTRIBUTION_DIR}
)
add_dependencies(distribution ${target})

if(NOT BEATRICE_DEV_VERSION)
    set(PARAPHERNALIA_DIR ${DISTRIBUTION_DIR}/beatrice_paraphernalia_jvs)
    file(MAKE_DIRECTORY ${PARAPHERNALIA_DIR})
    set(BASE_URL "https://huggingface.co/fierce-cats/beatrice-2.0.0-alpha/resolve/rc.0/rc.0/beatrice_paraphernalia_jvs_156_03000000")
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
        file(DOWNLOAD ${BASE_URL}/${file} ${PARAPHERNALIA_DIR}/${file})
    endforeach()
endif()
