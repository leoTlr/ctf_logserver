#!/usr/bin/python

# test script for ctf logserver
# sends POST with log entries, grabs JWT and tries to access entries with GET

import http.client
from sys import argv, exit

def usage():
    print("usage: {} user".format(argv[0]))
    print("make sure that there are no logfiles for user to test GETting back logentries with recieved JWT")
    exit()

if len(argv) != 2:
    usage()

try:
    user = str(argv[1])
except:
    usage()

# first get a JWT by posting some logentries

print("sending POST /"+user)
conn = http.client.HTTPConnection("localhost", port=65333)
body = "post_get.py: entry1\npost_get.py: entry2\n"
headers={"Content-Length": len(body)}
try:
    conn.request("POST", "/"+user, body=body, headers=headers)
    with conn.getresponse() as res1:
        if (res1.status == 200):
            jwt = res1.read()
            print("got 200 OK and a JWT")
        else:
            print("unexpected response:", res1.status, res1.reason, "cant continue")
            print("are you sure that no logs exist for given user?")
            exit()
except Exception as e:
    print("error:", e)
    exit()

# now try to send a GET to get the entries back again

print("sending GET /"+user)
conn = http.client.HTTPConnection("localhost", port=65333)
headers = {"Authorization": "Bearer " + str(jwt, "ascii")}
try:
    conn.request("GET", "/"+user, headers=headers)
    with conn.getresponse() as res2:
        if (res2.status == 200):
            entries = str(res2.read(), "ascii")
            print("got 200 OK; entries unchanged: ", end="")
            if body == entries:
                print(True)
                print("test succeeded")
                exit()
            else:
                print(False)
                print("--------")
                print('"' + entries + '"')
                print("original: ")
                print('"' + body + '"')
                print("--------")

        else:
            print("unexpected response:\n", res2.status, res2.reason, str(res2.read(), "ascii"))


except Exception as e:
    print("error:", e)

print("test failed")