# ctf_logserver
Service for ctf-project. Work in progress


possible exploits atm:
* GET /user?debug=true HTTP/1.1\r\nAuthorization: Bearer _some jwt_\r\n\r\n
* client-signed token (see description in exploit\_client\_signed\_token.py)

exploits to be added:
* token with header field _alg_ set to _none_ leading to skipped verification
* maybe skip fs::exists() check in HttpConnection::handleGET so one can simply request a token for existing logfile

## build:
__change the paths to rsa keypair (.pem format) in src/server.cpp before compiling__ (will do arg parsing soon)
```bash
$ mkdir build
$ cd build
$ cmake ..
$ make
```

* server requires openssl and boost::beast (and [arun11299/cpp-jwt](https://github.com/arun11299/cpp-jwt) (MIT) but this is included)
* exploit and test scripts require pyjwt `$ pip install pyjwt`
* client only requires python stdlib

## usage:
either use provided client or pipe requests into nc

```bash
$ cat logfile | logserver_client.py sendlogs some_user 127.0.0.1 65333
$ logserver_client.py getlogs some_user 127.0.0.1 65333
$ logserver_client.py showtoken some_user
$ printf "GET /some_user HTTP/1.1\r\nAuthorization: Bearer j.w.t\_\r\n\r\n" | nc 127.0.0.1 65333
```
