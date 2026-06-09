cmake_minimum_required(VERSION 3.20)

project(frc_prediction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(frc_prediction
    src/main.cpp
)

target_link_libraries(frc_prediction
    PRIVATE
        cpr::cpr
        nlohmann_json::nlohmann_json
)
