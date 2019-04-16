#!/usr/bin/python

import argparse
import pickle
import http.client
from ipaddress import ip_address
from sys import exit, argv
from os import remove

TOKENDUMP_PATH = 'userdumps.pickle'

# --- argument parsing ----------------------------------------
desc = ''' client for super secure logserver v0.1
 (for subcommand help type {} sub --help) '''.format(argv[0])
epi = '''sample usage:
    cat logfile | {} sendlogs some_user 127.0.0.1 1234
    {} getlogs some_user 127.0.0.1 1234
    {} showtoken some_user '''.format(argv[0], argv[0], argv[0])

getlogs_help = 'get either all or -nr lines'
sendlogs_help = 'input via stdin or file'
showtoken_help = 'show dumped tokens from server responses'
deltoken_help = 'delete dumped token for one or all users'
addtoken_help = 'add token for user (no validity checks done)'

deltoken_p_desc = ''' WARNING: without valid token you cant access ressources on server.
 Make sure to backup token using showtoken command before.
 There will be no way to restore your token once deleted'''

common_args = argparse.ArgumentParser(add_help=False)
common_args.add_argument('user', type=str, help='username for logserver')
common_args.add_argument('ip', type=ip_address, help='address of logserver')
common_args.add_argument('port', type=int, help='port of logserver')

parser = argparse.ArgumentParser(description=desc, epilog=epi, 
    formatter_class=argparse.RawDescriptionHelpFormatter)
subparsers = parser.add_subparsers(dest='subcommand')
subparsers.required = True

getlogs_p = subparsers.add_parser('getlogs', parents=[common_args], help=getlogs_help,
    description=' '+getlogs_help, formatter_class=argparse.RawDescriptionHelpFormatter)
getlogs_p.add_argument('-nr', '--nr-entries', type=int, metavar='NR', default=0,
    help='just like tail -n')
getlogs_p.add_argument('-f', '--outfile', type=argparse.FileType('wb'), default='-',
    help='specify output. Default is stdout')

sendlogs_p = subparsers.add_parser('sendlogs', parents=[common_args], help=sendlogs_help,
    description=' '+sendlogs_help, formatter_class=argparse.RawDescriptionHelpFormatter)
sendlogs_p.add_argument('-f', '--infile', type=argparse.FileType('r'), default='-',
    help='file to read the log entries from (default stdin)')

showtoken_p = subparsers.add_parser('showtoken', help=showtoken_help,
    description=' '+showtoken_help, formatter_class=argparse.RawDescriptionHelpFormatter)
showtoken_p.add_argument('user', type=str, help='username to check for dumped token')

deltoken_p = subparsers.add_parser('deltoken', help=deltoken_help,
    description=deltoken_p_desc, formatter_class=argparse.RawDescriptionHelpFormatter)
deltoken_p.add_argument('user', nargs='?', type=str, help='user for wich to delete dumped token')
deltoken_p.add_argument('--all', action='store_true', help='delete all dumped tokens')

addtoken_p = subparsers.add_parser('addtoken', help=addtoken_help,
    description=' '+addtoken_help, formatter_class=argparse.RawDescriptionHelpFormatter)
addtoken_p.add_argument('user', type=str, help='user for wich this token is signed')
addtoken_p.add_argument('token', type=str, help='JWT in base64url (parts separated by ".")')
addtoken_p.add_argument('-f', '--force', action='store_true', help='overwrite if exists')

def_args = ['getlogs', '127.0.0.1', '65333', 'new_client']
args = parser.parse_args()
# --- end of argument parsing ----------------------------------

token_dumps = dict()

# get a jwt from server (could also post to non-existing target)
def add_user(user, ip, port):
    print("[*] requesting server to add user", user)

    try:
        conn = http.client.HTTPConnection(ip, port=port, timeout=10)
        conn.request('GET', '/adduser?name='+user)

        with conn.getresponse() as res:
            if (res.status == 200):
                token_str = str(res.read(), 'ascii')
                print('[*] token for user {}:'.format(user))
                print('---------------------------------------------------')                
                print(token_str)
                print('--- the client will remember this token for you ---')

                token_dumps[args.user] = token_str

                with open(TOKENDUMP_PATH, 'wb') as dumpfile:
                    pickle.dump(token_dumps, dumpfile)
            else:
                print('[ERROR] {} {}: {}'.format(res.status, res.reason, str(res.read(), "ascii")))
                exit(1)
    except Exception as e:
        print(e)
        exit(1)


