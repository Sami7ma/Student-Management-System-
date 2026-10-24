cmake_minimum_required(VERSION 3.14)
project(StudentManagementSystem VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Strict warnings, treated as errors.
if(MSVC)
  add_compile_options(/W4 /WX /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

option(SMS_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(SMS_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

file(GLOB_RECURSE SMS_LIB_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/models/*.cpp
     src/services/*.cpp src/storage/*.cpp src/ui/*.cpp)

add_library(sms_core STATIC ${SMS_LIB_SOURCES})
target_include_directories(sms_core PUBLIC include)

add_executable(sms src/main.cpp)
target_link_libraries(sms PRIVATE sms_core)

enable_testing()
add_executable(sms_tests tests/test_main.cpp)
target_link_libraries(sms_tests PRIVATE sms_core)
add_test(NAME sms_unit_tests COMMAND sms_tests)
