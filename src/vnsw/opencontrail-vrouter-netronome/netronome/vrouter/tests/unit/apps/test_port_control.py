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

import argparse
import contextlib
import json
import logging
import os
import random
import re
import tempfile
import unittest
import uuid

from netronome import subcmd
from netronome.vrouter import (
    config, config_editor, database, fallback, flavor, glance, plug,
    plug_modes as PM, port, vf
)
from netronome.vrouter.apps import port_control
from netronome.vrouter.sa.sqlite import set_sqlite_synchronous_off
from netronome.vrouter.tests.helpers.config import FakeSysfs
from netronome.vrouter.tests.helpers.plug import (
    _DisableGC, _enable_fake_intel_iommu, FakeAgent
)
from netronome.vrouter.tests.randmac import RandMac
from netronome.vrouter.tests.unit import *

from ConfigParser import ConfigParser
from datetime import timedelta
from lxml import etree
from nova.virt.libvirt.config import LibvirtConfigGuestInterface
from oslo_config import cfg
from sqlalchemy.orm.session import sessionmaker
from StringIO import StringIO

try:
    from netronome import iommu_check
except ImportError:
    iommu_check = None

_CONFIG_LOGGER = make_getLogger('netronome.vrouter.config')
_FLAVOR_LOGGER = make_getLogger('netronome.vrouter.flavor')
_PLUG_LOGGER = make_getLogger('netronome.vrouter.plug')
_PORT_CONTROL_LOGGER = make_getLogger('netronome.vrouter.apps.port_control')
_VF_LOGGER = make_getLogger('netronome.vrouter.vf')
_VROUTER_LOGGER = make_getLogger('netronome.vrouter')


def _ROOT_LMC(cls=LogMessageCounter):
    return attachLogHandler(logging.root, cls())

if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'port_control.'
else:
    _TMP_PREFIX = 'netronome.vrouter.tests.unit.apps.test_port_control.'


def setUpModule():
    e = re.split(r'\s+', os.environ.get('NS_VROUTER_TESTS_ENABLE_LOGGING', ''))
    if 'port_control' in e:
        logging.basicConfig(level=logging.DEBUG)


class TestApp(unittest.TestCase):
    """Basic tests of the vRouterPortControl application."""

    def test_ctor(self):
        app = port_control.vRouterPortControl()
        self.assertGreaterEqual(len(app.cmds), 1)


class TestOsloConfigSubcmd(unittest.TestCase):
    """Basic tests of the OsloConfigSubcmd class."""

    def test_ctor(self):
        cmd = port_control.OsloConfigSubcmd(
            name='something', logger=subcmd._LOGGER
        )
        self.assertTrue(hasattr(cmd, 'conf'))
        self.assertIsNone(cmd.conf)

    def test_create_conf(self):
        cmd = port_control.OsloConfigSubcmd(
            name='something', logger=subcmd._LOGGER
        )
        conf = cmd.create_conf()
        self.assertIsInstance(conf, cfg.ConfigOpts)
        self.assertIsNone(cmd.conf)


class TestConfigCmd_BasicTest(unittest.TestCase):
    """Basic tests of the "config" command."""

    def test_ctor(self):
        cmd = port_control.ConfigCmd(logger=subcmd._LOGGER)
        self.assertIsInstance(cmd, port_control.OsloConfigSubcmd)

    def test_create_conf(self):
        cmd = port_control.ConfigCmd(logger=subcmd._LOGGER)
        conf = cmd.create_conf()
        self.assertIsInstance(conf, cfg.ConfigOpts)
        self.assertIsNone(cmd.conf)

    def test_parse_args(self):
        cmd = port_control.ConfigCmd(logger=subcmd._LOGGER)
        self.assertIsNone(cmd.conf)

        u = uuid.uuid1()
        a = (
            '--neutron-port', str(u),
        )
        rc = cmd.parse_args(
            prog=subcmd._PROG, command='xx', args=a, default_config_files=()
        )
        self.assertIsNone(rc)
        self.assertIsInstance(cmd.conf, cfg.ConfigOpts)

        self.assertEqual(cmd.conf.neutron_port, u)


def _make_config_file(**kwds):
    tmp = tempfile.NamedTemporaryFile(
        suffix='.conf', prefix=_TMP_PREFIX, delete=False).name

    c = ConfigParser()
    for k, v in kwds.iteritems():
        c.set('DEFAULT', k, v)
    with open(tmp, 'w') as fh:
        c.write(fh)

    return (tmp,)


