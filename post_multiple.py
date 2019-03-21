#!/usr/bin/python

# test script for ctf logserver
# sends POST with log entries, grabs JWT from response and sends another POST with JWT

import http.client
from sys import argv, exit

def usage():
    print("usage: {} user [times=2]".format(argv[0]))
    print("make sure that there are no logfiles for user to test POSTing more logentries with recieved JWT")
    exit()

if len(argv) != 2 and len(argv) != 3:
    usage()

user = ""
times = 2

try:
    user = str(argv[1])
    if len(argv) == 3:
        times = int(argv[2])
        if times < 2:
            usage()
except:
    usage()

# first get a JWT by posting some logentries

print("sending POST /"+user)
conn = http.client.HTTPConnection("localhost", port=65333)
body = "post_multiple.py: entry1\npost_multiple.py: entry2\n"
headers={"Content-Length": len(body)}
try:
    conn.request("POST", "/"+user, body=body, headers=headers)
    with conn.getresponse() as res1:
        if (res1.status == 200):
            jwt1 = res1.read()
            print("got 200 OK and a JWT")
        else:
            print("unexpected response:", res1.status, res1.reason, "cant continue")
            print("are you sure that no logs exist for given user?")
            exit()
except Exception as e:
    print("error:", e)
    exit()

# send another POST request with recieved JWT (or multiple other if times set)

for _ in range(times-1):
    print("sending second POST /"+user+" with recieved JWT")
    conn = http.client.HTTPConnection("localhost", port=65333)
    body = "post_multiple.py: entry3\npost_multiple.py: entry4\n"
    headers["Authorization"] = "Bearer " + str(jwt1, "ascii")
    headers["Content-Length"] = len(body)
    try:
        conn.request("POST", "/"+user, body=body, headers=headers)
        with conn.getresponse() as res2:
            if (res2.status == 200):
                jwt2 = res2.read()
                print("got 200 OK and another JWT, same: ", True if jwt1 == jwt2 else False)
            else:
                print("unexpected response:", res2.status, res2.reason, "cant continue")
                exit()
    except Exception as e:
        print("error:", e)
        exit()

print("test succeeded")
