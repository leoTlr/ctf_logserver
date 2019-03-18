#!/bin/sh

printf "GET /user1 HTTTTP/1.1\r\n\r\n" | nc 127.0.0.1 65333
