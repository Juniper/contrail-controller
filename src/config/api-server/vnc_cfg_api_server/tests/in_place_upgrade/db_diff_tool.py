#!/usr/bin/python
# -*- coding: utf-8 -*-

from __future__ import print_function
from __future__ import unicode_literals
import argparse
import sys
import os
import cgitb
import json
import tempfile
import difflib
import subprocess
import select
import termios
import tty
import pty
from collections import defaultdict

_YELLOW = '\033[93m'
_GREEN = '\033[92m'
_RED = '\033[91m'
_COL_END = '\033[0m'

DEBUG = False


def _colored(msg, col):
    return '{}{}{}'.format(col, msg, _COL_END)


def _debug(msg):
    if DEBUG:
        print('\t' + _colored(msg, _YELLOW))


class MergeImpossible(Exception):
    pass


def merge_containers(cont1, cont2):
    if isinstance(cont1, dict) and isinstance(cont2, dict):
        # Dict<String, Object>
        merged = {}
        all_keys = set(cont1.keys()) | set(cont2.keys())
        for key in all_keys:
            merged[key] = merge_containers(
                cont1.get(key, None), cont2.get(key, None))
        return merged
    elif isinstance(cont1, list) and isinstance(cont2, list):
        # List<Object>
        if len(cont1) == 0 and len(cont2) == 0:
            return []
        common = merge_containers(
            None if len(cont1) == 0 else cont1[0],
            None if len(cont2) == 0 else cont2[0],
        )
        return [common]
    elif cont1 == cont2:
        return cont1
    elif cont1 == str(type(None)) or cont1 == None:
        return cont2
    elif cont2 == str(type(None)) or cont2 == None:
        return cont1
    else:
        numerics = [str(int), str(long)]
        if cont1 in numerics and cont2 in numerics:
            return str(int)

        strings = [str(str), str(unicode)]
        if cont1 in strings and cont2 in strings:
            return str(str)

        raise MergeImpossible("{} <> {}".format(cont1, cont2))


def prop_signature(prop):
    def sig_rec(value):
        if isinstance(value, dict):
            return {key: sig_rec(val) for key, val in value.items()}
        elif isinstance(value, list):
            return [sig_rec(val) for val in value]
        else:
            return str(type(value))

    parsed = json.loads(prop)

    return sig_rec(parsed)


def cassandra_schema(data):
    def obj_property(key):
        if key.startswith('prop:'):
            return key[5:]
        return

    objects = data['config_db_uuid']['obj_uuid_table']
    schemes = defaultdict(lambda: {})
    for obj in objects.values():
        obj_type = obj['type'][0]
        for k, v in obj.items():
            prop = obj_property(k)
            if prop:
                prop_sig = prop_signature(v[0])
                if schemes[obj_type].has_key(prop):
                    try:
                        merged = merge_containers(
                            schemes[obj_type][prop], prop_sig)
                    except MergeImpossible as e:
                        _debug(e)
                        continue
                    schemes[obj_type][prop] = merged
                else:
                    schemes[obj_type][prop] = prop_sig

    return schemes

def print_own_diff(left, right):
    delta = difflib.unified_diff(left.split('\n'), right.split('\n'), n=5)
    for line in delta:
        if line[0] == '+':
            print(_colored(line, _GREEN))
        elif line[0] == '0':
            print(_colores(line, _RED))
        else:
            print(line)

def print_diff(*strings):
    with tempfile.NamedTemporaryFile("w", delete=False) as fl, \
        tempfile.NamedTemporaryFile("w", delete=False) as fr:

        fnames = []
        for fp, data in zip([fl, fr], strings):
            fp.write(data)
            fnames.append(fp.name)

    cmd = ['git', 'diff', '--no-index'] + fnames
    # command = ['bash', '-c'] + ['{}'.format(" ".join(cmd))]
    # _debug(command)
    command = cmd

    # save original tty setting then set it to raw mode
    old_tty = termios.tcgetattr(sys.stdin)
    tty.setraw(sys.stdin.fileno())

    # open pseudo-terminal to interact with subprocess
    master_fd, slave_fd = pty.openpty()

    # use os.setsid() make it run in a new process group, or bash job control will not be enabled
    p = subprocess.Popen(command,
            preexec_fn=os.setsid,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            universal_newlines=True)

    while p.poll() is None:
        r, w, e = select.select([sys.stdin, master_fd], [], [])
        if sys.stdin in r:
            d = os.read(sys.stdin.fileno(), 10240)
            os.write(master_fd, d)
        elif master_fd in r:
            o = os.read(master_fd, 10240)
            if o:
                os.write(sys.stdout.fileno(), o)
        else:
            break

    # restore tty settings back
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_tty)

    # stdout = []
    # def master_read(fd):
    #     while True:
    #         data = os.read(fd, 1024)
    #         if not data:
    #             return data

    #         stdout.append(data)

    # pty.spawn(cmd, master_read)
    # for chunk in stdout:
    #     print(chunk, end='')


    # master_fd, slave_fd = pty.openpty()
    # wut = subprocess.Popen(
    #     cmd,
    #     stdin=slave_fd,
    #     stdout=slave_fd,
    #     close_fds=True,
    # ).communicate()

    # os.close(slave_fd)

    # if err:
    #     print(err)
    #     sys.exit(1)
    # else:
    #     print(out)

def compare_files(file_l, file_r):
    def json_schema(fp):
        raw_data = json.load(fp)
        cass_data = raw_data['cassandra']
        cass_schema = cassandra_schema(cass_data)
        return json.dumps(cass_schema, indent=2, sort_keys=True)

    with open(file_l) as fl, open(file_r) as fr:
        left = json_schema(fl)
        right = json_schema(fr)

        print_diff(left, right)

    return left, right


def main():
    cgitb.enable(format='text')
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs=2, type=str,
                        help='Compare two DB backup files.')
    # parser.add_argument('--no-color', default=False, action='store_true',
    #                     help='Turn off coloring.')
    parser.add_argument('--debug', default=False, action='store_true',
                        help='Debug mode.')
    args = parser.parse_args()
    if args.debug:
        global DEBUG
        DEBUG = True

    compare_files(*args.files)

    sys.exit(0)


def _do_tests():
    seq1 = [str(str),
            {'abc': str(int), 'def': str(int), 'ghi': str(bool)},
            'stu',
            {'jkl': {'mno': str(str), 'pqr': str(str)},
             'vwx': {'012': ['a', 'b']}}]

    seq2 = [str(str),
            {'jkl': {'mno': str(str), 'pqr': str(str)},
             'vwx': {'012': ['a', 'c']}},
            {'fed': str(float), 'ihg': str(str), 'abc': str(int)},
            'xyz']

    import ipdb
    ipdb.set_trace()
    seq_merge(seq1, seq2)


if __name__ == '__main__':
    main()
    # _do_tests()
