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

import errno
import itertools
import os
import tempfile
import unittest
import uuid

from ConfigParser import ConfigParser
from datetime import timedelta
from oslo_config import cfg

from netronome.vrouter import (config_opts, plug_modes as PM)
from netronome.vrouter.tests.unit import *

if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'config_opts.'
else:
    _TMP_PREFIX = 'netronome.vrouter.tests.unit.test_config_opts.'


class TestParseTimeDelta(unittest.TestCase):
    def test_parse_timedelta(self):
        # basic test of format_timedelta() (which I only wrote to test
        # parse_timedelta()...)
        self.assertEqual(config_opts.format_timedelta(timedelta()), '0s')

        good = (
            ('0s', timedelta()),
            ('1s', timedelta(seconds=1)),
            ('60s', timedelta(minutes=1)),
            ('3600s', timedelta(hours=1)),
            ('3601s', timedelta(hours=1, seconds=1)),
            ('3721s', timedelta(hours=1, minutes=2, seconds=1)),
            ('1h1m1s', timedelta(seconds=3661)),
            ('1d1h1m1s', timedelta(seconds=90061)),
            ('1d3h2m1s', timedelta(seconds=97321)),
        )
        for pair in good:
            actual = config_opts.parse_timedelta(pair[0])
            expected = pair[1]
            self.assertEqual(
                config_opts.format_timedelta(actual),
                config_opts.format_timedelta(expected)
            )

        bad = (
            '',
            '1h1',
            '0',
        )
        for b in bad:
            with self.assertRaises(ValueError):
                config_opts.parse_timedelta(b)


class TestMacAddress(unittest.TestCase):
    def test_MacAddress_ctor(self):
        config_opts.MacAddress()

    def test_MacAddress(self):
        # basic test of MacAddress class
        good = (
            '00:00:00:00:00:00',
            '12:34:56:78:9a:BC',
        )
        for g in good:
            out = config_opts.MacAddress()(g)
            self.assertEqual(g, out)

        bad = (
            ' 00:00:00:00:00:00',
            '12:34:56:78:9a:BC ',
            '0:00:00:00:00:00',
            '000:00:00:00:00:00',
            '12:34:56:78:9a:B',
            '12:34:56:78:9a:BBB',
            '12:34:56:7a8:9a:BB',
            '',
            1,
            {},
        )
        for b in bad:
            with self.assertRaises(ValueError):
                config_opts.MacAddress()(b)


class TestTypes(unittest.TestCase):
    def test_TimeDelta_ctor(self):
        config_opts.TimeDelta()

    def test_Uuid_ctor(self):
        config_opts.Uuid()

    def test_Json_ctor(self):
        config_opts.Json()

    def test_TimeDelta_conversion(self):
        out = config_opts.TimeDelta()('1h30m')
        self.assertIsInstance(out, timedelta)

    def test_Uuid_conversion(self):
        u_in = uuid.uuid1()
        u_out = config_opts.Uuid()(str(u_in))
        self.assertIsInstance(u_out, uuid.UUID)
        self.assertEqual(u_out, u_in)

    def test_Json_conversion(self):
        out = config_opts.Json()('{"a": 1}')
        self.assertIsInstance(out, dict)
        self.assertEqual(out, {'a': 1})


def _permute_modes(modes):
    for i in xrange(len(modes), -1, -1):
        p = itertools.permutations(modes, i)
        for m in p:
            yield m


