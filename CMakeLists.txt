cmake_minimum_required(VERSION 3.14)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(ChatServer
    main.cpp
    chat_server.cpp
)

target_link_libraries(ChatServer
    pthread
)