cmake_minimum_required(VERSION 3.15)
project(ParallelSystems LANGUAGES CXX)

# Use C++17 or higher
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
add_executable(${PROJECT_NAME}
        main.cc
        controllers/HashCrackController.cpp
        controllers/HashCrackController.h
        models/ManagerToWorkerDTO.h
        models/WorkerToManagerDTO.h
)

# Manually add MongoDB include paths (from /usr/local)
target_include_directories(${PROJECT_NAME} PRIVATE
        /usr/local/include/mongocxx
        /usr/local/include/bsoncxx
)

# Link libraries manually
target_link_directories(${PROJECT_NAME} PRIVATE /usr/local/lib)

find_package(libmongocxx REQUIRED)
find_package(libbsoncxx REQUIRED)

add_library(libmongocxx INTERFACE IMPORTED)
add_library(libbsoncxx INTERFACE IMPORTED)

set_property(TARGET libmongocxx PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES ${LIBMONGOCXX_INCLUDE_DIRS})
set_property(TARGET libbsoncxx PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES ${LIBBSONCXX_INCLUDE_DIRS})

include_directories(${LIBMONGOCXX_INCLUDE_DIRS} ${LIBBSONCXX_INCLUDE_DIRS})

link_directories(${LIBMONGOCXX_INCLUDE_DIRS})
link_directories(${LIBBSONCXX_INCLUDE_DIR})

# Drogon
find_package(Drogon CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

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
