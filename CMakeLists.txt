cmake_minimum_required (VERSION 3.0)
project(ctf_logserver)

set(EXECUTABLE_OUTPUT_PATH ../)

add_executable(server 	
	src/server.cpp 
	src/http_server.cpp
	src/http_server.hpp
	src/responses.cpp
	include/cpp-jwt/jwt/jwt.hpp # https://github.com/arun11299/cpp-jwt
)

find_package(OpenSSL REQUIRED) # needed by jwt-cpp

include_directories(${OPENSSL_INCLUDE_DIR})

target_compile_options(server PUBLIC -Wall -Wextra -std=c++17 -g)

target_link_libraries(server PUBLIC
	${OPENSSL_LIBRARIES}
	-lpthread # boost::beast
	-lstdc++fs # std::filesystem
)
