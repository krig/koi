# Copyright (c) 2012 by Procera Networks, Inc. ("PROCERA")
#
# Permission to use, copy, modify, and/or distribute this software for
# any purpose with or without fee is hereby granted, provided that the
# above copyright notice and this permission notice appear in all
# copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND PROCERA DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL PROCERA BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
# OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import sys, os, subprocess, signal, re

TIMEOUT = 3
KOI = ['koi']

STATUS = ['status']
LOCAL = ['local']

for cfg in ["/etc/koi/koi.conf"]:
    if os.path.isfile(cfg):
        KOI += ['-f', cfg]

def devmode(koi, configs):
    global KOI
    KOI = [koi]
    for cfg in configs:
        if os.path.isfile(cfg):
            KOI += ['-f', cfg]

class TimeOutError(Exception):
    pass

def alarm_handler(signum, frame):
    raise TimeOutError

def _koi(*cmd):
    signal.signal(signal.SIGALRM, alarm_handler)
    signal.alarm(TIMEOUT)
    line = KOI + list(cmd)
    output = subprocess.Popen(line, stdout=subprocess.PIPE).communicate()[0]
    signal.alarm(0)
    return output

def _parse_services(line):
    """echo:Promoted:none:+, mako:Promoted:none:+, righto:Promoted:none:+, slowstart:Promoted:none:+, stufftodo:Promoted:none:+"""
    if line.startswith('['):
        line = line[1:-1]
    lines = [l.strip() for l in line.split(',')]
    lines = [l.split(':') for l in lines]
    lines = [{l[0]:l[1:]} for l in lines]
    return lines

def _parse_node(lines, i):
    n = {}
    while i < len(lines):
        if lines[i] == '[end]':
            break
        if lines[i].find(':') >= 0:
            k, v = lines[i].split(':', 1)
            if k.strip() == 'services':
                n['services'] = _parse_services(v.strip())
            else:
                n[k.strip()] = v.strip()
        i = i + 1
    return i, n

def status():
    st = _koi(*STATUS)
    lines = _strip_ret_head(st).split('\n')
    if len(lines) > 0:
        elector_state = {}
        nodes = {}
        i = 0
        while i < len(lines):
            if len(lines[i]):
                if lines[i].find(':') >= 0:
                    k, v = lines[i].split(':', 1)
                    elector_state[k.strip()] = v.strip()
            else:
                break
            i = i + 1
        i = i + 1 # skip empty line
        while i < len(lines):
            if lines[i] == '[node]':
                i, node = _parse_node(lines, i+1)
                nodes[node['uuid']] = node
            i = i + 1
        return {'elector':elector_state, 'nodes': nodes}
    else:
        return {}

NIL_UUID = '00000000-0000-0000-0000-000000000000'

def status_str():
    st = status()
    if 'elector' not in st:
        return ""

    ret = "Maintenance mode: %s\nManual master mode: %s\n" % (st['elector']['maintenance'],
                                                              st['elector']['manual-master'])
    if st['elector']['master'] != NIL_UUID:
        n = st['nodes'][st['elector']['master']]
        ret += "Master: %s (%s)\n" % (n['name'], n['uuid'])
    if st['elector']['target'] != NIL_UUID:
        ret += "Target master: %s\n" % (st['elector']['target'])

    ret += "\n"
    first = True
    for uuid, n in st['nodes'].iteritems():
        if not first:
            ret += "\n"
        first = False
        ret += "%s (%s):\n" % (n['name'], uuid)
        ret += "   Address: %s\n" % (n['addr'])
        if 'target-action' in n:
            ret += "   State: %s (%s)\n" % (n['state'], n['target-action'])
        else:
            ret += "   State: %s\n" % (n['state'])
        ret += "   Updated: %s\n" % (n['seen'])
        if 'flags' in n:
            ret += "   Flags: %s\n" % (n['flags'])
        if 'lastfailed' in n and not n['lastfailed'].startswith('1984'):
            ret += "   Last Failed: %s\n" % (n['lastfailed'])
        if 'services' in n and len(n['services']) > 0:
            ret += "   Services:\n"
            for svc in n['services']:
                if len(svc.keys()) >= 1:
                    k = svc.keys()[0]
                    service = svc[k]
                    if len(service) > 0:
                        ret += "      %s (%s)\n" % (k, svc[k][0])
                    else:
                        ret += "      %s\n" % (k)
    return ret

def local():
    st = _koi(*LOCAL)
    froms = _get_from_chain(st)
    lines = _strip_ret_head(st).split('\n')
    ret = {}
    for line in lines:
        if line.find(':') >= 0:
            k, v = line.split(':', 1)
            ret[k.strip()] = v.strip()
    return ret

def local_uuid():
    lc = local()
    return lc['uuid']

def _get_from_chain(ret):
    froms = []
    ret = ret.split('\n')
    while ret[0].startswith('From: ') or ret[0].startswith('Redirect: '):
        if ret[0].startswith('From: '):
            froms.append(ret[0][6:].strip())
        ret = ret[1:]
    return froms

def _strip_ret_head(ret):
    if len(ret):
        ret = ret.split('\n')
        while ret[0].startswith('From: ') or ret[0].startswith('Redirect: '):
            ret = ret[1:]
        ret = '\n'.join(ret)
    return ret.strip()

def make_master(uuid):
    return _strip_ret_head(_koi('promote', uuid))

def promote(uuid):
    return _strip_ret_head(_koi('promote', uuid))

def tree():
    return _strip_ret_head(_koi('tree'))

def demote():
    return _strip_ret_head(_koi('demote'))

def elect():
    return _strip_ret_head(_koi('elect'))

def recover(nd=None):
    if nd:
        return _strip_ret_head(_koi('recover', nd))
    else:
        return _strip_ret_head(_koi('recover'))

def failures():
    ret = _strip_ret_head(_koi('failures'))
    # don't return anything with zero failures
    if ret.startswith('Last 0 failures'):
        return ''
    return ret

def start(nd=None):
    if nd:
        return _strip_ret_head(_koi('start', nd))
    else:
        return _strip_ret_head(_koi('start'))

def stop(nd=None):
    if nd:
        return _strip_ret_head(_koi('stop', nd))
    else:
        return _strip_ret_head(_koi('stop'))

def maintenance(onoff):
    if onoff in [True, 1]:
        onoff = 'on'
    elif onoff in [False, 0]:
        onoff = 'off'
    if onoff not in ['on', 'off']:
        return None
    return _strip_ret_head(_koi('maintenance', onoff))

def reconfigure(nd=None):
    if nd:
        return _strip_ret_head(_koi('reconfigure', nd))
    else:
        return _strip_ret_head(_koi('reconfigure'))

if __name__ == "__main__":
    if len(sys.argv) > 1:
        if sys.argv[1] == 'status':
            print status()
        elif sys.argv[1] == 'local':
            print local()
        else:
            print "Unknown command"
            sys.exit(1)
    else:
        print status()
