# ctf_logserver
Service for ctf-project.

Meant to be used in custom-made ctf environment based on [iCTF-Framework](https://github.com/ucsb-seclab/ictf-framework)

Designed to have vulnerabilities that can be exploited by other teams to recieve a flag set by scorebot. Scorebot will POST a flag as user flag_id (generated randomly each round) and try to GET it again to ensure the service is still up.

Authentification is made by a server-signed Json Web Token, that needs to be provided on GET.

possible exploits:
* GET /user?debug=true HTTP/1.1\r\nAuthorization: Bearer _some jwt_\r\n\r\n -> logfile
* client-signed token (see description in exploit\_client\_signed\_token.py)
* token with header field _alg_ set to _none_ leading to skipped sig verification
* server sending token for user on GET index.html?name=user


exploits to be added:
* ~~maybe skip fs::exists() check in HttpConnection::handleGET so one can simply request a token for existing logfile~~

## build:
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
$ ./server 65333 ./pub_key.pem ./priv_key.pem &  
$ cat logfile | logserver_client.py sendlogs some_user 127.0.0.1 65333
$ logserver_client.py getlogs some_user 127.0.0.1 65333
$ logserver_client.py showtoken some_user
$ printf "GET /some_user HTTP/1.1\r\nAuthorization: Bearer j.w.t\_\r\n\r\n" | nc 127.0.0.1 65333
```
