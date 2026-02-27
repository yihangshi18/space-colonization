cmake_minimum_required(VERSION 3.14)
project(space_colonization C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Fetch raylib
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# Our executable
add_executable(space_colonization
    src/main.c
    src/tree.c
)

target_include_directories(space_colonization PRIVATE src)
target_link_libraries(space_colonization raylib m)
