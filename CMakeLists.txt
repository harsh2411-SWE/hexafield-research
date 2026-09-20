cmake_minimum_required(VERSION 3.20)
project(hft_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release so the hot path is actually measurable.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(hft_sim
  src/main.cpp
  src/market_data.cpp
)

target_include_directories(hft_sim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Sample feed lives next to the binary for a zero-config run.
target_compile_definitions(hft_sim PRIVATE
  HFT_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

add_custom_command(TARGET hft_sim POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/data/mock_feed.csv
          $<TARGET_FILE_DIR:hft_sim>/mock_feed.csv
)

# --- Optimization & diagnostics ------------------------------------------------
# GNU/Clang: honor the requested -O3 -march=native -flto stack.
# MSVC: map to the equivalent IPO / architecture flags.

if(MSVC)
  target_compile_options(hft_sim PRIVATE
    /W4
    /permissive-
    /Zc:preprocessor
    /EHsc
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Oi>
    $<$<CONFIG:Release>:/Ot>
    $<$<CONFIG:Release>:/GL>
    $<$<CONFIG:Release>:/Gw>
    $<$<CONFIG:RelWithDebInfo>:/O2>
  )
  target_link_options(hft_sim PRIVATE
    $<$<CONFIG:Release>:/LTCG>
  )
  # AVX2 is a reasonable native floor on modern Windows workstations.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("/arch:AVX2" HFT_HAS_AVX2)
  if(HFT_HAS_AVX2)
    target_compile_options(hft_sim PRIVATE /arch:AVX2)
  endif()
else()
  target_compile_options(hft_sim PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -fno-exceptions
    -fno-rtti
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:RelWithDebInfo>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-march=native>
  )
  target_link_options(hft_sim PRIVATE
    $<$<CONFIG:Release>:-flto>
  )
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT HFT_IPO_OK OUTPUT HFT_IPO_MSG)
if(HFT_IPO_OK)
  set_property(TARGET hft_sim PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# pthread / win threads
find_package(Threads REQUIRED)
target_link_libraries(hft_sim PRIVATE Threads::Threads)
