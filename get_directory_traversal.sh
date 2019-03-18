#!/bin/sh

printf "GET /../file.txt HTTP/1.1\r\n\r\n" | nc 127.0.0.1 65333