def _make_flavor(tweak=None):
    ans = {
        'type': 'Flavor',
        'vars': {
            '_changed_fields': None,
            '_deleted_at': None,
            '_vcpus': 1,
            '_context': None,
            '_deleted': False,
            '_created_at': None,
            '_flavorid': '1',
            '_disabled': False,
            '_orig_extra_specs': {},
            '_vcpu_weight': 0,
            '_updated_at': None,
            '_memory_mb': 512,
            'VERSION': '1.1',
            '_root_gb': 1,
            '_is_public': True,
            '_rxtx_factor': 1.0,
            '_extra_specs': {},
            '_orig_projects': [],
            '_name': 'm1.tiny',
            '_swap': 0,
            '_id': 2,
            '_ephemeral_gb': 0
        }
    }

    if tweak:
        tweak(ans)
    return ans


def _make_image_metadata(tweak=None):
    ans = {
        'type': 'dict',
        'vars': {
            'status': 'active',
            'deleted': False,
            'container_format': 'bare',
            'min_ram': 0,
            'updated_at': '2016-05-31T19:46:28.740202',
            'min_disk': 0,
            'owner': 'c56959550c8c4fc79aea4f3a238077ac',
            'is_public': True,
            'deleted_at': None,
            'properties': {},
            'size': 14825984,
            'name': 'cirros_wbrinzer_20160531T1546_netvf',
            'checksum': 'ac4e4f2a4345f2b82ab574a3e837dcf1',
            'created_at': '2016-05-31T19:46:27.988943',
            'disk_format': 'qcow2',
            'id': '312e928f-61b0-4fb5-b8d9-1f702900cb5e'
        }
    }

    if tweak:
        tweak(ans)
    return ans


def _enable_sriov(image_metadata):
    image_metadata['vars']['properties'].setdefault(
        glance.HW_ACCELERATION_FEATURES_PROPERTY, glance.SRIOV_FEATURE_TOKEN
    )


def _disable_sriov(image_metadata):
    del image_metadata['vars']['properties'][
        glance.HW_ACCELERATION_FEATURES_PROPERTY
    ]


def _get_plug_mode(db_fname, neutron_port):
    engine = database.create_engine(db_fname)[0]
    set_sqlite_synchronous_off(engine)
    Session = sessionmaker(bind=engine)

    s = Session()

    return s.query(port.PlugMode).filter(
        port.PlugMode.neutron_port == neutron_port).one().mode


