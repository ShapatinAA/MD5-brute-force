cmake_minimum_required(VERSION 3.15)
project(ParallelSystems LANGUAGES CXX)

# Use C++17 or higher
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /DDEBUG")

# Source files
add_executable(${PROJECT_NAME}
        main.cc
        controllers/HashCrackController.cpp
        controllers/HashCrackController.h
        models/ManagerToWorkerDTO.h
        models/WorkerToManagerDTO.h
)

# Drogon
find_package(Drogon CONFIG REQUIRED)
find_package(SimpleAmqpClient CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE SimpleAmqpClient Drogon::Drogon)

# Add extra sources
aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)
aux_source_directory(models MODEL_SRC)

target_sources(${PROJECT_NAME} PRIVATE
        ${CTL_SRC}
        ${FILTER_SRC}
        ${PLUGIN_SRC}
        ${MODEL_SRC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/models
)

# Views
drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views ${CMAKE_CURRENT_BINARY_DIR})

# (Optional) Debug message
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
