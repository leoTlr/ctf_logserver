#!/bin/sh

# dXNlcjE6dGVzdA== -> user1:test
printf "GET /user1 HTTP/1.1\r\nAuthorization: Basic dXNlcjE6dGVzdA==\r\n\r\n" | nc 127.0.0.1 65333