class TestConfigCmd_AppTest(unittest.TestCase):
    """Higher-level tests of the "config" command."""

    @staticmethod
    @contextlib.contextmanager
    def _session(db_fname):
        engine = database.create_engine(db_fname)[0]
        set_sqlite_synchronous_off(engine)
        Session = sessionmaker(bind=engine)
        yield Session()

    @contextlib.contextmanager
    def _test_config(
        self, _cm_parse=None, _cm_run=None, cmd_args=(),
        hw_acceleration_modes=None, database='tmp', neutron_port=None,
        _fallback_map_str='1234:56:78.9 abcd:ef:01.2 coffee_v3.4 5',
    ):
        # Setup the fake sysfs and procfs.
        fake_sysfs = FakeSysfs(nfp_status=1, physical_vif_count=1)
        _enable_fake_intel_iommu(fake_sysfs.root_dir)

        io = StringIO()
        cmd = port_control.ConfigCmd(
            _fh=io,
            _fallback_map_str=_fallback_map_str,
            _root_dir=fake_sysfs.root_dir,
            logger=_PORT_CONTROL_LOGGER(),
        )
        u = uuid.uuid1() if neutron_port is None else neutron_port
        a = cmd_args + ('--neutron-port', str(u))

        with attachLogHandler(_VROUTER_LOGGER(), LogMessageCounter()) as lmc:
            c = {'database': database}

            if hw_acceleration_modes is not None:
                if not isinstance(hw_acceleration_modes, tuple):
                    raise ValueError('hw_acceleration_modes must be a tuple')
                c['hw_acceleration_modes'] = (','.join(hw_acceleration_modes))

            def _parse():
                cmd.parse_args(
                    prog=subcmd._PROG, command='xx', args=a,
                    default_config_files=_make_config_file(**c)
                )

            if _cm_parse:
                with _cm_parse:
                    _parse()
                yield (io.getvalue(), lmc.count)

            _parse()

            def _run():
                rc = cmd.run()
                self.assertIsInstance(rc, int)
                return rc

            if _cm_run:
                rc = None
                with _cm_run:
                    rc = _run()
            else:
                rc = _run()

            yield (io.getvalue(), lmc.count, rc)

    def test_config_unaccelerated_json(self):
        """
        Basic test of vrouter-port-control config in unaccelerated mode, with
        JSON delta output. (High-level "unit" test)
        """
        with self._test_config(
            hw_acceleration_modes=(PM.unaccelerated,),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
            })

            # test output format
            delta = json.loads(output)

            # make sure it can be applied to a LibvirtConfigGuestInterface
            interface = LibvirtConfigGuestInterface()
            config_editor.apply_delta(interface, delta)

    def test_config_unaccelerated_xml(self):
        """
        Basic test of vrouter-port-control config in unaccelerated mode, with
        XML output. (High-level "unit" test)
        """
        with self._test_config(
            hw_acceleration_modes=(PM.unaccelerated,),
            cmd_args=('--output-format', 'xml'),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
            })

            # test output format
            xml = etree.fromstring(output)

            self.assertEqual(xml.tag, 'interface')

    def test_config_unaccelerated_none(self):
        """
        Basic test of vrouter-port-control config in unaccelerated mode, with
        no output. (High-level "unit" test)
        """
        with self._test_config(
            hw_acceleration_modes=(PM.unaccelerated,),
            cmd_args=('--output-format', 'none'),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
            })

            self.assertEqual(output, '')

    def test_config_VirtIO_none(self):
        # Main purpose of this test is to make sure that the _test_config is
        # sufficiently isolated and has all the mocking code installed to make
        # it valid for running "virtio with bad flavor," etc. tests.

        with self._test_config(
            hw_acceleration_modes=(PM.VirtIO,),
            cmd_args=('--output-format', 'none'),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
                _VF_LOGGER.name: {'INFO': 1},
            })

            self.assertEqual(output, '')

    def test_config_VirtIO_good_flavor(self):
        """
        Test of what happens when attempting to launch a VirtIO-accelerated
        instance with a flavor that doesn't support virtiorelayd (i.e., a
        flavor without shared hugepages).
        """

        good_flavor = _make_flavor()
        good_flavor['vars']['_extra_specs'].setdefault(
            'hw:mem_page_size', '1G'
        )

        with self._test_config(
            hw_acceleration_modes=(PM.VirtIO,),
            cmd_args=(
                '--flavor', json.dumps(good_flavor),
                '--output-format', 'none',
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

    def test_config_VirtIO_bad_flavor(self):
        """
        Test of what happens when attempting to launch a VirtIO-accelerated
        instance with a flavor that doesn't support virtiorelayd (i.e., a
        flavor without shared hugepages).
        """

        bad_flavor = _make_flavor()

        with self._test_config(
            hw_acceleration_modes=(PM.VirtIO,),
            cmd_args=('--flavor', json.dumps(bad_flavor)),
            _cm_run=self.assertRaises(flavor.FlavorVirtIOConfigError)
        ) as (output, logs, rc):

            self.assertIsNone(rc)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _FLAVOR_LOGGER.name: {'CRITICAL': 1},
            })
            self.assertEqual(output, '')

    def test_config_out_of_VFs_fallback_to_unaccelerated(self):
        # Make sure that we fall back to unaccelerated mode if it is permitted
        # and we run out of VFs (VRT-720).

        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        for preferred_mode in (PM.SRIOV, PM.VirtIO):
            db_fname = os.path.join(
                d, 'test_config_out_of_VFs.{}.sqlite3'.format(preferred_mode)
            )

            with self._test_config(
                hw_acceleration_modes=(preferred_mode, PM.unaccelerated),
                cmd_args=('--output-format', 'none'),
                database=db_fname,
                _fallback_map_str='',
            ) as (output, logs, rc):

                self.assertEqual(rc, 0)
                self.assertEqual(logs, {
                    _PORT_CONTROL_LOGGER.name: {'DEBUG': 1, 'WARNING': 1},
                    _VF_LOGGER.name: {'WARNING': 1, 'ERROR': 1},
                })

                self.assertEqual(output, '')

                with self._session(db_fname) as s:
                    q = s.query(port.PlugMode.mode)
                    self.assertEqual(q.one(), (PM.unaccelerated,))

    def test_config_out_of_VFs(self):
        # Make sure that we do NOT fall back to unaccelerated mode if it is
        # not permitted and we run out of VFs (VRT-720).

        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        for preferred_mode in (PM.SRIOV, PM.VirtIO):
            db_fname = os.path.join(
                d, 'test_config_out_of_VFs.{}.sqlite3'.format(preferred_mode)
            )

            with self._test_config(
                hw_acceleration_modes=(PM.SRIOV,),
                cmd_args=('--output-format', 'none'),
                database=db_fname,
                _fallback_map_str='',
                _cm_run=self.assertRaises(vf.AllocationError),
            ) as (output, logs, rc):

                self.assertIsNone(rc)
                self.assertEqual(logs, {
                    _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
                    _VF_LOGGER.name: {'ERROR': 1, 'WARNING': 1},
                })

                self.assertEqual(output, '')

    def test_config_SRIOV_none(self):
        # Main purpose of this test is to make sure that the _test_config is
        # sufficiently isolated and has all the mocking code installed to make
        # it valid for running "virtio with bad flavor," etc. tests.

        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        db_fname = os.path.join(d, 'test_config_SRIOV_none.sqlite3')

        with self._test_config(
            hw_acceleration_modes=(PM.SRIOV,),
            cmd_args=('--output-format', 'none'),
            database=db_fname,
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
                _VF_LOGGER.name: {'INFO': 1},
            })

            self.assertEqual(output, '')

            with self._session(db_fname) as s:
                q = s.query(port.PlugMode.mode)
                self.assertEqual(q.one(), (PM.SRIOV,))

    def test_config_SRIOV_missing_glance_metadata(self):
        """Test SR-IOV mode launch of an image not tagged for SR-IOV mode."""

        image_metadata = _make_image_metadata()

        with self._test_config(
            hw_acceleration_modes=(PM.SRIOV,),
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
            _cm_run=self.assertRaises(config.AccelerationModeConflict)
        ) as (output, logs, rc):

            self.assertIsNone(rc)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _CONFIG_LOGGER.name: {'ERROR': 1, 'INFO': 2},
            })

            self.assertEqual(output, '')

    def test_config_SRIOV_good_glance_metadata(self):
        """Test SR-IOV mode launch of an image tagged for SR-IOV mode."""

        image_metadata = _make_image_metadata(tweak=_enable_sriov)

        with self._test_config(
            hw_acceleration_modes=(PM.SRIOV,),
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })

            self.assertEqual(output, '')

    def test_reconfig_no_constraints(self):
        """Config twice."""
        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        db_fname = os.path.join(d, 'reconfig_no_constraints.sqlite3')

        # The essence of a "reconfig" is to configure the *same* port more than
        # once.
        neutron_port = uuid.uuid1()

        # Initial config, use blank (not None) Glance image metadata so that
        # the port is configured in VirtIO mode.
        image_metadata = _make_image_metadata()

        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port was initially plugged in VirtIO mode, as we
        # expected.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.VirtIO
        )

        # Second config. No changes.
        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port was still plugged in VirtIO mode, as we expected.
        # (This is not very surprising in the absence of external forces
        # encouraging the plug mode to change.)
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.VirtIO
        )

    def test_reconfig_hardware_acceleration_mode_same(self):
        """
        Config in freely determined VirtIO mode, then reconfig with
        --hardware-acceleration-mode=VirtIO. Confirm that there are no errors
        and that the 2nd configuration still results in VirtIO mode.
        """
        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        db_fname = os.path.join(
            d, 'reconfig_hardware_acceleration_mode_same.sqlite3'
        )

        # The essence of a "reconfig" is to configure the *same* port more than
        # once.
        neutron_port = uuid.uuid1()

        # Initial config, use blank (not None) Glance image metadata so that
        # the port is configured in VirtIO mode.
        image_metadata = _make_image_metadata()

        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port was initially plugged in VirtIO mode, as we
        # expected.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.VirtIO
        )

        # Second config.
        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
                '--hw-acceleration-mode', PM.VirtIO,
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port is still plugged in VirtIO mode.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.VirtIO
        )

    def test_reconfig_hardware_acceleration_mode_conflict(self):
        """
        Config in freely determined SR-IOV mode, then reconfig with
        --hardware-acceleration-mode=(conflicting mode). Confirm that this
        results in an AccelerationModeConflict error.
        """
        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        db_fname = os.path.join(
            d, 'reconfig_hardware_acceleration_mode_conflict.sqlite3'
        )

        # The essence of a "reconfig" is to configure the *same* port more than
        # once.
        neutron_port = uuid.uuid1()

        # Initial config, use Glance image metadata that is marked compatible
        # with SR-IOV.
        image_metadata = _make_image_metadata(tweak=_enable_sriov)

        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port was initially plugged in SR-IOV mode, as we
        # expected.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.SRIOV
        )

        # Conflicting configs.
        for conflicting_mode in ('off', PM.VirtIO):
            try:
                with self._test_config(
                    database=db_fname,
                    neutron_port=neutron_port,
                    hw_acceleration_modes=PM.all_plug_modes,
                    cmd_args=(
                        '--output-format', 'none',
                        '--image-metadata', json.dumps(image_metadata),
                        '--hw-acceleration-mode', conflicting_mode,
                    ),
                    _cm_run=self.assertRaises(plug.PlugModeError),
                ) as (output, logs, rc):

                    self.assertIsNone(rc)
                    expected_logs = {
                        _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                    }
                    self.assertEqual(logs, expected_logs)
                    self.assertEqual(output, '')

                # Check that the port is still plugged in SR-IOV mode.
                self.assertEqual(
                    _get_plug_mode(db_fname, neutron_port), PM.SRIOV
                )

            except AssertionError as e:
                e.args = explain_assertion(
                    e.args,
                    'conflicting_mode {!r} failed'.format(conflicting_mode)
                )
                raise

    def test_reconfig_add_glance_SRIOV(self):
        """
        Config in VirtIO mode, then reconfig with SR-IOV added to the Glance
        metadata. Confirm that the 2nd configuration still results in VirtIO
        mode.
        """
        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        db_fname = os.path.join(d, 'reconfig_add_glance_SRIOV.sqlite3')

        # The essence of a "reconfig" is to configure the *same* port more than
        # once.
        neutron_port = uuid.uuid1()

        # Initial config, use blank (not None) Glance image metadata so that
        # the port is configured in VirtIO mode.
        image_metadata = _make_image_metadata()

        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port was initially plugged in VirtIO mode, as we
        # expected.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.VirtIO
        )

        # Second config.
        _enable_sriov(image_metadata)

        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port is still plugged in VirtIO mode.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.VirtIO
        )

    def test_reconfig_remove_glance_SRIOV(self):
        """
        Config in SR-IOV mode, then reconfig with SR-IOV removed from the
        Glance metadata. Confirm that the 2nd configuration results in an
        AccelerationModeConflict, since the image no longer supports the
        original mode.
        """
        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        db_fname = os.path.join(d, 'reconfig_remove_glance_SRIOV.sqlite3')

        # The essence of a "reconfig" is to configure the *same* port more than
        # once.
        neutron_port = uuid.uuid1()

        # Initial config, use blank (not None) Glance image metadata so that
        # the port is configured in VirtIO mode.
        image_metadata = _make_image_metadata(tweak=_enable_sriov)

        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
        ) as (output, logs, rc):

            self.assertEqual(rc, 0)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _VF_LOGGER.name: {'INFO': 1},
            })
            self.assertEqual(output, '')

        # Check that the port was initially plugged in SR-IOV mode, as we
        # expected.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.SRIOV
        )

        # Second config.
        _disable_sriov(image_metadata)

        with self._test_config(
            database=db_fname,
            neutron_port=neutron_port,
            hw_acceleration_modes=PM.all_plug_modes,  # no constraints
            cmd_args=(
                '--output-format', 'none',
                '--image-metadata', json.dumps(image_metadata),
            ),
            _cm_run=self.assertRaises(config.AccelerationModeConflict)
        ) as (output, logs, rc):

            self.assertIsNone(rc)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _CONFIG_LOGGER.name: {'ERROR': 1, 'INFO': 2},
            })
            self.assertEqual(output, '')

        # Check that the port is still plugged in SR-IOV mode.
        self.assertEqual(
            _get_plug_mode(db_fname, neutron_port), PM.SRIOV
        )

    def test_config_cannot_create_db(self):
        """
        Check error handling with uncreatable DB.
        """
        d = tempfile.mkdtemp(prefix=_TMP_PREFIX)

        with self._test_config(
            database=d,
            hw_acceleration_modes=(PM.unaccelerated,),
        ) as (output, logs, rc):

            self.assertEqual(rc, 1)
            self.assertEqual(output, '')
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1, 'CRITICAL': 1},
            })

    def test_config_incomplete_input(self):
        """
        Check error handling with incomplete input.
        """
        with self._test_config(
            _cm_run=self.assertRaises(ValueError),
            cmd_args=('--input', '{}'),
            hw_acceleration_modes=(PM.unaccelerated,),
        ) as (output, logs, rc):

            self.assertIsNone(rc)
            self.assertEqual(output, '')
            self.assertEqual(logs, {})

    def test_config_incomplete_output(self):
        """
        Check error handling with incomplete output.
        """
        with self._test_config(
            cmd_args=('--input', json.dumps(
                {'type': 'LibvirtConfigGuestInterface', 'vars': {}}
            )),
            hw_acceleration_modes=(PM.unaccelerated,),
        ) as (output, logs, rc):

            self.assertEqual(rc, 1)
            self.assertEqual(output, '')
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {
                    'DEBUG': 1, 'ERROR': 1, 'CRITICAL': 1
                },
            })


