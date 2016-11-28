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
import unittest

from netronome.vrouter import config_editor

from nova.virt.libvirt.config import LibvirtConfigGuestInterface


class TestConfigEditor(unittest.TestCase):
    def test_serialize(self):
        """
        Make sure that it is possible to serialize a
        LibvirtConfigGuestInterface.
        """

        gi = LibvirtConfigGuestInterface()
        gs = config_editor.serialize(gi)
        json.dumps(gs)  # make sure it is JSON dumpable
        self.assertTrue('type' in gs)
        self.assertTrue('vars' in gs)

        # Make sure that the type of "vars" is dict, not string. The client is
        # supposed to call json.dumps() on the overall structure, so
        # serialize() should not call it on "vars".
        self.assertEqual(type(gs['vars']), dict)

        # Change an object after serialization, "vars" from an old call to
        # "serialize" should NOT change. (This was broken in the original code
        # written on 2016-06-28.)
        self.assertIsNone(gs['vars']['driver_name'])
        gi.driver_name = 'some driver name'
        self.assertIsNone(
            gs['vars']['driver_name'],
            'changing object should not retroactively affect serialized format'
        )

    def test_serialize_dict(self):
        """
        Make sure that it is possible to serialize a dict (that does not have a
        __dict__ attribute, therefore it cannot be used with vars()).
        """

        d = {'coffee': True}
        ds = config_editor.serialize(d)
        json.dumps(ds)  # make sure it is JSON dumpable
        self.assertTrue('type' in ds)
        self.assertTrue('vars' in ds)
        self.assertTrue('coffee' in ds['vars'])
        self.assertEqual(ds['vars']['coffee'], True)

    def test_deserialize(self):
        # main point: coverage of error handling code

        with self.assertRaisesRegexp(ValueError, 'to be a dict'):
            config_editor.deserialize('[]')

        with self.assertRaisesRegexp(ValueError, 'missing required.*type'):
            config_editor.deserialize('{}')

        with self.assertRaisesRegexp(
            ValueError, 'incorrect type.*type.*expected.*string'
        ):
            config_editor.deserialize('{"type":[]}')

        with self.assertRaisesRegexp(ValueError, 'missing required.*vars'):
            config_editor.deserialize('{"type":"blah"}')

        with self.assertRaisesRegexp(
            ValueError, 'incorrect type.*vars.*expected.*dict'
        ):
            config_editor.deserialize('{"type":"blah","vars":1}')

        t, v = config_editor.deserialize('{"type":"blah","vars":{}}')
        self.assertEqual(t, 'blah')
        self.assertEqual(v, {})

        with self.assertRaisesRegexp(
            ValueError, 'unknown key.*in input.*xtra2'
        ):
            config_editor.deserialize(
                '{"type":"blah","vars":{},"xtra1":4,"xtra2":{}}'
            )

        # should not throw
        config_editor.deserialize(
            '{"type":"blah","vars":{}}',
            allowed_types=('blah', 'LibvirtConfigGuestInterface')
        )

        with self.assertRaisesRegexp(
            ValueError, 'JSON input type.*blah.*not.*allowed'
        ):
            config_editor.deserialize(
                '{"type":"blah","vars":{}}',
                allowed_types=('LibvirtConfigGuestInterface',)
            )

        t, v = config_editor.deserialize(
            '{"type":"blah","vars":{"a":1,"b":[2,3],"c":null}}',
            allowed_types=('blah',)
        )
        self.assertEqual(t, 'blah')
        self.assertEqual(v, {'c': None, 'b': [2, 3], 'a': 1})

    def test_apply_changes(self):
        gi = LibvirtConfigGuestInterface()
        self.assertIsNone(gi.net_type)
        config_editor._apply_changes(gi, {'net_type': 'hostdev'})
        self.assertEqual(gi.net_type, 'hostdev')

    def test_apply_changes_error(self):
        gi = LibvirtConfigGuestInterface()
        with self.assertRaises(AttributeError):
            # attribute name basically guaranteed not to exist in real
            # OpenStack code
            config_editor._apply_changes(
                gi, {'brinzer_attribute': 'nonexistent'}
            )

    def test_apply_delta(self):
        gi = LibvirtConfigGuestInterface()
        self.assertIsNone(gi.net_type)

        in_orig = {'changes': {'net_type': 'hostdev'}}
        in_copy = copy.deepcopy(in_orig)

        config_editor.apply_delta(gi, in_orig)
        self.assertEqual(gi.net_type, 'hostdev')

        self.assertEqual(
            in_orig, in_copy, 'apply_delta() must not modify its input'
        )

    def test_apply_delta_error(self):
        # main point: coverage of error handling code

        gi = LibvirtConfigGuestInterface()
        self.assertIsNone(gi.net_type)

        with self.assertRaisesRegexp(ValueError, 'to be a dict'):
            config_editor.apply_delta(gi, ['changes', {'net_type': 'hostdev'}])
        self.assertIsNone(gi.net_type)

        example_net_type = 'example_net_type'
        gi.net_type = example_net_type
        in_orig = {'changes': {'net_type': 'hostdev'}, 'additions': '1+1'}
        with self.assertRaisesRegexp(
            ValueError, 'JSON delta.*unknown key.*in input.*additions'
        ):
            config_editor.apply_delta(gi, in_orig)
        self.assertEqual(gi.net_type, example_net_type)

        in_orig = {}
        with self.assertRaisesRegexp(ValueError, 'missing required.*changes'):
            config_editor.apply_delta(gi, in_orig)

    def test_create_delta(self):
        delta = config_editor.create_delta({}, {})
        self.assertEqual(delta, {'changes': {}})

        delta = config_editor.create_delta({'a': 1, 'b': 2}, {'a': 1, 'b': 3})
        self.assertEqual(delta, {'changes': {'b': 3}})

        delta = config_editor.create_delta({'b': 1, 'a': 2}, {'b': 1, 'a': []})
        self.assertEqual(delta, {'changes': {'a': []}})

    def test_create_delta_error(self):
        with self.assertRaisesRegexp(
            ValueError, 'orig.*keys'
        ):
            config_editor.create_delta([], {})

        with self.assertRaisesRegexp(
            ValueError, 'new.*keys'
        ):
            config_editor.create_delta({}, [])

        with self.assertRaisesRegexp(
            ValueError, 'mismatched.*keys.*not.*supported'
        ):
            config_editor.create_delta({}, {'a': 1})

        with self.assertRaisesRegexp(
            ValueError, 'mismatched.*keys.*not.*supported'
        ):
            config_editor.create_delta({'a': 1}, {})

if __name__ == '__main__':
    unittest.main()
