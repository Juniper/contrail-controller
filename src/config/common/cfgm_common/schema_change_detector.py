#!/usr/bin/python
# USAGE STEPS
# From controller directory run script and pass
# python schema_change_detector.py 3
# python schema_change_detector.py --release 1910


from __future__ import print_function
from __future__ import unicode_literals

import argparse
import cgitb
import subprocess
import sys
import re

reload(sys)
sys.setdefaultencoding('UTF8')


def find_releases():
    cmd = ['git', 'branch', '--all', '--no-color', '--list', '*R*']
    out, err = subprocess.Popen(cmd, stdout=subprocess.PIPE).communicate()
    if err:
        print(err)
        sys.exit(1)

    regex = re.compile(r'^.*?/R([0-9]{4})$')

    releases = set()
    for branch in out.split('\n'):
        match = regex.search(branch)
        if match is not None:
            releases.add(int(match.group(1)))
    return sorted(releases, reverse=True)


def find_changes(against_release):
    cmd = ['git diff --stat --name-only --patience --no-color '
           '--ignore-cr-at-eol --ignore-space-at-eol '
           '--ignore-space-change --ignore-blank-lines '
           'remotes/github/R{}'.format(against_release)]
    out, err = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                shell=True).communicate()
    if err:
        print(err)
        sys.exit(1)
    return [f for f in out.split('\n') if '.xsd' in f]


def main():
    cgitb.enable(format='text')

    parser = argparse.ArgumentParser()
    parser.add_argument('versions', metavar='N', nargs='?',
                        type=int, help='Check backwards for how many releases')
    parser.add_argument('--release', dest='release', type=str,
                        help='Check against one specific release')
    args = parser.parse_args()

    releases = []
    if args.release is not None:
        releases.append(args.release)
    else:
        releases = find_releases()[:args.versions]

    for release in releases:
        diff = find_changes(against_release=release)
        print('Changes from release {}:\n{}\n'.format(
            release, '\n'.join(diff) if diff else 'No differences in schema'))
    sys.exit(0)


if __name__ == '__main__':
    main()
