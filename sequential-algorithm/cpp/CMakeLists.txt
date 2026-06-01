cmake_minimum_required(VERSION 3.16)
project(tabu_search CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.3.2
)
FetchContent_MakeAvailable(CLI11)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(tabu_search
    src/main.cpp
    src/config.cpp
    src/logger.cpp
    src/clusterize.cpp
    src/routes.cpp
    src/neighborhoods.cpp
    src/solutions.cpp
)

target_include_directories(tabu_search PRIVATE src)
target_link_libraries(tabu_search PRIVATE CLI11::CLI11 nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(tabu_search PRIVATE /W3 /O2)
else()
    target_compile_options(tabu_search PRIVATE -Wall -Wextra -O2)
endif()