class TestAddCmd_BasicTest(unittest.TestCase):
    """Basic tests of the "add" command."""

    def test_ctor(self):
        cmd = port_control.AddCmd(logger=subcmd._LOGGER)
        self.assertIsInstance(cmd, port_control.OsloConfigSubcmd)

    def test_parse_args(self):
        cmd = port_control.AddCmd(logger=subcmd._LOGGER)
        self.assertIsNone(cmd.conf)

        port_uuid = uuid.uuid1()
        instance_uuid = uuid.uuid1()
        vm_project_uuid = uuid.uuid1()
        vn_uuid = uuid.uuid1()
        mac1 = RandMac().generate()

        a = (
            '--uuid', str(port_uuid),
            '--instance_uuid', str(instance_uuid),
            '--vm_project_uuid', str(vm_project_uuid),
            '--vif_type', 'Vrouter',
            '--mac', mac1,
            '--port_type', 'NovaVMPort',
            '--tap_name', config.default_devname('tap', port_uuid),
            '--vn_uuid', str(vn_uuid),
        )

        rc = cmd.parse_args(
            prog=subcmd._PROG, command='xx', args=a, default_config_files=()
        )
        self.assertIsNone(rc)
        self.assertIsInstance(cmd.conf, cfg.ConfigOpts)

        self.assertEqual(cmd.conf.uuid, port_uuid)
        self.assertEqual(cmd.conf.instance_uuid, instance_uuid)
        self.assertEqual(cmd.conf.vm_project_uuid, vm_project_uuid)
        self.assertEqual(cmd.conf.vn_uuid, vn_uuid)
        self.assertEqual(cmd.conf.mac, mac1)

    def test_parse_args_ip_address_None(self):
        """IP address options must accept the literal string None for None."""
        cmd = port_control.AddCmd(logger=subcmd._LOGGER)
        self.assertIsNone(cmd.conf)

        port_uuid = uuid.uuid1()
        instance_uuid = uuid.uuid1()
        vm_project_uuid = uuid.uuid1()
        vn_uuid = uuid.uuid1()
        mac1 = RandMac().generate()

        a = (
            '--uuid', str(port_uuid),
            '--instance_uuid', str(instance_uuid),
            '--vm_project_uuid', str(vm_project_uuid),
            '--vif_type', 'Vrouter',
            '--mac', mac1,
            '--port_type', 'NovaVMPort',
            '--tap_name', config.default_devname('tap', port_uuid),
            '--vn_uuid', str(vn_uuid),
            '--ip_address', 'None',
            '--ipv6_address', 'fe80::1',
        )

        rc = cmd.parse_args(
            prog=subcmd._PROG, command='xx', args=a, default_config_files=()
        )
        self.assertIsNone(rc)
        self.assertIsInstance(cmd.conf, cfg.ConfigOpts)

        self.assertEqual(cmd.conf.uuid, port_uuid)
        self.assertEqual(cmd.conf.instance_uuid, instance_uuid)
        self.assertEqual(cmd.conf.vm_project_uuid, vm_project_uuid)
        self.assertEqual(cmd.conf.vn_uuid, vn_uuid)
        self.assertEqual(cmd.conf.mac, mac1)
        self.assertIsNone(cmd.conf.ip_address)
        self.assertIsNotNone(cmd.conf.ipv6_address)

        a = (
            '--uuid', str(port_uuid),
            '--instance_uuid', str(instance_uuid),
            '--vm_project_uuid', str(vm_project_uuid),
            '--vif_type', 'Vrouter',
            '--mac', mac1,
            '--port_type', 'NovaVMPort',
            '--tap_name', config.default_devname('tap', port_uuid),
            '--vn_uuid', str(vn_uuid),
            '--ip_address', '123.4.5.6',
            '--ipv6_address', 'None',
        )

        rc = cmd.parse_args(
            prog=subcmd._PROG, command='xx', args=a, default_config_files=()
        )
        self.assertIsNone(rc)
        self.assertIsInstance(cmd.conf, cfg.ConfigOpts)

        self.assertEqual(cmd.conf.uuid, port_uuid)
        self.assertEqual(cmd.conf.instance_uuid, instance_uuid)
        self.assertEqual(cmd.conf.vm_project_uuid, vm_project_uuid)
        self.assertEqual(cmd.conf.vn_uuid, vn_uuid)
        self.assertEqual(cmd.conf.mac, mac1)
        self.assertIsNotNone(cmd.conf.ip_address)
        self.assertIsNone(cmd.conf.ipv6_address)

    def test_translate_conf(self):
        cmd = port_control.AddCmd(logger=subcmd._LOGGER)
        self.assertIsNone(cmd.conf)

        port_uuid = uuid.uuid1()
        instance_uuid = uuid.uuid1()
        vm_project_uuid = uuid.uuid1()
        vn_uuid = uuid.uuid1()
        mac1 = RandMac().generate()

        a = (
            '--uuid', str(port_uuid),
            '--instance_uuid', str(instance_uuid),
            '--vm_project_uuid', str(vm_project_uuid),
            '--vif_type', 'Vrouter',
            '--mac', mac1,
            '--port_type', 'NovaVMPort',
            '--tap_name', config.default_devname('tap', port_uuid),
            '--vn_uuid', str(vn_uuid),

            # can also (in the future) verify that these options get the right
            # value in the output dictionaries
            '--virtio-relay-zmq-receive-timeout', '1m23s',
            '--vhostuser_socket_timeout', '1h25m11s',
            # '--no_persist', 'True',
        )

        rc = cmd.parse_args(
            prog=subcmd._PROG, command='xx', args=a, default_config_files=()
        )
        self.assertIsNone(rc)
        self.assertIsInstance(cmd.conf, cfg.ConfigOpts)

        with attachLogHandler(_VF_LOGGER()):
            vf_pool = vf.Pool(fallback.FallbackMap())

        plug_driver_types = (
            (plug._PlugUnaccelerated, {}),
            (plug._PlugSRIOV, {'vf_pool': vf_pool}),
            (plug._PlugVirtIO, {'vf_pool': vf_pool}),
        )

        for t in plug_driver_types:
            # 1. Make sure translate_conf() works.
            c = t[0].translate_conf(cmd.conf)
            self.assertIsInstance(c, dict)

            # 2. Make sure it produces something that can be fed into the plug
            # driver's constructor.
            d = t[0](config=c, **t[1])


