cmake_minimum_required(VERSION 3.22)

project(WUPdate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(WUPdate
    src/main.cpp
    src/error.cpp
    src/update_client.cpp
    src/commands.cpp
)

target_compile_definitions(WUPdate PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(WUPdate PRIVATE /W4 /permissive-)
endif()

target_link_libraries(WUPdate PRIVATE
    ole32
    oleaut32
)
