#!/bin/bash

# make required directories and keys and build executable

mkdir logfiles rsa_keys build
if [ $? -ne 0 ]; then
    echo "did you run this script already?"
    exit
fi

# generate rsa keypair in required format for ctf_logserver
echo -n "[*] generating rsa keypair in ./rsa_keys ... "
openssl genrsa -out rsa_keys/priv_key.pem 1>/dev/null 2>/dev/null && 
openssl rsa -pubout -in rsa_keys/priv_key.pem -outform PEM -out rsa_keys/pub_key.pem 1>/dev/null 2>/dev/null
if [ $? -eq 0 ]; then
    echo "success"
else
    echo -e "failed \ntry by hand. need both a pub and priv rsa key file in .pem format"
    exit
fi

echo "[*] building executable"
cd build && cmake .. && make
if [ $? -eq 0 ]; then
    echo success
    echo "run server by typing:"
    echo "./server port rsa_keys/pub_key.pem rsa_keys/priv_key.pem"
else
    echo "build error :("
fi