def randip4():
    addr = ((1, 223), (0, 255), (0, 255), (0, 254))
    return '.'.join(map(lambda t: str(random.randint(*t)), addr))


def randip6():
    return 'fdf1:c50c:721b::{:x}'.format(random.randint(1, 65535))


class TestAddCmd_AppTest(_DisableGC, unittest.TestCase):
    """Higher-level tests of the "add" command."""

    def _pre_populate_database_with_plug_mode(self, db_fname, uuid, mode):
        engine = database.create_engine(db_fname)[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        pm = port.PlugMode(neutron_port=uuid, mode=mode)
        s.add(pm)
        s.commit()

    @contextlib.contextmanager
    def _test(
        self, pre_populate_mode, hw_acceleration_mode=None, _cm_run=None,
        n_trials=1, **config_kwds
    ):
        # Make a temporary database with a plug mode already configured.
        tmp = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        db_fname = os.path.join(tmp, 'TestAddCmd_AppTest.sqlite3')
        config_kwds.setdefault('database', db_fname)
        c = _make_config_file(**config_kwds)

        u = uuid.uuid1()
        if pre_populate_mode is not None:
            self._pre_populate_database_with_plug_mode(
                db_fname, u, mode=pre_populate_mode
            )

        # Setup the fake sysfs and procfs.
        fake_sysfs = FakeSysfs(
            nfp_status=1, physical_vif_count=1, _root_dir=tmp
        )
        _enable_fake_intel_iommu(fake_sysfs.root_dir)

        # Boot the fake agent.
        sysfs = FakeAgent.Sysfs(root_dir=fake_sysfs.root_dir)
        agent_config, agent, agent_thr = FakeAgent.boot(
            assertTrue=self.assertTrue,
            sysfs=sysfs,

            # test_plug_straightaway() can take close to 5s, which used to be
            # the maximum default time that the agent allowed itself to run.
            # This causes sporadic (rare) "IOLoop is closing" runtime errors
            # (if the timeout occurs before agent.sync()), and/or
            # InterfacePlugTimeoutErrors (if the timeout occurs before an
            # interface plug completes).
            #
            # 30s is still execution speed dependent but is long enough that it
            # should not cause problems during normal operation.
            run_timedelta=timedelta(seconds=30),
        )

        cmd = port_control.AddCmd(
            _fallback_map_str='0001:00:00.0 0007:05:85.0 coffee_v0.1 1',
            logger=_PORT_CONTROL_LOGGER(),
        )
        cmd._root_dir = tmp

        instance_uuid = uuid.uuid1()
        vm_project_uuid = uuid.uuid1()
        vn_uuid = uuid.uuid1()

        contrail_agent_port_dir = os.path.join(tmp, 'agent-port-dir')
        os.makedirs(contrail_agent_port_dir)

        a = [
            '--instance_uuid', str(instance_uuid),
            '--ip_address', randip4(),
            '--ipv6_address', randip6(),
            '--mac', RandMac().generate(),
            '--port_type', 'NovaVMPort',
            '--tap_name', config.default_devname('app', u),
            '--vif_type', 'Vrouter',
            '--vm_name', 'hello',
            '--vm_project_uuid', str(vm_project_uuid),
            '--vn_uuid', str(vn_uuid),
            '--uuid', str(u),
            '--contrail-agent-port-dir', contrail_agent_port_dir,
            '--contrail-agent-api-ep', agent_config.base_url,
        ]
        if hw_acceleration_mode is not None:
            a.extend(('--hw-acceleration-mode', hw_acceleration_mode))

        def _parse():
            cmd.parse_args(
                prog=subcmd._PROG, command='xo', args=a,
                default_config_files=c
            )

        def _run():
            rc = cmd.run()
            self.assertIsInstance(rc, int)
            return rc

        class Mock(object):
            pass

        with _ROOT_LMC() as lmc:
            _parse()

            cmd.conf.iproute2 = Mock()
            cmd.conf.iproute2.dry_run = True

            for i in xrange(n_trials):
                if _cm_run:
                    rc = None
                    with _cm_run:
                        rc = _run()
                else:
                    rc = _run()

        # Shut down the fake agent.
        agent.sync()
        agent.stop()
        JOIN_TIMEOUT_SEC = 9
        agent_thr.join(JOIN_TIMEOUT_SEC)
        self.assertFalse(
            agent.timeout or agent_thr.isAlive(),
            'agent did not shut down within {}s'.format(JOIN_TIMEOUT_SEC)
        )

        yield (lmc.count, rc)

    def test_configure_then_plug(self):
        with self._test(pre_populate_mode=PM.SRIOV) as (logs, rc):
            self.assertEqual(rc, 0)

            have_iommu_check = iommu_check is not None
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
                _PLUG_LOGGER.name: {'DEBUG': 7 + have_iommu_check, 'INFO': 2},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
                'urllib3.connectionpool': {'DEBUG': 1, 'INFO': 1},
                'urllib3.util.retry': {'DEBUG': 1},
            })

    def test_configure_then_multiplug(self):
        self.maxDiff = None

        with self._test(
            pre_populate_mode=PM.SRIOV,
            n_trials=2,
        ) as (logs, rc):

            self.assertEqual(rc, 0)

            have_iommu_check = iommu_check is not None
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 2},
                _PLUG_LOGGER.name: {
                    'INFO': 4, 'DEBUG': 14 + 2 * have_iommu_check,
                },
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 2},
                'urllib3.connectionpool': {'DEBUG': 2, 'INFO': 2},
                'urllib3.util.retry': {'DEBUG': 2},
            })

    def test_configure_then_plug_mandatory(self):
        with self._test(
            pre_populate_mode=PM.SRIOV,
            hw_acceleration_mode=PM.SRIOV
        ) as (logs, rc):

            self.assertEqual(rc, 0)

            have_iommu_check = iommu_check is not None
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
                _PLUG_LOGGER.name: {'DEBUG': 7 + have_iommu_check, 'INFO': 2},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
                'urllib3.connectionpool': {'DEBUG': 1, 'INFO': 1},
                'urllib3.util.retry': {'DEBUG': 1},
            })

    def test_configure_then_plug_mandatory_with_conflict(self):
        with self._test(
            pre_populate_mode=PM.SRIOV,
            hw_acceleration_mode=PM.VirtIO,
            _cm_run=self.assertRaises(plug.PlugModeError)
        ) as (logs, rc):

            self.assertIsNone(rc)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
            })

    def test_plug_straightaway(self):
        with self._test(
            pre_populate_mode=None,
            hw_acceleration_mode=PM.SRIOV,
        ) as (logs, rc):

            self.assertEqual(rc, 0)

            have_iommu_check = iommu_check is not None
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
                _PLUG_LOGGER.name: {'DEBUG': 7 + have_iommu_check, 'INFO': 2},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
                'urllib3.connectionpool': {'DEBUG': 1, 'INFO': 1},
                'urllib3.util.retry': {'DEBUG': 1},
            })

    def test_plug_straightaway_without_mandatory(self):
        with self._test(
            pre_populate_mode=None,
            _cm_run=self.assertRaises(plug.PlugModeError)
        ) as (logs, rc):

            self.assertIsNone(rc)
            self.assertEqual(logs, {
                _PORT_CONTROL_LOGGER.name: {'DEBUG': 1},
                _PLUG_LOGGER.name: {'INFO': 1},
            })


