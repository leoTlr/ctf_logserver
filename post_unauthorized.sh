#!/bin/sh

printf "POST /user2 HTTP/1.1\r\nContent-Type: text/plai\r\nContent-Length: 11\r\n\r\nentry\nentry" | nc 127.0.0.1 65333
