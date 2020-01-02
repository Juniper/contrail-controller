#!/usr/bin/python
# -*- coding: utf-8 -*-
# USAGE
# There are two available ways to check if schema has been changed.
# Both ways are mutually exclusive and '--release' flag has precedence.
# Signature:
# python schema_change_detector.py [versions|--release] [--no-color]
#
# Find changes between a current commit and last 3 releases:
#   python schema_change_detector.py 3
#
# Find changes between a current commit and the specific release:
#   python schema_change_detector.py --release 1910

from __future__ import print_function
from __future__ import unicode_literals

import argparse
import cgitb
import re
import subprocess
import sys

# TODO (pawel.zadrozny): remove six import and if-statement
#  after full migration to Py3
import six

if six.PY2:
    # FileNotFoundError is only available since Python 3.3
    FileNotFoundError = IOError
    from io import open

DEBUG = False


def _debug(cmd):
    if DEBUG:
        print('\nExecuted GIT command:')
        print(' ' * 4 + ' '.join(cmd))


def find_releases():
    cmd = ['git', 'branch', '--all', '--no-color', '--list', '*R*']
    _debug(cmd)
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
    cmd = ['git diff --stat --name-only --no-color '
           'remotes/github/R{}'.format(against_release)]
    _debug(cmd)
    out, err = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                shell=True).communicate()
    if err:
        print(err)
        sys.exit(1)
    return [f for f in out.decode('utf8').split('\n') if '.xsd' in f]


def get_file_data_from_release(release, path_to_edited_file):
    cmd = ['git show remotes/github/R{}:{}'.format(release,
                                                   path_to_edited_file)]
    _debug(cmd)
    out, err = subprocess.Popen(cmd,
                                stdout=subprocess.PIPE,
                                shell=True).communicate()
    if err:
        print(err)
        sys.exit(1)
    return out.decode('utf8')


def get_current_file_data(path_to_edited_file):
    with open(path_to_edited_file, 'r', encoding='utf8') as f:
        data = f.read()
    return data


def print_differences_between_files(release, path_to_edited_file, no_color):
    old_data = get_file_data_from_release(release, path_to_edited_file)
    new_data = get_current_file_data(path_to_edited_file)
    old_elements, new_elements = find_elements_with_resources(old_data,
                                                              new_data)
    if old_elements != new_elements:
        print('\n' + '-' * 100 +
              '\nFile {}:'.format(path_to_edited_file))
        compare_elements_resources(old_elements, new_elements, no_color)


def compare_elements_resources(old_elements, new_elements, no_color):
    keys = set(old_elements.keys()) | set(new_elements.keys())
    if no_color:
        green_start, red_start, color_end = '', '', ''
    else:
        green_start, red_start, color_end = '\033[92m', '\033[91m', '\033[0m'
    for key in keys:
        old = set(old_elements.get(key, []))
        new = set(new_elements.get(key, []))
        if old != new:
            print('\n{}'.format(key))
            if new - old:
                print('ADDED/MODIFIED RESOURCES\n{}{}{}'
                      .format(green_start, '\n'.join(new - old), color_end))
            if old - new:
                print('REMOVED/MODIFIED RESOURCES\n{}{}{}'
                      .format(red_start, '\n'.join(old - new), color_end))


def find_elements_with_resources(old_data, new_data):
    regex = re.compile(r'<xsd:element.*?(?=<xsd:element|</xsd:schema)',
                       re.DOTALL)
    new_elements = regex.findall(new_data)
    old_elements = regex.findall(old_data)
    regex_key = re.compile(r'<xsd:element.*?/>', re.DOTALL)
    regex_value = re.compile(r'<!--.*?#IFMAP.*?-->', re.DOTALL)
    new_elements_with_resources = {
        regex_key.search(element).group(0): regex_value.findall(element)
        for element in new_elements if '#IFMAP' in element}
    old_elements_with_resources = {
        regex_key.search(element).group(0): regex_value.findall(element)
        for element in old_elements if '#IFMAP' in element}
    return old_elements_with_resources, new_elements_with_resources


def main():
    cgitb.enable(format='text')
    parser = argparse.ArgumentParser()
    parser.add_argument('versions', metavar='N', nargs='?', type=int,
                        help='Check backwards for how many releases.')
    parser.add_argument('--release', dest='release', type=str,
                        help='Check against one specific release.')
    parser.add_argument('--no-color', default=False, action='store_true',
                        help='Turn off coloring.')
    parser.add_argument('--verbose', default=False, action='store_true',
                        help='Shows executed git commands.')
    args = parser.parse_args()
    if args.verbose:
        global DEBUG
        DEBUG = True
    releases = []
    if args.release is not None:
        releases.append(args.release.lstrip('R'))
    else:
        releases = find_releases()[:args.versions]
    for release in releases:
        diff = find_changes(against_release=release)
        print('Changes from release {}:\n{}\n'.format(
            release, '\n'.join(diff) if diff else 'No differences in schema'))
        for path_to_file in diff:
            print_differences_between_files(release=release,
                                            path_to_edited_file=path_to_file,
                                            no_color=args.no_color)
    sys.exit(0)


if __name__ == '__main__':
    main()
