cmake_minimum_required (VERSION 3.0)
project(ctf_logserver)

set(EXECUTABLE_OUTPUT_PATH ../)

add_executable(server src/server.cpp src/http_server.cpp src/jwt.cpp src/http_server.hpp src/responses.cpp)

target_compile_options(server PUBLIC -Wall -Wextra -std=c++17 -g)

target_link_libraries(server PUBLIC
	-lpthread # boost::beast
	-lstdc++fs # std::filesystem
)