class TestDeleteCmd_BasicTest(unittest.TestCase):
    """Basic tests of the "delete" command."""

    def test_ctor(self):
        cmd = port_control.DeleteCmd(logger=subcmd._LOGGER)
        self.assertIsInstance(cmd, port_control.OsloConfigSubcmd)

    def test_parse_args(self):
        cmd = port_control.DeleteCmd(logger=subcmd._LOGGER)
        self.assertIsNone(cmd.conf)

        port_uuid = uuid.uuid1()

        a = (
            '--uuid', str(port_uuid),
        )

        rc = cmd.parse_args(
            prog=subcmd._PROG, command='xx', args=a, default_config_files=()
        )
        self.assertIsNone(rc)
        self.assertIsInstance(cmd.conf, cfg.ConfigOpts)

        self.assertEqual(cmd.conf.uuid, port_uuid)

    def test_translate_conf(self):
        cmd = port_control.DeleteCmd(logger=subcmd._LOGGER)
        self.assertIsNone(cmd.conf)

        port_uuid = uuid.uuid1()

        a = (
            '--uuid', str(port_uuid),

            # can also (in the future) verify that these options get the right
            # value in the output dictionaries
            '--virtio-relay-zmq-receive-timeout', '1m23s',
            '--no_persist', 'False',
        )

        rc = cmd.parse_args(
            prog=subcmd._PROG, command='xx', args=a, default_config_files=()
        )
        self.assertIsNone(rc)
        self.assertIsInstance(cmd.conf, cfg.ConfigOpts)

        with attachLogHandler(_VF_LOGGER()):
            vf_pool = vf.Pool(fallback.FallbackMap())

        plug_driver_types = (
            (plug._PlugUnaccelerated, {}),
            (plug._PlugSRIOV, {'vf_pool': vf_pool}),
            (plug._PlugVirtIO, {'vf_pool': vf_pool}),
        )

        for t in plug_driver_types:
            # 1. Make sure translate_conf() works.
            c = t[0].translate_conf(cmd.conf)
            self.assertIsInstance(c, dict)

            # 2. Make sure it produces something that can be fed into the plug
            # driver's constructor.
            d = t[0](config=c, **t[1])


if __name__ == '__main__':
    unittest.main()
