cmake_minimum_required(VERSION 3.6)
project(ServerMonitor)

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/url_apple.mm
)

if(MSVC)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Werror
  )
endif()
