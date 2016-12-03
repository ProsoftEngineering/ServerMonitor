cmake_minimum_required(VERSION 3.1)
project(ServerMonitor)

add_subdirectory(vendor/tiny-process-library)

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/types.hpp
  src/curl.cpp
  src/curl.hpp
)

if(MSVC)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Werror
    -std=c++14
  )
endif()

find_package(CURL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})

target_include_directories(${PROJECT_NAME} PRIVATE
  vendor/json/src
  vendor/tiny-process-library
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  tiny-process-library
)
