# vim: set expandtab shiftwidth=4 fileencoding=UTF-8:

# Copyright 2016 Netronome Systems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import absolute_import

import copy
import json


def serialize(o):
    return {
        'type': type(o).__name__,
        'vars': copy.deepcopy(o if isinstance(o, dict) else vars(o)),
    }


def _pop_assert(what, d, k, expected_type):
    value = d.pop(k)
    if not isinstance(value, expected_type):
        raise ValueError(
            'incorrect type for {} field "{}": expected {}, got {}'.
            format(what, k, expected_type.__name__, type(value).__name__)
        )
    return value


def _assert_no_keys(what, d):
    if d:
        raise ValueError(
            '{}: unknown key(s) in input: {}'.format(
                what, ', '.join(map(lambda s: '"{}"'.format(s), d.iterkeys()))
            )
        )


def _missing_required(what, e):
    return ValueError(
        '{}: missing required key "{}"'.format(what, e.args[0])
    )


def deserialize(s, allowed_types=None):
    what = 'JSON input'

    if isinstance(s, basestring):
        j = json.loads(s)
    else:
        j = copy.deepcopy(s)

    if not isinstance(j, dict):
        raise ValueError('expected {} to be a dict'.format(what))

    try:
        t = _pop_assert(what, j, 'type', basestring)
        v = _pop_assert(what, j, 'vars', dict)

        _assert_no_keys(what, j)

        if allowed_types is not None and t not in allowed_types:
            raise ValueError(
                'JSON input type {} is not allowed; valid types are: {}'.
                format(
                    t,
                    ', '.join(map(lambda s: '"{}"'.format(s), allowed_types))
                )
            )

        return t, v

    except KeyError as e:
        raise _missing_required(what, e)


def _apply_changes(o, changes):
    for k, v in changes.iteritems():
        getattr(o, k)
        setattr(o, k, v)


def apply_delta(o, delta):
    what = 'JSON delta'

    if not isinstance(delta, dict):
        raise ValueError('expected {} to be a dict'.format(what))

    delta = copy.deepcopy(delta)
    try:
        changes = _pop_assert(what, delta, 'changes', dict)
        if not delta:
            _apply_changes(o, changes)
            return

        _assert_no_keys(what, delta)

    except KeyError as e:
        raise _missing_required(what, e)


def _must_support_keys(what, o):
    return ValueError(
        '{} must support a keys() method (type: {})'.format(
            what, type(o).__name__
        )
    )


def create_delta(orig, new):
    try:
        ko = orig.keys()
    except AttributeError:
        raise _must_support_keys('orig', orig)

    try:
        kn = new.keys()
    except AttributeError:
        raise _must_support_keys('new', new)

    if ko != kn:
        raise ValueError('mismatched set of keys are not supported')

    return {
        'changes': {k: new[k] for k in orig.iterkeys() if orig[k] != new[k]}
    }