class TestParseConf(unittest.TestCase):
    def _test_parse_conf(self, expected_rc, args=[], config_files=()):

        # Test that we can create multiple oslo_config.ConfigOpts objects
        # (inside get_conf() with the same options registered. (Command-line
        # testing yesterday seemed to indicate that this could be problematic.)
        for i in xrange(1, 10):
            rc = config_opts.parse_conf(
                args=args,
                default_config_files=config_files
            )

            self.assertIsInstance(rc, cfg.ConfigOpts)
            self.assertIsInstance(rc.reservation_timeout, timedelta)

            # check values only for the attributes that were sent in
            for k, v in expected_rc.iteritems():
                attr = getattr(rc, k)
                try:
                    self.assertEqual(attr, v)
                except (AssertionError, cfg.NoSuchOptError) as e:
                    if config_files:
                        suffix = 'See {}'.format(':'.join(config_files))
                    else:
                        suffix = '(no config files)'

                    e.args = explain_assertion(
                        e.args,
                        'Expected conf.{} to have the value {!r}:\n'
                        '    => {}'
                        .format(k, v, suffix)
                    )

                    raise

    def test_parse_conf(self):
        c2f = lambda s: 'off' if s == PM.unaccelerated else s
        f2c = lambda s: PM.unaccelerated if s == 'off' else s
        config_opts_modes = filter(c2f, PM.all_plug_modes)

        for hw_acceleration_modes in _permute_modes(config_opts_modes):
            for hw_acceleration_mode in (None,) + config_opts_modes:

                if not hw_acceleration_modes:
                    config_files = ()
                    expected_modes = [f2c('off')]
                else:
                    tmp = tempfile.NamedTemporaryFile(
                        suffix='.conf', prefix=_TMP_PREFIX, delete=False).name

                    c = ConfigParser()
                    c.set(
                        'DEFAULT',
                        'hw_acceleration_modes',
                        ', '.join(hw_acceleration_modes)
                    )
                    with open(tmp, 'w') as fh:
                        c.write(fh)

                    config_files = (tmp,)
                    expected_modes = config_opts.iommu_mode_sort(
                        filter(f2c, hw_acceleration_modes)
                    )

                if hw_acceleration_mode is None:
                    args = ()
                else:
                    args = (
                        '--hw-acceleration-mode', c2f(hw_acceleration_mode)
                    )

            self._test_parse_conf(
                args=args,
                config_files=config_files,
                expected_rc={
                    'hw_acceleration_modes': expected_modes,
                    'hw_acceleration_mode': hw_acceleration_mode,
                }
            )

    def test_parse_hw_acceleration_modes(self):
        # Ordered collection for deterministic order of running tests (helpful
        # when debugging by commenting things out).
        #
        # Note, the outputs are supposed to be de-duplicated, and sorted in
        # order of most to least restrictive IOMMU requirements.
        test_data = (
            (None, [PM.unaccelerated]),
            ('', [PM.unaccelerated]),
            ('unaccelerated', [PM.unaccelerated]),
            ('VirtIO', [PM.VirtIO]),
            ('SR-IOV', [PM.SRIOV]),
            ('VirtIO, SR-IOV', [PM.VirtIO, PM.SRIOV]),
            ('VirtIO,SR-IOV', [PM.VirtIO, PM.SRIOV]),
            ('SR-IOV, VirtIO', [PM.VirtIO, PM.SRIOV]),
            ('SR-IOV,VirtIO', [PM.VirtIO, PM.SRIOV]),

            # De-duplicate.
            ('SR-IOV, SR-IOV, VirtIO', [PM.VirtIO, PM.SRIOV]),

            # Test mode sorting with unaccelerated mode in the mix.
            ('unaccelerated, SR-IOV, SR-IOV, VirtIO,unaccelerated, VirtIO',
             [PM.VirtIO, PM.SRIOV, PM.unaccelerated]),

            # TODO(wbrinzer): Improve handling of "misspellings."
            ('sriov', [PM.unaccelerated]),
        )

        for td in test_data:
            k, expected_modes = td

            if k is None:
                config_files = ()
            else:
                tmp = tempfile.NamedTemporaryFile(
                    suffix='.conf', prefix=_TMP_PREFIX, delete=False).name

                c = ConfigParser()
                c.set('DEFAULT', 'hw_acceleration_modes', k)
                with open(tmp, 'w') as fh:
                    c.write(fh)

                config_files = (tmp,)

            self._test_parse_conf(
                args=[],
                config_files=config_files,
                expected_rc={
                    'hw_acceleration_modes': expected_modes,
                }
            )

    def test_parse_hw_acceleration_mode(self):
        # Ordered collection for deterministic order of running tests (helpful
        # when debugging by commenting things out).
        test_data = (
            ('off', PM.unaccelerated),
            ('VirtIO', PM.VirtIO),
            ('SR-IOV', PM.SRIOV),
        )

        for td in test_data:
            k, expected_mode = td

            args = ['--hw-acceleration-mode={}'.format(k)]

            self._test_parse_conf(
                args=args,
                config_files=(),
                expected_rc={
                    'hw_acceleration_mode': expected_mode,
                }
            )


class TestFeaturesEnabled(unittest.TestCase):
    def _test(self, description, dname, fname, pred, pairs):
        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, description)
        control_d = os.path.join(d, dname)
        os.makedirs(control_d)
        f = os.path.join(control_d, fname)

        for p in pairs:
            assertTF = self.assertTrue if p[1] else self.assertFalse

            try:
                try:
                    os.unlink(f)
                except (IOError, OSError) as e:
                    if e.errno != errno.ENOENT:
                        raise

                if p[0] is not None:
                    with open(f, 'w') as fh:
                        fh.write(p[0])

                action = 'write'
                assertTF(pred(_root_dir=d))

                if [0] is not None:
                    with open(f, 'w') as fh:
                        print >>fh, p[0]

                action = 'print'
                assertTF(pred(_root_dir=d))

            except AssertionError as e:
                e.args = explain_assertion(
                    e.args,
                    'failed: {!r} == {!r} ({})'.format(p[0], p[1], action)
                )
                raise

    def test_acceleration_enabled(self):
        self._test(
            description='test_acceleration_enabled',
            dname='sys/module/nfp_vrouter/control',
            fname='physical_vif_count',
            pred=config_opts.is_acceleration_enabled,
            pairs=(
                (None, False),    # missing
                ('', False),      # empty
                (' ', False),     # non-integer
                ('0', False),
                (' 0', False),
                ('0 ', False),
                ('1', True),
                (' 1', True),
                ('1 ', True),
                ('-2', False),    # negative should be False
                ('12345', True),
                (' 12345', True),
                ('12345 ', True),
            ),
        )

    def test_ksm_enabled(self):
        self._test(
            description='test_ksm_enabled',
            dname='sys/kernel/mm/ksm',
            fname='run',
            pred=config_opts.is_ksm_enabled,
            pairs=(
                (None, False),    # missing
                ('', False),      # empty
                (' ', False),     # non-integer
                ('0', False),
                (' 0', False),
                ('0 ', False),
                ('1', True),
                (' 1', True),
                ('1 ', True),
                ('-2', True),     # negative should be True
                ('12345', True),
                (' 12345', True),
                ('12345 ', True),
            )
        )

if __name__ == '__main__':
    unittest.main()
