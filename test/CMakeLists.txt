cmake_minimum_required(VERSION 3.0)
project(fast_shared_mutex LANGUAGES CXX)

# Default build with Release
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

enable_testing()

find_package(GTest REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

set(TESTS test_mutex_wrapper test_shared_mutex benchmark)

foreach (TEST ${TESTS})
    add_executable(${TEST} ${CMAKE_CURRENT_SOURCE_DIR}/${TEST}.cc)
    add_test(NAME ${TEST} COMMAND ${TEST})
    target_link_libraries(${TEST} gflags GTest::GTest GTest::Main)
    if (WIN32)
        target_link_libraries(${TEST} synchronization)  # for WaitOnAddress
    endif()
endforeach()
