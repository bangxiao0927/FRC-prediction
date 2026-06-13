cmake_minimum_required(VERSION 3.20)

project(frc_prediction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(frc_prediction
    src/cache.cpp
    src/config.cpp
    src/main.cpp
    src/opr.cpp
    src/picklist.cpp
    src/predictor.cpp
    src/roles.cpp
    src/stats.cpp
    src/tba_client.cpp
)

target_link_libraries(frc_prediction
    PRIVATE
        cpr::cpr
        nlohmann_json::nlohmann_json
)

enable_testing()

add_executable(frc_prediction_tests
    tests/predictor_tests.cpp
    src/opr.cpp
    src/picklist.cpp
    src/predictor.cpp
    src/roles.cpp
    src/stats.cpp
)

target_link_libraries(frc_prediction_tests
    PRIVATE
        nlohmann_json::nlohmann_json
)

add_test(NAME frc_prediction_tests COMMAND frc_prediction_tests)
