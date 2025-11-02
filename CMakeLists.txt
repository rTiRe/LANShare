cmake_minimum_required(VERSION 3.10)
project(LocalNetworkDemo LANGUAGES CXX)

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Исходники
set(SOURCES
    main.cpp
    SubnetBroadcaster.cpp
    SubnetListener.cpp
)

# Исполняемый файл
add_executable(netdemo ${SOURCES})

# Линкуем Threads только на UNIX (Linux/macOS)
if(UNIX)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(netdemo PRIVATE Threads::Threads)
endif()
