#!/usr/bin/python

try:
    import argparse
    import pickle
    import http.client
    from ipaddress import ip_address
    from sys import exit, argv
except ImportError as e:
    print(e)

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

common_args = argparse.ArgumentParser(add_help=False)
common_args.add_argument('user', type=str, help='username for logserver')
common_args.add_argument('ip', type=ip_address, help='address of logserver')
common_args.add_argument('port', type=int, help='port of logserver')
common_args.add_argument('-n', '--new-user', action='store_true', 
    help='create a new user on server. This will override dumped token for this user if exists')

parser = argparse.ArgumentParser(description=desc, epilog=epi, 
    formatter_class=argparse.RawDescriptionHelpFormatter)
subparsers = parser.add_subparsers(dest='subcommand')

getlogs_p = subparsers.add_parser('getlogs', parents=[common_args], help=getlogs_help)
getlogs_p.add_argument('-nr', '--nr-entries', type=int, metavar='NR', default=0,
    help='specify how many of the most recent entries you want to get (just like tail -n)')
getlogs_p.add_argument('-f', '--outfile', type=argparse.FileType('wb'), default='-',
    help='specify output. Default is stdout')

sendlogs_p = subparsers.add_parser('sendlogs', parents=[common_args], help=sendlogs_help)
sendlogs_p.add_argument('-f', '--infile', type=argparse.FileType('r'), default='-',
    help='specify a file to read the log entries from. Default is stdin')

showtoken_p = subparsers.add_parser('showtoken', help=showtoken_help)
showtoken_p.add_argument('user', type=str, help='username to check for dumped token')

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

                with open('userdumps.pickle', 'wb') as dumpfile:
                    pickle.dump(token_dumps, dumpfile)
            else:
                print('[ERROR] {} {}: {}'.format(res.status, res.reason, str(res.read(), "ascii")))
                exit(1)
    except Exception as e:
        print(e)
        exit(1)


def get_logs(user, ip, port, nr_lines, outfile):
    print('[*] getting logs for user', user)
    token = token_dumps[user]
    header = {'Authorization':'Bearer '+token}

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
    token = token_dumps[user]
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


# restore dumped tokens
try:
    with open('userdumps.pickle', 'rb') as dumps:
        token_dumps = pickle.load(dumps)
    print('[*] found token dumps')
except FileNotFoundError:
    print('[*] no dumped tokens found')

if args.subcommand =='showtoken':
    if args.user in token_dumps:
        print('[*] token for user {}:'.format(args.user))
        print('---------------------------------------------------')                
        print(token_dumps[args.user])
        print('---------------------------------------------------')
        exit(0)
    else:
        print('[*] no token found for user', args.user,'¯\(°_o)/¯')
        exit(1)
          
if args.new_user:
    add_user(args.user, args.ip.exploded, args.port)
elif not args.user in token_dumps:
    print('[*] no token found for user', args.user)
    add_user(args.user, args.ip.exploded, args.port)

if args.subcommand == 'sendlogs':
    send_logs(args.user, args.ip.exploded, args.port, args.infile)
elif args.subcommand == 'getlogs':
    get_logs(args.user, args.ip.exploded, args.port, args.nr_entries, args.outfile)
           
