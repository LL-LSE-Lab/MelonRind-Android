cmake_minimum_required(VERSION 3.22)

project(melonrind_android LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MELONRIND_BUILD_TESTS "Build host-side model tests" ON)

add_library(melonrind_model STATIC
    src/model/HudTransform.cpp
    src/model/Markers.cpp
    src/model/Preview.cpp
)
set_target_properties(melonrind_model PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(melonrind_model PUBLIC src)
target_compile_options(melonrind_model PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra -Wpedantic>
)

if(MELONRIND_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(melonrind_model_tests
        tests/model_tests.cpp
    )
    target_link_libraries(melonrind_model_tests PRIVATE melonrind_model)
    add_test(NAME melonrind_model_tests COMMAND melonrind_model_tests)

    add_executable(melonrind_runtime_tests
        tests/runtime_tests.cpp
        src/runtime/Bedrock12651Adapter.cpp
        src/runtime/PlayerSnapshot.cpp
        src/runtime/SignatureCatalog.cpp
    )
    target_include_directories(melonrind_runtime_tests PRIVATE src)
    add_test(NAME melonrind_runtime_tests COMMAND melonrind_runtime_tests)
endif()

if(ANDROID)
    set(CMAKE_ANDROID_STL_TYPE c++_shared CACHE STRING "Android C++ runtime" FORCE)

    # Keep the SDK's fmt dependency portable across Android NDK revisions;
    # some libc header sets do not expose malloc/free transitively.
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:-include>"
        "$<$<COMPILE_LANGUAGE:CXX>:cstdlib>")

    set(PRELOADER_ANDROID_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/third_party/preloader-android"
        CACHE PATH "Path to the Preloader checkout matching the target launcher")

    if(NOT EXISTS "${PRELOADER_ANDROID_ROOT}/include/pl/Mod.hpp")
        message(FATAL_ERROR
            "PRELOADER_ANDROID_ROOT must point to a preloader-android checkout")
    endif()

    if(NOT TARGET preloader)
        add_subdirectory("${PRELOADER_ANDROID_ROOT}"
                         "${CMAKE_BINARY_DIR}/_preloader_android")
    endif()

    add_library(melonrind_android SHARED
        src/mod/ModEntry.cpp
        src/runtime/GameAdapter.cpp
        src/runtime/Bedrock12651Adapter.cpp
        src/runtime/HudRenderer.cpp
        src/runtime/PlayerSnapshot.cpp
        src/runtime/SignatureCatalog.cpp
    )
    set_target_properties(melonrind_android PROPERTIES
        OUTPUT_NAME "melonrind_android"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}")
    target_include_directories(melonrind_android PRIVATE
        src
        "${PRELOADER_ANDROID_ROOT}/include")
    target_link_libraries(melonrind_android PRIVATE
        preloader
        melonrind_model
        nlohmann_json::nlohmann_json
        Boost::pfr
        magic_enum::magic_enum
        fmt::fmt)
    find_library(MELONRIND_LOG_LIB log)
    if(MELONRIND_LOG_LIB)
        target_link_libraries(melonrind_android PRIVATE "${MELONRIND_LOG_LIB}")
    endif()
    target_compile_options(melonrind_android PRIVATE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -Wall -Wextra -Wpedantic)
    target_compile_definitions(melonrind_android PRIVATE
        MELONRIND_ANDROID=1
        NOMINMAX)
endif()
