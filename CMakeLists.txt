cmake_minimum_required(VERSION 3.20)

project(wireless_tsn_deadline_lab VERSION 0.1.0 LANGUAGES CXX)

option(WTSN_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(wtsn_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(wtsn_core
    src/config.cpp
    src/simulator.cpp
)
target_include_directories(wtsn_core PUBLIC include)
target_compile_features(wtsn_core PUBLIC cxx_std_20)

wtsn_strict_warnings(wtsn_core)

if(WTSN_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(wtsn_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(wtsn_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(wtsn-sim src/main.cpp)
target_link_libraries(wtsn-sim PRIVATE wtsn_core)
wtsn_strict_warnings(wtsn-sim)

include(CTest)
if(BUILD_TESTING)
    add_executable(wtsn-tests tests/cpp/test_simulator.cpp)
    target_link_libraries(wtsn-tests PRIVATE wtsn_core)
    wtsn_strict_warnings(wtsn-tests)
    target_compile_definitions(wtsn-tests PRIVATE WTSN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME wtsn-core-tests COMMAND wtsn-tests)
endif()

install(TARGETS wtsn-sim RUNTIME DESTINATION bin)