def get_logs(user, ip, port, nr_lines, outfile):
    print('[*] getting logs for user', user)

    try:
        token = token_dumps[user]
    except KeyError:
        print('[ERROR] no dumped token found for user', user)
        print('[INFO] did u delete it? you could add one with addtoken command, but it wont be signed by server then')
        exit(-1)

    header = {'Authorization':'Bearer '+(token or '')}

    try:
        conn = http.client.HTTPConnection(ip, port=port, timeout=10)
        conn.request('GET', '/{}?entries={}'.format(user, nr_lines), headers=header)

        with conn.getresponse() as res:
            if (res.status == 200):
                print('[*] recieved requested logs')
                with outfile as of:
                    if outfile.isatty():
                        of.write(str(res.read(), 'ascii'))
                    else:
                        of.write(res.read())
            else:
                print('[ERROR] {} {}: {}'.format(res.status, res.reason, str(res.read(), "ascii")))
                exit(1)
    except Exception as e:
        print(e)
        exit(1)


def send_logs(user, ip, port, infile):
    print('[*] sending logs as user', user)

    try:
        token = token_dumps[user]
    except KeyError:
        print('[*] no dumped token found for user', user)
        add_user(args.user, ip, port)

    header = {'Authorization':'Bearer '+token}

    try:
        conn = http.client.HTTPConnection(ip, port=port, timeout=10)

        if infile.isatty():
            print('[*] expecting input from stdin:')
            print('[INFO] type sth and end with ctrl-d (or use -f next time)')

        body = infile.read()
        if not body[-1] == '\n':
            body += '\n'

        conn.request('POST', '/'+user, headers=header, body=body)
        with conn.getresponse() as res:
            if (res.status == 200):
                print('[*] logs sent successfully')
            else:
                print('[ERROR] {} {}: {}'.format(res.status, res.reason, str(res.read(), "ascii")))
                exit(1)
    except Exception as e:
        print(e)
        exit(1)


# first restore dumped tokens
try:
    with open(TOKENDUMP_PATH, 'rb') as dumps:
        token_dumps = pickle.load(dumps)
    print('[*] found token dumps')
except FileNotFoundError:
    print('[*] no dumped tokens found')

# take action only on specified subcommand
if args.subcommand == 'sendlogs':
    send_logs(args.user, args.ip.exploded, args.port, args.infile)
elif args.subcommand == 'getlogs':
    get_logs(args.user, args.ip.exploded, args.port, args.nr_entries, args.outfile)
elif args.subcommand =='showtoken':
    if args.user in token_dumps:
        print('[*] token for user {}:'.format(args.user))
        print('---------------------------------------------------')                
        print(token_dumps[args.user])
        print('---------------------------------------------------')
    else:
        print('[*] no token found for user', args.user,'¯\\(°_o)/¯')
elif args.subcommand == 'deltoken':
    wipe = False
    if args.all:
        wipe = True
    if args.user and token_dumps and args.user in token_dumps:
        token = token_dumps[args.user]
        print('[*] removed token dump for user '+args.user)
        del token_dumps[args.user]
        if len(token_dumps) == 0:
            wipe = True
    if wipe:
        try:
            remove(TOKENDUMP_PATH)
        except FileNotFoundError: pass
    else:
        with open(TOKENDUMP_PATH, 'wb') as dumpfile:
            pickle.dump(token_dumps, dumpfile)
elif args.subcommand == 'addtoken':
    if args.user in token_dumps and not args.force:
        print('[ERROR] there already is a dumped token for user', args.user)
        exit(-1)
    token_dumps[args.user] = args.token
    with open(TOKENDUMP_PATH, 'wb') as dumpfile:
            pickle.dump(token_dumps, dumpfile)
    print('[*] token added for user', args.user)
           
