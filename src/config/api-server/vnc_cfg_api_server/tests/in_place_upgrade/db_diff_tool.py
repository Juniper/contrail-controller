#!/usr/bin/python
# -*- coding: utf-8 -*-

from __future__ import print_function
from __future__ import unicode_literals

import argparse
import cgitb
from collections import defaultdict
import difflib
from functools import wraps
import json
import sys

_YELLOW = '\033[93m'
_GREEN = '\033[92m'
_RED = '\033[91m'
_COL_END = '\033[0m'

DEBUG = False
NO_COLOR = False


def _colored(msg, col):
    if NO_COLOR:
        return msg
    return '{}{}{}'.format(col, msg, _COL_END)


def _debug(msg):
    if DEBUG:
        print('\t' + _colored(msg, _YELLOW))


class MergeImpossible(Exception):
    pass


_NUMERICS_T = [str(int), str(long)]  # noqa:F821
_STRINGS_T = [str(str), str(unicode)]  # noqa:F821
_CONSTRUCTORS = {
    str(int): int,
    str(long): int,  # noqa:F821
    str(str): str,
    str(unicode): str,  # noqa:F821
    str(bool): bool,
    str(None): lambda *_: None,
}


def python2_types_coerce(func):
    """Python2 has long/int, unicode/string etc.; we don't like that."""

    @wraps(func)
    def wrapper(*args, **kwargs):
        tp = func(*args, **kwargs)
        if tp in _NUMERICS_T:
            return str(int)
        elif tp in _STRINGS_T:
            return str(str)
        else:
            return tp
    return wrapper


def merge_containers(cont1, cont2):
    """Recursively merges collections. Collapses lists into 1-item lists."""
    if isinstance(cont1, dict) and isinstance(cont2, dict):
        # Dict<String, T>
        merged = {}
        all_keys = set(cont1.keys()) | set(cont2.keys())
        for key in all_keys:
            merged[key] = merge_containers(
                cont1.get(key, None), cont2.get(key, None))
        return merged
    elif isinstance(cont1, list) and isinstance(cont2, list):
        # assumption: List<T> for some T
        if len(cont1) == 0 and len(cont2) == 0:
            return []
        common = merge_containers(
            None if len(cont1) == 0 else cont1[0],
            None if len(cont2) == 0 else cont2[0],
        )
        return [common]
    elif cont1 == cont2:
        return cont1
    elif cont1 == str(type(None)) or cont1 is None:
        return cont2
    elif cont2 == str(type(None)) or cont2 is None:
        return cont1
    else:
        if cont1 in _NUMERICS_T and cont2 in _NUMERICS_T:
            return cont1

        if cont1 in _STRINGS_T and cont2 in _STRINGS_T:
            return cont1

        # encountered two nodes with incompatible types
        raise MergeImpossible("{} <> {}".format(cont1, cont2))


def construct_type_from_string(func):

    @wraps(func)
    def wrapper(*args, **kwargs):
        value = func(*args, **kwargs)
        if isinstance(value, str) or isinstance(value, unicode):  # noqa:F821
            value = _CONSTRUCTORS.get(value, lambda _: value)(0)
        return value

    return wrapper


@construct_type_from_string
def update_container_with_schema(cont, update):
    """Recursively updates values of $cont with values from $update."""
    if update is None:
        return cont

    if cont is None:
        if isinstance(cont, dict):
            return update_container_with_schema({}, update)
        if isinstance(cont, list):
            return update_container_with_schema([], update)
        else:
            return update

    if isinstance(cont, dict) and isinstance(update, dict):
        updated = {}
        all_keys = set(cont.keys()) | set(update.keys())
        for key in all_keys:
            updated[key] = update_container_with_schema(
                cont.get(key, None), update.get(key, None))
        return updated

    elif isinstance(cont, list) and isinstance(update, list):
        return cont if len(update) == 0 else \
            [update_container_with_schema(None, update[0])]
    else:
        # there are no tuples in this scenario, so what is left are primitives
        return update


def property_signature(prop):
    """Forgetful functor clearing out values and leaving only their types."""

    @python2_types_coerce
    def sig_rec(value):
        if isinstance(value, dict):
            return {key: sig_rec(val) for key, val in value.items()}
        elif isinstance(value, list):
            # assumption: List<T> for some T
            return [] if len(value) == 0 else [sig_rec(value[0])]
        else:
            return str(type(value))

    parsed = json.loads(prop)
    return sig_rec(parsed)


def iterate_over_objects(data):
    """Iterate over all objects that are relevant to the task at hand."""
    objects = data['config_db_uuid']['obj_uuid_table']
    for obj in objects.values():
        obj_type = obj['type'][0]  # e.g. '"virtual_network"'
        obj_type = obj_type.strip('"')
        yield obj_type, obj


def cassandra_schema(data):
    """Extract objects from Cassandra dump."""
    def obj_property(key):
        if key.startswith('prop:'):
            return key[5:]
        return

    schemes = defaultdict(lambda: {})
    for obj_type, obj in iterate_over_objects(data):
        for k, v in obj.items():
            prop = obj_property(k)
            if prop:
                prop_sig = property_signature(v[0])

                # add to obj schema or merge with existing entry
                if prop in schemes[obj_type]:
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


def conform_cassandra_data_to_schema(data, scheme_upgrade):
    for obj_type, obj in iterate_over_objects(data):
        upgrade = scheme_upgrade.get(obj_type, {})
        for k, schema in upgrade.items():
            prop_key = "prop:{}".format(k)
            value = obj.get(prop_key, None)
            if value:
                updated = update_container_with_schema(value, schema)
                obj[prop_key] = updated
            else:
                new_val = update_container_with_schema(None, schema)
                obj[prop_key] = new_val


def print_simple_diff(left_str, right_str, **kwargs):
    """Use difflib and ANSI color codes for output. May not be pretty."""
    delta = difflib.unified_diff(
        left_str.split('\n'), right_str.split('\n'), n=5)
    for line in delta:
        if line[0] == '+':
            print(_colored(line, _GREEN))
        elif line[0] == '0':
            print(_colored(line, _RED))
        else:
            print(line)


def extract_schema_from_file(fp, **kwargs):
    raw_data = json.load(fp)
    cass_data = raw_data['cassandra']
    cass_schema = cassandra_schema(cass_data)

    if kwargs['schema_save']:
        filename = "{}.SCHEMA".format(fp.name)
        with open(filename, "w") as fw:
            json.dump(cass_schema, fw, indent=2, sort_keys=True)

    return cass_schema


def compare_files(file_l, file_r, **kwargs):
    """Compare two DB dumps by diffing simplified Cassandra contents."""
    with open(file_l) as fl, open(file_r) as fr:
        scheme_jsons = [json.dumps(
            extract_schema_from_file(fp, **kwargs),
            indent=2,
            sort_keys=True
        ) for fp in [fl, fr]]

        print_simple_diff(*scheme_jsons, **kwargs)


def json_upgrade(file_l, file_r, **kwargs):
    """
    Update the newest DB dump with data from the previous one.

    Uses the schema difference for reference
    """
    with open(file_l) as fl, open(file_r) as fr:
        [schm_old, schm_new] = [extract_schema_from_file(fp, **kwargs)
                                for fp in [fl, fr]]

        # there were two possible options, we went with the first one:
        # - old data with new schema
        # - new data with old schema
        fl.seek(0)
        data = json.load(fl)  # load old data
        conform_cassandra_data_to_schema(data['cassandra'], schm_new)

        with open("{}.UPGRADE".format(fl.name), "w") as fw:
            json.dump(data, fw, indent=2, sort_keys=True)


def main():
    cgitb.enable(format='text')
    parser = argparse.ArgumentParser(description="""
        Compares two DB backup files and displays the difference between them.
        Optionally, generates a DB dump for upgrade testing.""")
    parser.add_argument('file_older', type=str,
                        help="n-1'th release DB dump")
    parser.add_argument('file_newer', type=str,
                        help="n'th release DB dump")

    parser.add_argument('--json-upgrade', default=False, action='store_true',
                        help="Generate JSON for n-1 -> n upgrade.\n" +
                        "It will be a sum of all the incoming changes from " +
                        "n'th and contents of n-1'th. I.e. whole n-1'th " +
                        "JSON + all the green stuff from the git-diff.")
    parser.add_argument('--schema-save', default=False, action='store_true',
                        help="Save computed JSON schemas to files.")
    parser.add_argument('--no-color', default=False, action='store_true',
                        help='Disable colors in output')
    parser.add_argument('--debug', default=False, action='store_true',
                        help='Debug mode')
    args = parser.parse_args()
    kwargs = vars(args)

    if args.debug:
        global DEBUG
        DEBUG = True

    if kwargs['no_color']:
        global NO_COLOR
        NO_COLOR = True

    if kwargs['json_upgrade']:
        json_upgrade(kwargs['file_older'], kwargs['file_newer'], **kwargs)
    else:
        compare_files(kwargs['file_older'], kwargs['file_newer'], **kwargs)

    sys.exit(0)


if __name__ == '__main__':
    main()
