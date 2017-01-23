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

import contextlib
import errno
import inspect
import json
import logging
import os
import random
import re
import subprocess
import sys
import tempfile
import threading
import unittest
import uuid

from datetime import timedelta
from sqlalchemy.orm.session import sessionmaker

try:
    import netronome.virtiorelayd.virtiorelayd_pb2 as relay
except ImportError:
    relay = None

try:
    from netronome import iommu_check
except ImportError:
    iommu_check = None

from netronome.vrouter.rest import *
from netronome.vrouter import (
    config, database, fallback, pci, plug, plug_modes as PM, port, vf
)
from netronome.vrouter.sa.helpers import one_or_none
from netronome.vrouter.sa.sqlite import set_sqlite_synchronous_off
from netronome.vrouter.tests.helpers.config import _random_pci_address
from netronome.vrouter.tests.randmac import RandMac
from netronome.vrouter.tests.unit import *
from netronome.vrouter.tests.helpers.config import (
    _random_pci_address, _select_plug_mode_for_port, FakeSysfs
)
from netronome.vrouter.tests.helpers.plug import (
    _enable_fake_intel_iommu, _make_fake_sysfs_fallback_map, _DisableGC,
    FakeAgent, FakeVirtIOServer, URLLIB3_LOGGERS
)
from netronome.vrouter.tests.helpers.vf import (fake_FallbackMap)
from netronome.vrouter.tests.unit.rest import test_rest

from tornado.web import HTTPError

_CONFIG_LOGGER = make_getLogger('netronome.vrouter.config')
_PLUG_LOGGER = make_getLogger('netronome.vrouter.plug')
_VF_LOGGER = make_getLogger('netronome.vrouter.vf')

if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'plug.'
else:
    _TMP_PREFIX = 'netronome.vrouter.tests.unit.test_plug.'

VFSET1 = make_vfset('''
    0001:06:08.0 0001:06:08.1 0001:06:08.2 0001:06:08.3 0001:06:08.4
    0001:06:08.5 0001:06:08.6 0001:06:08.7
''')

# 127.255.255.255 gives ENETUNREACH on Linux, and EADDRNOTAVAIL on
# FreeBSD (regardless of target port).
_UNREACHABLE_AGENT_URL = 'http://127.255.255.255:49151'

urllib3_logging = SetLogLevel(loggers=URLLIB3_LOGGERS, level=logging.WARNING)


def setUpModule():
    e = re.split(r'\s+', os.environ.get('NS_VROUTER_TESTS_ENABLE_LOGGING', ''))
    if 'plug' in e:
        logging.basicConfig(level=logging.DEBUG)

    urllib3_logging.setUp()


def tearDownModule():
    urllib3_logging.tearDown()


def _test_data(tweak=None):
    u = str(uuid.uuid1())
    tap_name = ('nfp' + u)[:14]
    ans = {
        'uuid': u,
        'instance_uuid': 'dda746d1-d4bf-4b04-b57b-2999403b5b01',
        'vn_uuid': '2d624ac9-6156-421f-9550-07d46d823f1c',
        'vm_project_uuid': '55cb77ce-5eb1-49ca-b4fd-5bff809aba35',
        'ip_address': '0.0.0.0',
        'ipv6_address': None,
        'vm_name': 'Test VM #1',
        'mac': '11:22:33:44:55:66',
        'tap_name': tap_name,
        'port_type': 'NovaVMPort',
        'vif_type': 'Vrouter',
        'rx_vlan_id': -1,
        'tx_vlan_id': -1,
    }
    if tweak:
        tweak(ans)
    return ans


class TestPlug(unittest.TestCase):
    def test_ctors(self):
        with attachLogHandler(_VF_LOGGER()):
            vf_pool = vf.Pool(fallback.FallbackMap())

        plug_types = (
            (plug._PlugUnaccelerated, {}),
            (plug._PlugSRIOV, {'vf_pool': vf_pool}),
            (plug._PlugVirtIO, {'vf_pool': vf_pool}),
        )

        # Steps that are supposed to be in every type of plug operation.
        required_steps = frozenset((
            plug._AgentFileWrite,
            plug._AgentPost,
            plug._AgentRaiseOnCompleteFailure,
        ))

        for pt in plug_types:
            t, kwds = pt
            p = t(**kwds)
            self.assertIsInstance(p.steps, tuple), \
                '{} ctor did not create a tuple of steps'.format(t.__name__)

            # check required steps
            actual_steps = frozenset(map(lambda x: type(x), p.steps))
            missing = required_steps - actual_steps
            self.assertEqual(
                missing,
                frozenset(),
                '{} missing required step(s): {}'.format(
                    t.__name__,
                    ', '.join(map(lambda t: t.__name__, missing))
                )
            )


class TestStep(unittest.TestCase):
    def test_Step(self):
        s = plug._Step()

        with self.assertRaises(NotImplementedError):
            s.forward_action(None, None, None)

        with self.assertRaises(NotImplementedError):
            s.reverse_action(None, None, None)

    def test_Step_str(self):
        """
        Test that str(step) does not fail if `step` is missing its docstring.
        (Naturally this actually happened at one point.)
        """

        class S(plug._Step):
            pass

        str(S())


class TestPlugUnaccelerated(_DisableGC, unittest.TestCase):
    def test_PlugUnaccelerated(self):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_PlugUnaccelerated')

        agent_config, agent, thr = FakeAgent.boot(self.assertTrue)

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': d},
                'contrail-vrouter-agent': {'base_url': agent_config.base_url},
                'linux_net': {'dry_run': True},
            },
        )
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            agent.sync()

            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((plug._StepStatus.OK,))
            )
            del plug_ok

            # check port file contents
            port_fname = os.path.join(d, str(po.uuid))
            with open(port_fname, 'r') as fh:
                port_file = json.load(fh)
                self.assertEqual(port_file['system-name'], td['tap_name'])

            # check agent POST contents
            posts = []
            for r in agent.good_requests:
                if r[0] == 'POST':
                    posts.append(r[1])
            self.assertEqual(len(posts), 1)
            for post in posts:
                self.assertIn('system-name', post)
                self.assertEqual(post['system-name'], td['tap_name'])

            # check port vf address
            self.assertIsNone(po.vf)
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3},
                'tornado.access': {'INFO': 1},
            })

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((plug._StepStatus.OK,))
            )
            del unplug_ok

            # file should've been deleted
            self.assertFalse(os.path.exists(port_fname))
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3},
                'tornado.access': {'INFO': 1},
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        deletes = []
        for r in agent.good_requests:
            if r[0] == 'DELETE':
                deletes.append(r[1])
        self.assertEqual(len(deletes), 1)
        for deleted_uuid in deletes:
            self.assertEqual(deleted_uuid, uuid.UUID(td['uuid']))

        s.commit()

    def test_PlugUnaccelerated_agent_POST_fail_just_warns(self):
        # Main purpose of this test is to validate the test that when agent
        # file write is disabled/fails and agent POST fails (i.e., both at the
        # same time), we get an exception... and that this is not from one of
        # the substeps.

        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(
            d_root, 'test_PlugUnaccelerated_agent_POST_fail_just_warns'
        )

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': d},
                'contrail-vrouter-agent': {'base_url': _UNREACHABLE_AGENT_URL},
                'linux_net': {'dry_run': True},
            },
        )

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            # no agent :)
            # agent.sync()

            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.ERROR,
                ))
            )
            del plug_ok[plug._AgentPost]
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                ))
            )
            del plug_ok

            # check port file contents
            port_fname = os.path.join(d, str(po.uuid))
            with open(port_fname, 'r') as fh:
                port_file = json.load(fh)
                self.assertEqual(port_file['system-name'], td['tap_name'])

            # check port vf address
            self.assertIsNone(po.vf)

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3, 'ERROR': 1},
            })

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            # no agent :)
            # agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.ERROR,
                ))
            )
            del unplug_ok[plug._AgentPost]
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                ))
            )
            del unplug_ok

            # file should've been deleted
            self.assertFalse(os.path.exists(port_fname))

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3, 'ERROR': 1},
            })

        s.commit()

    def test_PlugUnaccelerated_agent_file_write_fail_just_warns(self):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_PlugUnaccelerated')
        os.makedirs(d)
        f_uncreatable = os.path.join(d, 'obstruction')
        with open(f_uncreatable, 'w') as fh:
            pass

        agent_config, agent, thr = FakeAgent.boot(self.assertTrue)

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': f_uncreatable},
                'contrail-vrouter-agent': {'base_url': agent_config.base_url},
                'linux_net': {'dry_run': True},
            },
        )

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            agent.sync()

            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.ERROR,
                ))
            )
            del plug_ok[plug._AgentFileWrite]
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                ))
            )
            del plug_ok

            # check agent POST contents
            posts = []
            for r in agent.good_requests:
                if r[0] == 'POST':
                    posts.append(r[1])
            self.assertEqual(len(posts), 1)
            for post in posts:
                self.assertIn('system-name', post)
                self.assertEqual(post['system-name'], td['tap_name'])

            # check port vf address
            self.assertIsNone(po.vf)

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3, 'ERROR': 1},
                'tornado.access': {'INFO': 1},
            })

        # need to get rid of the obstruction since _AgentFileWrite.undo()
        # raises on anything other than ENOENT (following Contrail
        # vrouter-port-control) (test:
        # TestAgentFileWrite.test_AgentFileWrite_undo_obstruction_raises())
        os.unlink(f_uncreatable)
        os.makedirs(f_uncreatable)

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((plug._StepStatus.OK,))
            )
            del unplug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3},
                'tornado.access': {'INFO': 1},
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        deletes = []
        for r in agent.good_requests:
            if r[0] == 'DELETE':
                deletes.append(r[1])
        self.assertEqual(len(deletes), 1)
        for deleted_uuid in deletes:
            self.assertEqual(deleted_uuid, uuid.UUID(td['uuid']))

        s.commit()

    def test_PlugUnaccelerated_agent_file_write_has_no_persist_option(self):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_PlugUnaccelerated')

        agent_config, agent, thr = FakeAgent.boot(self.assertTrue)

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': None},
                'contrail-vrouter-agent': {'base_url': agent_config.base_url},
                'linux_net': {'dry_run': True},
            },
        )
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            agent.sync()

            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.SKIP,
                ))
            )
            del plug_ok[plug._AgentFileWrite]
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                ))
            )
            del plug_ok

            # check agent POST contents
            posts = []
            for r in agent.good_requests:
                if r[0] == 'POST':
                    posts.append(r[1])
            self.assertEqual(len(posts), 1)
            for post in posts:
                self.assertIn('system-name', post)
                self.assertEqual(post['system-name'], td['tap_name'])

            # check port vf address
            self.assertIsNone(po.vf)

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 2},
                'tornado.access': {'INFO': 1},
            })

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.SKIP,
                ))
            )
            del unplug_ok[plug._AgentFileWrite]
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                ))
            )
            del unplug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 2},
                'tornado.access': {'INFO': 1},
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        deletes = []
        for r in agent.good_requests:
            if r[0] == 'DELETE':
                deletes.append(r[1])
        self.assertEqual(len(deletes), 1)
        for deleted_uuid in deletes:
            self.assertEqual(deleted_uuid, uuid.UUID(td['uuid']))

        s.commit()

    def test_PlugUnaccelerated_agent_file_write_and_POST_both_fail_raises(
        self
    ):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_PlugUnaccelerated')
        os.makedirs(d)
        f_uncreatable = os.path.join(d, 'obstruction')
        with open(f_uncreatable, 'w') as fh:
            pass

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': f_uncreatable},
                'contrail-vrouter-agent': {'base_url': _UNREACHABLE_AGENT_URL},
                'linux_net': {'dry_run': True},
            },
        )

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            with self.assertRaises(plug.CompleteAgentFailure):
                pl.plug(s, po)

            # check port vf address
            self.assertIsNone(po.vf)

            # check log messages
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'CRITICAL': 2, 'DEBUG': 6, 'ERROR': 3},
            })

            # no need to unplug, plug exception handler already unwound for us

        s.commit()

    def test_PlugUnaccelerated_agent_post_fail_and_no_persist_raises(self):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': None},
                'contrail-vrouter-agent': {'base_url': _UNREACHABLE_AGENT_URL},
                'linux_net': {'dry_run': True},
            },
        )

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            with self.assertRaises(plug.CompleteAgentFailure):
                pl.plug(s, po)

            # check port vf address
            self.assertIsNone(po.vf)

            # check log messages
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'CRITICAL': 1, 'DEBUG': 4, 'ERROR': 2},
            })

            # no need to unplug, plug exception handler already unwound for us

        s.commit()


class TestPlugUnacceleratedDPDK(_DisableGC, unittest.TestCase):
    def test_PlugUnaccelerated_DPDK(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_PlugUnaccelerated_DPDK')
        socket_path = os.path.join(
            d_root, 'test_PlugUnaccelerated_DPDK.socket'
        )

        def _tweak(ans):
            ans['vif_type'] = port.VIF_TYPE_VHOSTUSER
            ans['vhostuser_socket'] = socket_path

        td = _test_data(_tweak)
        po = port.Port(**td)
        s.add(po)

        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue, vhostuser_socket=socket_path
        )

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': d},
                'contrail-vrouter-agent': {'base_url': agent_config.base_url},
                'DPDK': {'socket_timeout_ms': 2000},
                'linux_net': {'dry_run': True},
            },
        )
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            agent.sync()

            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((plug._StepStatus.OK,))
            )
            del plug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 5},
                'tornado.access': {'INFO': 1},
            })

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((plug._StepStatus.OK,))
            )
            del unplug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3},
                'tornado.access': {'INFO': 1},
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    def _test_PlugUnaccelerated_DPDK_error(
        self, port_post_handler, expected_log_messages
    ):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        stem = '_test_PlugUnaccelerated_DPDK_error'
        d = os.path.join(d_root, stem)
        socket_path = os.path.join(d_root, stem + '.socket')

        def _tweak(ans):
            ans['vif_type'] = port.VIF_TYPE_VHOSTUSER
            ans['vhostuser_socket'] = socket_path

        td = _test_data(_tweak)
        po = port.Port(**td)
        s.add(po)

        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue, vhostuser_socket=socket_path,
            port_post_handler=port_post_handler
        )

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': d},
                'contrail-vrouter-agent': {'base_url': agent_config.base_url},
                'DPDK': {'socket_timeout_ms': 2000},
                'linux_net': {'dry_run': True},
            },
        )
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            with self.assertRaises(plug.SocketWaitError):
                pl.plug(s, po)

            agent.sync()
            self.assertEqual(lmc.count, expected_log_messages)

            # no need to unplug, plug exception handler already unwound for us

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    class _WrongSocketDPDKPostHandler(FakeAgent._PortPostHandler):
        def create_dpdk_socket(self):
            if self.vhostuser_socket is None:
                return

            # create wrong type of object
            fh = open(self.vhostuser_socket, 'w')
            fh.close()

    def test_PlugUnaccelerated_DPDK_wrong_socket_type(self):
        self._test_PlugUnaccelerated_DPDK_error(
            port_post_handler=self._WrongSocketDPDKPostHandler,
            expected_log_messages={
                _PLUG_LOGGER.name: {'DEBUG': 7, 'CRITICAL': 1},
                'tornado.access': {'INFO': 2},
            }
        )

    class _TimeoutDPDKPostHandler(FakeAgent._PortPostHandler):
        def create_dpdk_socket(self):
            # don't create any object
            pass

    def test_PlugUnaccelerated_DPDK_socket_timeout(self):
        self._test_PlugUnaccelerated_DPDK_error(
            port_post_handler=self._TimeoutDPDKPostHandler,
            expected_log_messages={
                _PLUG_LOGGER.name: {'DEBUG': 7, 'CRITICAL': 1},
                'tornado.access': {'INFO': 2},
            }
        )

    def test_PlugUnaccelerated_DPDK_do_not_wait_if_agent_POST_fails(self):
        """
        DPDK socket wait is supposed to be conditional on agent POST
        succeeding.
        """

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(
            d_root,
            'test_PlugUnaccelerated_DPDK_do_not_wait_if_agent_POST_fails'
        )
        socket_path = os.path.join(
            d_root,
            'test_PlugUnaccelerated_DPDK_do_not_wait_if_agent_POST_fails.'
            'socket'
        )

        def _tweak(ans):
            ans['vif_type'] = port.VIF_TYPE_VHOSTUSER
            ans['vhostuser_socket'] = socket_path

        td = _test_data(_tweak)
        po = port.Port(**td)
        s.add(po)

        pl = plug._PlugUnaccelerated(
            config={
                'vrouter-port-control': {'directory': d},
                'contrail-vrouter-agent': {'base_url': _UNREACHABLE_AGENT_URL},
                'DPDK': {'socket_timeout_ms': 2000},
                'linux_net': {'dry_run': True},
            },
        )

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            # no agent :)
            # agent.sync()

            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.SKIP,
                    plug._StepStatus.ERROR
                ))
            )
            del plug_ok[plug._AgentPost]
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.SKIP,
                ))
            )
            del plug_ok[plug._DPDKSocketWait]
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                ))
            )
            del plug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3, 'ERROR': 1},
            })

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            # no agent :)
            # agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.ERROR
                ))
            )
            del unplug_ok[plug._AgentPost]
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                ))
            )
            del unplug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 3, 'ERROR': 1},
            })

        s.commit()


class _FakeStep(plug._Step):
    def forward_action(self, session, port, journal):
        return plug._Action(
            lambda: self._do(session, port, journal),
            plug._format_getdoc(inspect.getdoc(self))
        )

    def reverse_action(self, session, port, journal):
        return plug._Action(
            lambda: self._undo(session, port, journal),
            plug._format_getdoc(inspect.getdoc(self))
        )


class _FakeCreateTAP(_FakeStep):
    """Create TAP interface (fake)."""

    def __init__(self, **kwds):
        super(_FakeCreateTAP, self).__init__(**kwds)
        self.do_count = self.undo_count = 0

    def _do(self, session, port, journal):
        self.do_count += 1
        return plug._StepStatus.OK

    def _undo(self, session, port, journal):
        self.undo_count += 1
        return plug._StepStatus.OK


class _FakeAttachDriver(_FakeStep):
    """Attach driver, e.g., igb_uio or pci-stub, to a device (fake)."""

    def _do(self, session, port, journal):
        return plug._StepStatus.OK

    def _undo(self, session, port, journal):
        raise NotImplementedError(type(self).__name__)


class _FakeAgentFileWrite(_FakeStep):
    """Write port information to vRouter agent database (fake)."""

    def _do(self, session, port, journal):
        return plug._StepStatus.OK

    def _undo(self, session, port, journal):
        raise NotImplementedError(type(self).__name__)


class _FakeAgentPost(_FakeStep):
    """Inform vRouter agent of port via HTTP POST (fake)."""

    def _do(self, session, port, journal):
        raise NotImplementedError(type(self).__name__)

    def _undo(self, session, port):
        return plug._StepStatus.OK


class PlugWithException(plug._PlugDriver):
    """Perform fake plug operation, raise exception during plugging."""

    STEP_TYPES = frozenset((
        _FakeAgentFileWrite,
        _FakeAgentPost,
        _FakeAttachDriver,
        _FakeCreateTAP,
        _FakeCreateTAP,
    ))


class TestExceptionDuringPlug(unittest.TestCase):
    def test_exception_during_plug(self):
        """(what happens when a plug step raises an exception?)"""
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        pl = PlugWithException()
        pl.set_steps(
            config={},
            steps=(
                _FakeCreateTAP(description='Create TAP (fake) (1/2).'),
                # _FakeAgentFileWrite(),  # raises during unwind
                _FakeAgentPost(),         # raises during plug
                _FakeCreateTAP(description='Create TAP (fake) (2/2).'),
            )
        )

        # preconditions
        self.assertEqual(pl.steps[0].do_count, 0)
        self.assertEqual(pl.steps[0].undo_count, 0)
        self.assertEqual(pl.steps[-1].do_count, 0)
        self.assertEqual(pl.steps[-1].undo_count, 0)

        # the regexp is to make sure that we are rethrowing the correct (first)
        # exception, not a subsequent exception during unwind.
        log_message_counter = LogMessageCounter()
        with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
            with self.assertRaisesRegexp(
                NotImplementedError, '_FakeAgentPost'
            ):
                pl.plug(s, po)

        self.assertEqual(log_message_counter.count, {
            _PLUG_LOGGER.name: {'DEBUG': 4, 'CRITICAL': 1}
        })
        self.assertEqual(pl.steps[0].do_count, 1)
        self.assertEqual(pl.steps[0].undo_count, 1)
        self.assertEqual(pl.steps[-1].do_count, 0)
        self.assertEqual(pl.steps[-1].undo_count, 0)

        s.commit()

    def test_exception_during_unwind(self):
        """(what happens when an error recovery step raises an exception?)"""
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        pl = PlugWithException()
        pl.set_steps(
            config={},
            steps=(
                _FakeCreateTAP(description='Create TAP (fake) (1/2).'),
                _FakeAgentFileWrite(),  # raises during unwind
                _FakeAgentPost(),       # raises during plug
                _FakeCreateTAP(description='Create TAP (fake) (2/2).'),
            )
        )

        # preconditions
        self.assertEqual(pl.steps[0].do_count, 0)
        self.assertEqual(pl.steps[0].undo_count, 0)
        self.assertEqual(pl.steps[-1].do_count, 0)
        self.assertEqual(pl.steps[-1].undo_count, 0)

        # the regexp is to make sure that we are rethrowing the correct (first)
        # exception, not a subsequent exception during unwind.
        log_message_counter = LogMessageCounter()
        with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
            with self.assertRaisesRegexp(
                NotImplementedError, '_FakeAgentPost'
            ):
                pl.plug(s, po)

        self.assertEqual(log_message_counter.count, {
            _PLUG_LOGGER.name: {'CRITICAL': 2, 'DEBUG': 7}
        })
        self.assertEqual(pl.steps[0].do_count, 1)
        self.assertEqual(pl.steps[0].undo_count, 1)
        self.assertEqual(pl.steps[-1].do_count, 0)
        self.assertEqual(pl.steps[-1].undo_count, 0)

        s.commit()


class TestExceptionDuringUnplug(unittest.TestCase):
    def test_exception_during_unplug(self):
        """(what happens when plugging succeeds, but unplugging fails?)"""

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        pl = PlugWithException()
        pl.set_steps(
            config={},
            steps=(
                _FakeCreateTAP(description='Create TAP (fake) (1/2).'),
                _FakeAttachDriver(),    # raises during unplug (2nd)
                _FakeAgentFileWrite(),  # raises during unplug (1st)
                # _FakeAgentPost(),     # raises during plug
                _FakeCreateTAP(description='Create TAP (fake) (2/2).'),
            )
        )

        # preconditions
        self.assertEqual(pl.steps[0].do_count, 0)
        self.assertEqual(pl.steps[0].undo_count, 0)
        self.assertEqual(pl.steps[-1].do_count, 0)
        self.assertEqual(pl.steps[-1].undo_count, 0)

        # the regexp is to make sure that we are rethrowing the correct (first)
        # exception, not a subsequent exception during unwind.
        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            pl.plug(s, po)
            with self.assertRaisesRegexp(
                NotImplementedError, '_FakeAgentFileWrite'
            ):
                pl.unplug(s, po)

        self.assertEqual(lmc.count, {
            _PLUG_LOGGER.name: {'DEBUG': 10, 'CRITICAL': 2}
        })
        self.assertEqual(pl.steps[-1].do_count, 1)
        self.assertEqual(pl.steps[-1].undo_count, 1)

        # should have made it all the way back to the beginning despite the
        # exception
        self.assertEqual(pl.steps[0].do_count, 1)
        self.assertEqual(pl.steps[0].undo_count, 1)

        s.commit()


class TestAgentFileWrite(unittest.TestCase):
    def test_AgentFileWrite(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        self.assertTrue(os.path.isdir(d_root))
        d = os.path.join(d_root, 'some/test/dir')
        self.assertFalse(os.path.exists(d))

        step = plug._AgentFileWrite()
        step.configure(directory=d)
        self.assertFalse(os.path.exists(d))
        do_ok = step.forward_action(s, po, {}).execute()
        self.assertIsInstance(do_ok, plug._StepStatus)
        self.assertTrue(do_ok.is_positive())
        self.assertTrue(os.path.isdir(d))
        self.assertTrue(os.path.isfile(os.path.join(d, str(po.uuid))))
        undo_ok = step.reverse_action(s, po, {}).execute()
        self.assertIsInstance(undo_ok, plug._StepStatus)
        self.assertTrue(undo_ok.is_positive())
        self.assertFalse(os.path.exists(os.path.join(d, str(po.uuid))))
        self.assertTrue(os.path.isdir(d))  # still supposed to exist

    def test_AgentFileWrite_undo_obstruction_raises(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        self.assertTrue(os.path.isdir(d_root))
        d = os.path.join(d_root, 'some/test/dir')
        self.assertFalse(os.path.exists(d))
        os.makedirs(d)
        f_uncreatable = os.path.join(d, 'obstruction')
        with open(f_uncreatable, 'w') as fh:
            pass

        step = plug._AgentFileWrite()
        step.configure(directory=f_uncreatable)
        with self.assertRaises(OSError) as cm:
            step.reverse_action(s, po, {}).execute()
        self.assertEqual(cm.exception.errno, errno.ENOTDIR)


_AGENT_POST_SCHEMA = {
    "author": basestring,
    "display-name": basestring,
    "id": basestring,
    "instance-id": basestring,
    "ip-address": basestring,
    "ip6-address": basestring,
    "mac-address": basestring,
    "rx-vlan-id": int,
    "system-name": basestring,
    "time": basestring,
    "tx-vlan-id": int,
    "type": int,
    "vm-project-id": basestring,
    "vn-id": basestring,
}


def _assert_valid_POST_body(body, schema):
    """
    Check that any key in `body` appears in `schema` with a compatible type
    (None or the listed type).
    """
    body_type = dict
    if not isinstance(body, body_type):
        raise ValueError(
            'POST body: type mismatch: expected {}, got {}'.format(
                body_type.__name__, type(body).__name__
            )
        )

    for k, v in body.iteritems():
        if v is not None and not isinstance(v, schema[k]):
            raise ValueError(
                'POST body: type mismatch on field "{}": expected {}, got {}'.
                format(
                    k, schema[k].__name__, type(v).__name__
                )
            )


class TestAgentPost(
    _DisableGC,
    unittest.TestCase,
    test_rest.AssertContentType,
    test_rest.AssertResponseCode
):
    def test_AgentPost(self):
        agent_config, agent, thr = FakeAgent.boot(self.assertTrue)

        step = plug._AgentPost()
        step.configure(base_url=agent_config.base_url)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        do_ok = step.forward_action(s, po, {}).execute()
        self.assertIsInstance(do_ok, plug._StepStatus)
        self.assertTrue(do_ok.is_positive())
        undo_ok = step.reverse_action(s, po, {}).execute()
        self.assertIsInstance(undo_ok, plug._StepStatus)
        self.assertTrue(undo_ok.is_positive())

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        self.assertEqual(len(agent.good_requests), 2)

        r = agent.good_requests[0]
        self.assertEqual(r[0], 'POST')
        _assert_valid_POST_body(schema=_AGENT_POST_SCHEMA, body=r[1])

        r = agent.good_requests[1]
        self.assertEqual(r[0], 'DELETE')
        self.assertEqual(r[1], po.uuid)

        s.commit()

    def test_AgentPost_connection_error(self):
        step = plug._AgentPost()
        step.configure(base_url=_UNREACHABLE_AGENT_URL)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        log_message_counter = LogMessageCounter()
        with attachLogHandler(logging.root, log_message_counter):
            do_ok = step.forward_action(s, po, {}).execute()
            # no agent :)
            # agent.sync()

            self.assertIsInstance(do_ok, plug._StepStatus)
            self.assertEqual(do_ok, plug._StepStatus.ERROR)  # want ERROR
            self.assertFalse(do_ok.is_positive())
            self.assertTrue(do_ok.is_negative())
            self.assertEqual(log_message_counter.count, {
                _PLUG_LOGGER.name: {'ERROR': 1},
            })

        log_message_counter = LogMessageCounter()
        with attachLogHandler(logging.root, log_message_counter):
            undo_ok = step.reverse_action(s, po, {}).execute()
            # no agent :)
            # agent.sync()

            self.assertEqual(undo_ok, plug._StepStatus.ERROR)  # want ERROR
            self.assertFalse(undo_ok.is_positive())
            self.assertTrue(undo_ok.is_negative())
            self.assertEqual(log_message_counter.count, {
                _PLUG_LOGGER.name: {'ERROR': 1},
            })

        s.commit()

    @staticmethod
    def make_ErrorPostHandler(d):
        class _ErrorPostHandler(FakeAgent._PortPostHandler):
            def post(self):
                args = [418, ]
                kwds = {'reason': "I'm a Teapot"}
                if 'finish' in d:
                    d['finish'](self, args, kwds)
                    self.set_status(*args, **kwds)
                else:
                    raise HTTPError(*args, **kwds)

        return _ErrorPostHandler

    @staticmethod
    def make_ErrorDeleteHandler(d):
        class _ErrorDeleteHandler(FakeAgent._PortDeleteHandler):
            def delete(self, *args):
                args = [418, ]
                kwds = {'reason': "I'm a Teapot"}
                if 'finish' in d:
                    d['finish'](self, args, kwds)
                    self.set_status(*args, **kwds)
                else:
                    raise HTTPError(*args, **kwds)

        return _ErrorDeleteHandler

    def test_AgentPost_HTTP_error(self):
        d = {}
        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue,
            port_post_handler=self.make_ErrorPostHandler(d),
            port_delete_handler=self.make_ErrorDeleteHandler(d),
        )

        step = plug._AgentPost()
        step.configure(base_url=agent_config.base_url)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        # default response
        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            with self.assertRaises(ValueError):
                step.forward_action(s, po, {}).execute()
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'ERROR': 1, 'INFO': 2}
            })

        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            with self.assertRaises(ValueError):
                step.reverse_action(s, po, {}).execute()
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'ERROR': 1, 'INFO': 2}
            })

        # multi-line JSON response
        def _mlj(self, args, kwds):
            self.set_header('Content-Type', 'application/json')
            self.write(json.dumps(
                {'hello': 'world', 'this': ['is', 'a', 'test']}, indent=4
            ))
        d['finish'] = _mlj

        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            with self.assertRaises(ValueError):
                step.forward_action(s, po, {}).execute()
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'ERROR': 1, 'INFO': 9}
            })

        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            with self.assertRaises(ValueError):
                step.reverse_action(s, po, {}).execute()
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'ERROR': 1, 'INFO': 9}
            })

        # image/gif response
        def _gif(self, args, kwds):
            self.set_header('Content-Type', 'image/gif')
            self.write('')
        d['finish'] = _gif

        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            with self.assertRaises(ValueError):
                step.forward_action(s, po, {}).execute()
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'ERROR': 1, 'INFO': 1}
            })

        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            with self.assertRaises(ValueError):
                step.reverse_action(s, po, {}).execute()
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'ERROR': 1, 'INFO': 1}
            })

        # one more... 404 is not supposed to raise an exception, but rather
        # just warn.
        def _404(self, args, kwds):
            args[0] = 404
            del kwds['reason']
            self.set_header('Content-Type', 'text/plain')
            self.write("teapot not found")
        d['finish'] = _404

        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            step.reverse_action(s, po, {}).execute()
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'ERROR': 1, 'INFO': 2}
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    class _BadPostHandler(FakeAgent._PortPostHandler):
        def finish_post_response(self):
            self.set_header('Content-Type', 'image/png')

    class _BadDeleteHandler(FakeAgent._PortDeleteHandler):
        def finish_delete_response(self):
            self.set_header('Content-Type', 'image/png')
            self.write('')

    def test_AgentPost_bad_content_type(self):
        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue,
            port_post_handler=TestAgentPost._BadPostHandler,
            port_delete_handler=TestAgentPost._BadDeleteHandler
        )

        step = plug._AgentPost()
        step.configure(base_url=agent_config.base_url)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        log_message_counter = LogMessageCounter()
        with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
            with self.assertRaises(ValueError):
                step.forward_action(s, po, {}).execute()
            self.assertEqual(log_message_counter.count, {
                _PLUG_LOGGER.name: {'ERROR': 1}
            })

        log_message_counter = LogMessageCounter()
        with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
            # delete is not supposed to care about the content-type
            undo_ok = step.reverse_action(s, po, {}).execute()
            self.assertIsInstance(undo_ok, plug._StepStatus)
            self.assertTrue(undo_ok.is_positive())
            self.assertEqual(log_message_counter.count, {})

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()


class TestAllocateVF(unittest.TestCase):
    def test_AllocateVF(self):
        pool = vf.Pool(
            fake_FallbackMap([pci.parse_pci_address('0000:00:00.0')])
        )
        step = plug._AllocateVF(vf_pool=pool)
        if hasattr(step, 'configure'):
            step.configure()

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        self.assertIsNone(po.vf)
        do_ok = step.forward_action(s, po, {}).execute()
        self.assertIsInstance(do_ok, plug._StepStatus)
        self.assertTrue(do_ok.is_positive())
        self.assertIsNotNone(po.vf)
        undo_ok = step.reverse_action(s, po, {}).execute()
        self.assertIsInstance(undo_ok, plug._StepStatus)
        self.assertTrue(undo_ok.is_positive())
        self.assertIsNone(po.vf)

        s.commit()

    def test_AllocateVF_failure(self):
        with attachLogHandler(_VF_LOGGER()):
            pool = vf.Pool(fallback.FallbackMap())

        step = plug._AllocateVF(vf_pool=pool)
        if hasattr(step, 'configure'):
            step.configure()

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        self.assertIsNone(po.vf)
        with attachLogHandler(_VF_LOGGER()):
            with self.assertRaises(vf.AllocationError):
                step.forward_action(s, po, {}).execute()

        undo_ok = step.reverse_action(s, po, {}).execute()
        self.assertIsInstance(undo_ok, plug._StepStatus)
        self.assertTrue(undo_ok.is_positive())
        self.assertIsNone(po.vf)

        s.commit()


class _CommandRecorder(object):
    def __init__(self, **kwds):
        super(_CommandRecorder, self).__init__(**kwds)
        self.cmds = []

    def record_cmd(self, cmd):
        self.cmds.append(cmd)


class TestBringUpFallback(unittest.TestCase):
    def test_BringUpFallback(self):
        fake_fallback_devname = 'nfp_v11.42'

        fallback_map = fallback.read_sysfs_fallback_map(
            _in='0001:01:02.3 0008:03:02.1 {} 14'.format(fake_fallback_devname)
        )
        pool = vf.Pool(fallback_map)

        allocate_step = plug._AllocateVF(vf_pool=pool)
        if hasattr(allocate_step, 'configure'):
            allocate_step.configure()

        cr = _CommandRecorder()
        bring_up_step = plug._BringUpFallback(fallback_map=fallback_map)
        bring_up_step.configure(execute=cr.record_cmd)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)

        allocate_ok = allocate_step.forward_action(s, po, {}).execute()
        self.assertIsInstance(allocate_ok, plug._StepStatus)
        self.assertTrue(allocate_ok.is_positive())
        self.assertFalse(allocate_ok.is_negative())

        bring_up_ok = bring_up_step.forward_action(s, po, {}).execute()
        self.assertIsInstance(bring_up_ok, plug._StepStatus)
        self.assertTrue(bring_up_ok.is_positive())
        self.assertFalse(bring_up_ok.is_negative())

        try:
            self.assertEqual(len(cr.cmds), 2)
        except AssertionError:
            print >>sys.stderr, cr.cmds
            raise

        self.assertIn('address', cr.cmds[0])
        self.assertIn(fake_fallback_devname, cr.cmds[0])
        self.assertIn('up', cr.cmds[1])
        self.assertIn(fake_fallback_devname, cr.cmds[1])

        cr = _CommandRecorder()
        bring_up_step = plug._BringUpFallback(fallback_map=fallback_map)
        bring_up_step.configure(execute=cr.record_cmd)
        bring_down_ok = bring_up_step.reverse_action(s, po, {}).execute()
        self.assertIsInstance(bring_down_ok, plug._StepStatus)
        self.assertTrue(bring_down_ok.is_positive())
        self.assertFalse(bring_down_ok.is_negative())

        try:
            self.assertEqual(len(cr.cmds), 1)
        except AssertionError:
            print >>sys.stderr, cr.cmds
            raise

        self.assertIn('down', cr.cmds[0])
        self.assertIn(fake_fallback_devname, cr.cmds[0])

        s.commit()


# this was not covered in the original set of test cases, which I
# noticed because there was a BUG comment left after I added the +OK,
# -ERROR, -SKIP return value checking on 2016-06-29.
def _preallocate_vf(session, port, fallback_map=None, vf_pool=None):
    if fallback_map is None:
        fallback_map = fallback.FallbackMap()
    if vf_pool is None:
        vf_pool = vf.Pool(fallback_map)

    # NOTE: If the current reservation expires then we should mark it
    # non expiring! Not just say "oh well the port already has a VF,
    # good enough!"
    expires = vf.calculate_expiration_datetime(timedelta(minutes=30))
    addr = vf_pool.allocate_vf(
        session, port.uuid, expires=expires, raise_on_failure=True
    )
    port.vf = session.query(vf.VF).get(addr)


class TestPlugSRIOV(_DisableGC, unittest.TestCase):
    def _test_PlugSRIOV(self, customize_port, expected_plug_logs):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_PlugSRIOV')

        sysfs = FakeAgent.Sysfs(root_dir=d_root)
        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue, sysfs=sysfs
        )

        fake_fallback_devname = 'teapot_19.7'
        self.assertNotEqual(td['tap_name'], fake_fallback_devname)
        addr = pci.parse_pci_address('02d1:07:15.d')
        fallback_map = fallback.read_sysfs_fallback_map(
            _in='0001:01:02.4 {} {} 6'.format(addr, fake_fallback_devname)
        )

        # Enable the fake Intel IOMMU (so that we pass the IOMMU check).
        _enable_fake_intel_iommu(d_root)

        if customize_port is not None:
            # Hook for testing "VF already allocated for this port"
            customize_port(fallback_map=fallback_map, session=s, port=po)

        cr = _CommandRecorder()
        pl = plug._PlugSRIOV(
            vf_pool=vf.Pool(fallback_map),
            config={
                '*': {
                    'root_dir': d_root,
                },
                'contrail-vrouter-agent': {
                    'base_url': agent_config.base_url,
                    'vif_sync': {
                        'timeout_ms': 10000,
                    },
                },
                'fallback': {'execute': cr.record_cmd},
                'vrouter-port-control': {'directory': d},
            },
        )
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            agent.sync()

            have_iommu_check = iommu_check is not None
            expected_plug_ok_values = (
                frozenset((plug._StepStatus.OK,)) if have_iommu_check
                else frozenset((plug._StepStatus.OK, plug._StepStatus.SKIP))
            )
            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()), expected_plug_ok_values
            )
            del plug_ok

            # check port file contents
            port_fname = os.path.join(d, str(po.uuid))
            with open(port_fname, 'r') as fh:
                port_file = json.load(fh)
                self.assertEqual(
                    port_file['system-name'], fake_fallback_devname
                )

            # check agent POST contents
            posts = []
            for r in agent.good_requests:
                if r[0] == 'POST':
                    posts.append(r[1])
            self.assertEqual(len(posts), 1)
            for post in posts:
                self.assertIn('system-name', post)
                self.assertEqual(post['system-name'], fake_fallback_devname)

            # check port vf address
            self.assertEqual(po.vf.addr, addr)
            self.assertEqual(lmc.count, expected_plug_logs)

            # and finally, the port should be marked non-expiring!
            self.assertIsNone(po.vf.expires)

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            try:
                self.assertEqual(
                    frozenset(unplug_ok.itervalues()),
                    frozenset((plug._StepStatus.OK,))
                )
            except AssertionError:
                print >>sys.stderr, unplug_ok
                raise

            del unplug_ok

            # file should've been deleted
            self.assertFalse(os.path.exists(port_fname))
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 6},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        deletes = []
        for r in agent.good_requests:
            if r[0] == 'DELETE':
                deletes.append(r[1])
        self.assertEqual(len(deletes), 1)
        for deleted_uuid in deletes:
            self.assertEqual(deleted_uuid, uuid.UUID(td['uuid']))

        s.commit()

    def test_PlugSRIOV(self):
        have_iommu_check = iommu_check is not None
        self._test_PlugSRIOV(
            customize_port=None,
            expected_plug_logs={
                _PLUG_LOGGER.name: {'DEBUG': 6 + have_iommu_check},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            }
        )

    def test_PlugSRIOV_with_VF_already_allocated(self):
        have_iommu_check = iommu_check is not None
        self._test_PlugSRIOV(
            customize_port=_preallocate_vf,
            expected_plug_logs={
                _PLUG_LOGGER.name: {'DEBUG': 6 + have_iommu_check},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            }
        )


class TestWaitForFirmwareVIFTable(_DisableGC, unittest.TestCase):
    def test_plug_timeout(self):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_plug_timeout')

        # Make a special sysfs that won't actually bother putting the interface
        # into the plugged interfaces list.
        sysfs = FakeAgent.Sysfs(root_dir=d_root)
        sysfs.add_vif_timeout = lambda: None
        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue, sysfs=sysfs
        )

        fake_fallback_devname = 'teapot_19.8'
        self.assertNotEqual(td['tap_name'], fake_fallback_devname)
        addr = pci.parse_pci_address('01d2:07:15.d')
        fallback_map = fallback.read_sysfs_fallback_map(
            _in='0004:02:01.0 {} {} 7'.format(addr, fake_fallback_devname)
        )

        # Enable the fake Intel IOMMU (so that we pass the IOMMU check).
        _enable_fake_intel_iommu(d_root)

        cr = _CommandRecorder()
        pl = plug._PlugSRIOV(
            vf_pool=vf.Pool(fallback_map),
            config={
                '*': {
                    'root_dir': d_root,
                },
                'contrail-vrouter-agent': {
                    'base_url': agent_config.base_url,
                    'vif_sync': {
                        # The purpose of this test is to hit the timeout, so
                        # we might as well make it short.
                        'timeout_ms': 100,
                    },
                },
                'fallback': {'execute': cr.record_cmd},
                'vrouter-port-control': {'directory': d},
            },
        )
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            with self.assertRaises(plug.InterfacePlugTimeoutError):
                pl.plug(s, po)

            agent.sync()

            have_iommu_check = iommu_check is not None

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {
                    'CRITICAL': 1, 'DEBUG': 8 + have_iommu_check
                },
                _VF_LOGGER.name: {'INFO': 2},
                'tornado.access': {'INFO': 1},
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    def test_unplug_timeout(self):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_unplug_timeout')

        # Make a special sysfs that will put the interface into the plugged
        # interfaces list, but will never take it out.
        sysfs = FakeAgent.Sysfs(root_dir=d_root)
        sysfs.add_vif_timeout = lambda: timedelta(milliseconds=1)
        sysfs.remove_vif_timeout = lambda: None
        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue, sysfs=sysfs
        )

        fake_fallback_devname = 'teapot_19.8'
        self.assertNotEqual(td['tap_name'], fake_fallback_devname)
        addr = pci.parse_pci_address('01d2:07:15.d')
        fallback_map = fallback.read_sysfs_fallback_map(
            _in='0004:02:01.0 {} {} 7'.format(addr, fake_fallback_devname)
        )

        # Enable the fake Intel IOMMU (so that we pass the IOMMU check).
        _enable_fake_intel_iommu(d_root)

        cr = _CommandRecorder()
        pl = plug._PlugSRIOV(
            vf_pool=vf.Pool(fallback_map),
            config={
                '*': {
                    'root_dir': d_root,
                },
                'contrail-vrouter-agent': {
                    'base_url': agent_config.base_url,
                    'vif_sync': {
                        # The purpose of this test is to hit the timeout, so
                        # we might as well make it short.
                        'timeout_ms': 1000,
                    },
                },
                'fallback': {'execute': cr.record_cmd},
                'vrouter-port-control': {'directory': d},
            },
        )

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            agent.sync()

            self.assertIsInstance(plug_ok, dict)

            have_iommu_check = iommu_check is not None
            expected_plug_ok_values = (
                frozenset((plug._StepStatus.OK,)) if have_iommu_check
                else frozenset((plug._StepStatus.OK, plug._StepStatus.SKIP))
            )
            self.assertEqual(
                frozenset(plug_ok.itervalues()), expected_plug_ok_values
            )
            del plug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 6 + have_iommu_check},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            })

        # Now do the actual unplug. This should log a timeout error, but NOT
        # throw an exception.
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((
                    plug._StepStatus.OK,
                    plug._StepStatus.ERROR,
                ))
            )
            del unplug_ok

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 5, 'ERROR': 1},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            })

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()


def _create_driver_tree(
    step, vendor=None, device=None, driver='igb_uio',
    stub_driver='pci-stub'
):
    device = device or random.randint(1, 65534)
    vendor = vendor or random.randint(1, 65534)

    driver_dir = step._driver_path(driver)
    plug.makedirs(driver_dir)
    stub_driver_dir = step._driver_path(stub_driver)
    plug.makedirs(stub_driver_dir)

    addr = _random_pci_address()
    device_dir = step._device_path(addr)
    plug.makedirs(device_dir)

    with open(os.path.join(device_dir, 'vendor'), 'w') as fh:
        print >>fh, hex(vendor)
    with open(os.path.join(device_dir, 'device'), 'w') as fh:
        print >>fh, hex(device)

    return (
        vendor, device, addr, device_dir, driver, driver_dir, stub_driver,
        stub_driver_dir
    )


class TestAttachDriver(unittest.TestCase):
    def _check_bind_file(self, d, f, addr):
        bind_fname = os.path.join(d, f)
        with open(bind_fname) as fh:
            bind_addr = fh.readlines()
            self.assertEqual(len(bind_addr), 1)
            self.assertRegexpMatches(
                bind_addr[0], r'.*\S\Z',
                msg='sysfs bind and unbind files should not end in whitespace'
            )
            bind_addr = bind_addr[0].strip()
            self.assertEqual(pci.parse_pci_address(bind_addr), addr)

        # make bind_fname unusable for future writes
        os.chmod(bind_fname, 0)
        return bind_fname

    def _assert_no_file(self, d, f):
        path = os.path.join(d, f)
        self.assertFalse(
            os.path.exists(path), '{} exists and it should not'.format(path)
        )

    def _check_new_id_file(self, d, f, expected_prefix):
        new_id_fname = os.path.join(d, f)
        with open(new_id_fname, 'r') as fh:
            new_id = fh.readlines()
            self.assertEqual(len(new_id), 1)
            new_id = new_id[0]

        actual_prefix = tuple(
            map(lambda s: int(s, 16), re.split(r'\s+', new_id.strip()))
        )[:len(expected_prefix)]

        self.assertEqual(expected_prefix, actual_prefix)

    def _test_AttachDriver(self, initial_bind):
        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_AttachDriver')

        step = plug._AttachDriver()
        step.configure()
        plug.apply_root_dir(step, d)

        (vendor, device, addr, device_dir, driver, driver_dir, stub_driver,
         stub_driver_dir) = _create_driver_tree(step)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)
        v = vf.VF(addr, po.uuid)
        po.vf = v

        # initial driver symlink
        driver_symlink = os.path.join(device_dir, 'driver')
        if initial_bind:
            if not isinstance(initial_bind, basestring):
                initial_bind = stub_driver
                initial_bind_dir = stub_driver_dir
                assert initial_bind, 'postcondition'
            else:
                # make sure symlink target exists
                initial_bind_dir = step._driver_path(initial_bind)
                plug.makedirs(initial_bind_dir)

        if initial_bind:
            os.symlink(
                os.path.join('../../drivers', initial_bind), driver_symlink
            )

        # --- Bind ---
        log_message_counter = LogMessageCounter()
        with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
            attach_ok = step.forward_action(s, po, {}).execute()
            self.assertIsInstance(attach_ok, plug._StepStatus)
            self.assertTrue(attach_ok.is_positive())

            if initial_bind == stub_driver:
                expected_logs = {'DEBUG': 1, 'INFO': 2}
            elif initial_bind:
                expected_logs = {'DEBUG': 1, 'INFO': 2, 'WARNING': 1}
            else:
                expected_logs = {'DEBUG': 1, 'INFO': 1, 'WARNING': 1}

            self.assertEqual(log_message_counter.count, {
                _PLUG_LOGGER.name: expected_logs
            })

        # check bind/unbind file contents
        rm = [self._check_bind_file(driver_dir, 'bind', addr)]

        if initial_bind:
            rm.append(
                self._check_bind_file(initial_bind_dir, 'unbind', addr)
            )
        else:
            self._assert_no_file(stub_driver_dir, 'unbind')

        self._assert_no_file(stub_driver_dir, 'bind')
        self._assert_no_file(driver_dir, 'unbind')

        # new_id file contents doesn't matter here

        # fixup driver symlink
        if initial_bind:
            os.unlink(driver_symlink)
        os.symlink(os.path.join('../../drivers', driver), driver_symlink)

        # remove files so we can tell if they got recreated
        for path in rm:
            os.unlink(path)

        # --- Unbind ---
        log_message_counter = LogMessageCounter()
        with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
            detach_ok = step.reverse_action(s, po, {}).execute()
            self.assertIsInstance(detach_ok, plug._StepStatus)
            self.assertTrue(detach_ok.is_positive())
            self.assertEqual(log_message_counter.count, {
                _PLUG_LOGGER.name: {'INFO': 2, 'DEBUG': 1}
            })

        # check bind/unbind file contents
        rm = [
            self._check_bind_file(stub_driver_dir, 'bind', addr),
            self._check_bind_file(driver_dir, 'unbind', addr),
        ]
        self._assert_no_file(stub_driver_dir, 'unbind')
        self._assert_no_file(driver_dir, 'bind')

        # check new_id file contents
        self._check_new_id_file(stub_driver_dir, 'new_id', (vendor, device))

        s.commit()

    def test_AttachDriver(self):
        self._test_AttachDriver(initial_bind=True)

    def test_AttachDriver_no_initial_bind(self):
        self._test_AttachDriver(initial_bind=False)

    def test_AttachDriver_wrong_initial_bind(self):
        self._test_AttachDriver(initial_bind='water')

    def test_AttachDriver_already_bound_to_correct_driver(self):
        """Not doing this correctly triggers VRT-413."""

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(
            d_root, 'test_AttachDriver_already_bound_to_correct_driver'
        )

        step = plug._AttachDriver()
        step.configure()
        plug.apply_root_dir(step, d)

        (vendor, device, addr, device_dir, driver, driver_dir, stub_driver,
         stub_driver_dir) = _create_driver_tree(step)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)
        v = vf.VF(addr, po.uuid)
        po.vf = v

        # initial driver symlink: already bound to the correct driver
        driver_symlink = os.path.join(device_dir, 'driver')
        initial_bind_dir = step._driver_path(driver)
        plug.makedirs(initial_bind_dir)
        os.symlink(
            os.path.join('../../drivers', driver), driver_symlink
        )

        # --- Bind ---
        with attachLogHandler(_PLUG_LOGGER(), LogMessageCounter()) as lmc:
            attach_ok = step.forward_action(s, po, {}).execute()
            self.assertIsInstance(attach_ok, plug._StepStatus)
            self.assertEqual(attach_ok, plug._StepStatus.SKIP)

            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 1, 'INFO': 1}
            })
            for record in lmc.logs:
                if record[1] != 'INFO':
                    continue
                self.assertRegexpMatches(
                    record[2], 'VF.*is already bound.*not rebinding'
                )

        # the bind and unbind files had better not exist
        self._assert_no_file(driver_dir, 'bind')
        self._assert_no_file(driver_dir, 'unbind')
        self._assert_no_file(stub_driver_dir, 'bind')
        self._assert_no_file(stub_driver_dir, 'unbind')

        s.commit()


class TestGetVifDevnames(unittest.TestCase):
    def _test_get_vif_devname(self, get_vif_devname):
        for i in xrange(0, 60):
            fake_fallback_devname = 'tomato{}'.format(random.randint(0, 999))
            fake_tapname = 'wrong'
            pf_addr = _random_pci_address()
            vf_addr = _random_pci_address()
            fallback_map = fallback.read_sysfs_fallback_map(
                _in='{} {} {} {}'.format(
                    pf_addr, vf_addr, fake_fallback_devname, i
                )
            )

            class Mock(object):
                pass

            po = Mock()
            po.vf = Mock()
            po.vf.addr = vf_addr
            po.tap_name = fake_tapname
            fn = get_vif_devname(fallback_map)
            devname = fn(po)
            self.assertIsInstance(devname, basestring)

            # An old buggy version of the virtio code was returning the
            # vhostuser socket path for the vif devname.
            self.assertFalse('/run/virtiorelayd/' in devname)

            self.assertEqual(devname, fake_fallback_devname)
            self.assertNotEqual(devname, fake_tapname)

    def test_sriov_get_vif_devname(self):
        self._test_get_vif_devname(plug._sriov_get_vif_devname)

    def test_virtio_get_vif_devname(self):
        self._test_get_vif_devname(plug._virtio_get_vif_devname)


@unittest.skipIf(
    FakeVirtIOServer is None, 'virtiorelay interface not installed'
)
class TestSetupVirtIO(_DisableGC, unittest.TestCase):
    @contextlib.contextmanager
    def _rig(self, cls, **kwds):
        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_SetupVirtIO/virtiorelayd')
        plug.makedirs(d)
        zmq_ep = 'ipc://' + os.path.join(d, 'port_control')

        pf_addr = _random_pci_address()
        vf_addr = _random_pci_address()
        fake_fallback_devname = 'tomato{}'.format(random.randint(0, 10000))
        fake_vf_number = random.randint(0, 59)
        fallback_map = fallback.read_sysfs_fallback_map(
            _in='{} {} {} {}'.format(
                pf_addr, vf_addr, fake_fallback_devname, fake_vf_number
            )
        )

        step = plug._SetupVirtIO(fallback_map=fallback_map)
        step.configure(ep=zmq_ep, rcvtimeo_ms=5000)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        po = port.Port(**_test_data())
        s.add(po)
        v = vf.VF(vf_addr, po.uuid)
        po.vf = v

        # Boot the fake server.
        kwds.setdefault('server_zmq_ep', zmq_ep)
        server, thr = cls.boot(assertTrue=self.assertTrue, **kwds)

        yield (step, s, po, server)

        server.stop()
        JOIN_TIMEOUT_SEC = 9
        thr.join(JOIN_TIMEOUT_SEC)
        self.assertFalse(
            thr.isAlive(),
            'server did not shut down within {}s'.format(JOIN_TIMEOUT_SEC)
        )

        s.commit()

    def test_SetupVirtIO_connect_timeout(self):
        with self._rig(FakeVirtIOServer, server_zmq_ep=None) as rig:
            step, s, po, server = rig

            log_message_counter = LogMessageCounter()
            with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
                with self.assertRaises(plug.VirtIORelayPlugError):
                    step.forward_action(s, po, {}).execute()

                self.assertEqual(log_message_counter.count, {
                    _PLUG_LOGGER.name: {'DEBUG': 2, 'ERROR': 1, 'INFO': 1}
                })

            self.assertEqual(server.request_types, {})
            self.assertEqual(len(server.request_log), 0)

    def test_SetupVirtIO_recv_timeout(self):
        with self._rig(FakeVirtIOServer) as rig:
            step, s, po, server = rig

            log_message_counter = LogMessageCounter()
            with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
                with self.assertRaises(plug.VirtIORelayPlugError):
                    step.forward_action(s, po, {}).execute()

                self.assertEqual(log_message_counter.count, {
                    _PLUG_LOGGER.name: {'DEBUG': 2, 'ERROR': 1, 'INFO': 1}
                })

            self.assertEqual(server.request_types, {'ADD': 1})
            self.assertEqual(len(server.request_log), 1)

    def test_SetupVirtIO_invalid_response(self):
        with self._rig(FakeVirtIOServer, handle_request=lambda req: '') as rig:
            step, s, po, server = rig

            log_message_counter = LogMessageCounter()
            with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
                with self.assertRaises(ValueError):
                    step.forward_action(s, po, {}).execute()

                self.assertEqual(log_message_counter.count, {
                    _PLUG_LOGGER.name: {'DEBUG': 2, 'ERROR': 1, 'INFO': 1}
                })

            self.assertEqual(server.request_types, {'ADD': 1})
            self.assertEqual(len(server.request_log), 1)

    def test_SetupVirtIO_incomplete_response(self):
        def h(request):
            response = relay.PortControlResponse()
            return response.SerializePartialToString()

        with self._rig(FakeVirtIOServer, handle_request=h) as rig:
            step, s, po, server = rig

            log_message_counter = LogMessageCounter()
            with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
                with self.assertRaises(ValueError):
                    step.forward_action(s, po, {}).execute()

                self.assertEqual(log_message_counter.count, {
                    _PLUG_LOGGER.name: {'DEBUG': 2, 'ERROR': 1, 'INFO': 1}
                })

            self.assertEqual(server.request_types, {'ADD': 1})
            self.assertEqual(len(server.request_log), 1)

    def test_SetupVirtIO_valid_error_response(self):
        def h(request):
            response = relay.PortControlResponse()
            response.status = response.ERROR
            response.error_code = 999
            response.error_code_source = 'moonbeams()'
            return response.SerializeToString()

        with self._rig(FakeVirtIOServer, handle_request=h) as rig:
            step, s, po, server = rig

            log_message_counter = LogMessageCounter()
            with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
                with self.assertRaisesRegexp(
                    plug.VirtIORelayPlugError, '.*moonbeams'
                ):
                    step.forward_action(s, po, {}).execute()

                self.assertEqual(log_message_counter.count, {
                    _PLUG_LOGGER.name: {'DEBUG': 2, 'ERROR': 1, 'INFO': 1}
                })

            self.assertEqual(server.request_types, {'ADD': 1})
            self.assertEqual(len(server.request_log), 1)

    def test_SetupVirtIO_valid_ok_response(self):
        def h(request):
            response = relay.PortControlResponse()
            response.status = response.OK
            return response.SerializeToString()

        with self._rig(FakeVirtIOServer, handle_request=h) as rig:
            step, s, po, server = rig

            log_message_counter = LogMessageCounter()
            with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
                plug_ok = step.forward_action(s, po, {}).execute()
                self.assertIsInstance(plug_ok, plug._StepStatus)
                self.assertTrue(plug_ok.is_positive())

                self.assertEqual(log_message_counter.count, {
                    _PLUG_LOGGER.name: {'DEBUG': 2, 'INFO': 2}
                })

            self.assertEqual(server.request_types, {'ADD': 1})
            self.assertEqual(len(server.request_log), 1)

            log_message_counter = LogMessageCounter()
            with attachLogHandler(_PLUG_LOGGER(), log_message_counter):
                unplug_ok = step.reverse_action(s, po, {}).execute()
                self.assertIsInstance(unplug_ok, plug._StepStatus)
                self.assertTrue(unplug_ok.is_positive())

                self.assertEqual(log_message_counter.count, {
                    _PLUG_LOGGER.name: {'DEBUG': 2, 'INFO': 2}
                })

            self.assertEqual(server.request_types, {'ADD': 1, 'REMOVE': 1})
            self.assertEqual(len(server.request_log), 2)


@unittest.skipIf(
    FakeVirtIOServer is None, 'virtiorelay interface not installed'
)
class TestPlugVirtIO(_DisableGC, unittest.TestCase):
    def test_virtio_get_vif_devname(self):
        fake_fallback_devname = 'tomato75'
        fake_tapname = 'nope'
        pf_addr = pci.parse_pci_address('0006:01:02.3')
        vf_addr = pci.parse_pci_address('0008:03:02.2')
        fallback_map = fallback.read_sysfs_fallback_map(
            _in='{} {} {} 17'.format(pf_addr, vf_addr, fake_fallback_devname)
        )

        class Mock(object):
            pass

        po = Mock()
        po.vf = Mock()
        po.vf.addr = vf_addr
        po.tap_name = fake_tapname
        fn = plug._virtio_get_vif_devname(fallback_map)
        devname = fn(po)
        self.assertIsInstance(devname, basestring)
        self.assertEqual(devname, fake_fallback_devname)
        self.assertNotEqual(devname, fake_tapname)

    def _test_PlugVirtIO(
        self, expected_plug_logs, customize_port=None, customize_sysfs=None
    ):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        td = _test_data()
        po = port.Port(**td)
        s.add(po)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_PlugVirtIO')
        control_d = os.path.join(d, 'virtiorelayd')
        plug.makedirs(control_d)
        zmq_ep = 'ipc://' + os.path.join(control_d, 'port_control')

        # Setup the fake PCI device.
        pf_addr = _random_pci_address()
        vf_addr = _random_pci_address()
        fake_vf_number = random.randint(0, 59)

        # Setup the fake driver tree.
        uio_dir = os.path.join(d, 'sys/bus/pci/drivers/igb_uio')
        plug.makedirs(uio_dir)
        pci_stub_dir = os.path.join(d, 'sys/bus/pci/drivers/pci-stub')
        plug.makedirs(pci_stub_dir)

        device_dir = os.path.join(d, 'sys/bus/pci/devices/{}'.format(vf_addr))
        plug.makedirs(device_dir)

        device = random.randint(1, 65534)
        vendor = random.randint(1, 65534)

        with open(os.path.join(device_dir, 'vendor'), 'w') as fh:
            print >>fh, hex(vendor)
        with open(os.path.join(device_dir, 'device'), 'w') as fh:
            print >>fh, hex(device)

        # Enable the fake IOMMU.
        _enable_fake_intel_iommu(root_dir=d, pt=True)

        # Setup other fake sysfs features.
        if customize_sysfs:
            customize_sysfs(root_dir=d)

        # Boot the fake agent.
        sysfs = FakeAgent.Sysfs(root_dir=d)
        agent_config, agent, agent_thr = FakeAgent.boot(
            self.assertTrue,
            sysfs=sysfs,

            # Increase the maximum allowed execution time for the agent
            # (default: 10s).
            #
            # The VirtIO plugging tests can take ~5s on a slow machine (which
            # used to be the default timeout; it's subsequently been raised to
            # 10s). This causes sporadic (rare) "IOLoop is closing" runtime
            # errors (if the timeout occurs before agent.sync()), and/or
            # InterfacePlugTimeoutErrors (if the timeout occurs before an
            # interface plug completes).
            #
            # 30s is still execution speed dependent but is long enough that it
            # should not cause problems during normal operation.
            run_timedelta=timedelta(seconds=30),
        )

        # Boot the fake virtiorelayd.
        def h(request):
            response = relay.PortControlResponse()
            response.status = response.OK
            return response.SerializeToString()

        virtiorelayd, virtiorelayd_thr = FakeVirtIOServer.boot(
            assertTrue=self.assertTrue, server_zmq_ep=zmq_ep, handle_request=h
        )

        fake_fallback_devname = 'teapot_{}.{}'.format(
            random.randint(0, 99), random.randint(0, 99)
        )
        self.assertNotEqual(td['tap_name'], fake_fallback_devname)
        fallback_map = fallback.read_sysfs_fallback_map(
            _in='{} {} {} {}'.format(
                pf_addr, vf_addr, fake_fallback_devname, fake_vf_number
            )
        )

        if customize_port is not None:
            # Hook for testing "VF already allocated for this port"
            customize_port(fallback_map=fallback_map, session=s, port=po)

        cr = _CommandRecorder()
        pl = plug._PlugVirtIO(
            vf_pool=vf.Pool(fallback_map),
            config={
                '*': {
                    'root_dir': d,
                },
                'contrail-vrouter-agent': {
                    'base_url': agent_config.base_url,
                    'vif_sync': {
                        'timeout_ms': 10000,
                    },
                },
                'fallback': {'execute': cr.record_cmd},
                'virtio.zmq': {'ep': zmq_ep},
                'vrouter-port-control': {'directory': d},
            },
        )

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug_ok = pl.plug(s, po)
            agent.sync()

            self.assertIsInstance(plug_ok, dict)
            self.assertEqual(
                frozenset(plug_ok.itervalues()),
                frozenset((plug._StepStatus.OK,))
            )
            del plug_ok

            # check port file contents
            port_fname = os.path.join(d, str(po.uuid))
            with open(port_fname, 'r') as fh:
                port_file = json.load(fh)
                self.assertEqual(
                    port_file['system-name'], fake_fallback_devname
                )

            # check agent POST contents
            posts = []
            for r in agent.good_requests:
                if r[0] == 'POST':
                    posts.append(r[1])
            self.assertEqual(len(posts), 1)
            for post in posts:
                self.assertIn('system-name', post)
                self.assertEqual(post['system-name'], fake_fallback_devname)

            # check port vf address
            self.assertEqual(po.vf.addr, vf_addr)
            self.assertEqual(lmc.count, expected_plug_logs)

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            unplug_ok = pl.unplug(s, po)
            agent.sync()

            self.assertIsInstance(unplug_ok, dict)
            self.assertEqual(
                frozenset(unplug_ok.itervalues()),
                frozenset((plug._StepStatus.OK,))
            )
            del unplug_ok

            # file should've been deleted
            self.assertFalse(os.path.exists(port_fname))
            self.assertEqual(lmc.count, {
                _PLUG_LOGGER.name: {'DEBUG': 11, 'INFO': 3, 'WARNING': 1},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            })

        JOIN_TIMEOUT_SEC = 9
        virtiorelayd.stop()
        virtiorelayd_thr.join(JOIN_TIMEOUT_SEC)
        self.assertFalse(
            virtiorelayd_thr.isAlive(),
            'virtiorelayd did not shut down within {}s'.
            format(JOIN_TIMEOUT_SEC)
        )

        agent.stop()
        agent_thr.join(JOIN_TIMEOUT_SEC)
        self.assertFalse(
            agent.timeout or agent_thr.isAlive(),
            'agent did not shut down within {}s'.format(JOIN_TIMEOUT_SEC)
        )

        deletes = []
        for r in agent.good_requests:
            if r[0] == 'DELETE':
                deletes.append(r[1])
        self.assertEqual(len(deletes), 1)
        for deleted_uuid in deletes:
            self.assertEqual(deleted_uuid, uuid.UUID(td['uuid']))

        s.commit()

    def test_PlugVirtIO(self):
        self._test_PlugVirtIO(
            customize_port=None,
            expected_plug_logs={
                _PLUG_LOGGER.name: {'DEBUG': 12, 'INFO': 3, 'WARNING': 1},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            }
        )

    def test_PlugVirtIO_with_VF_already_allocated(self):
        self._test_PlugVirtIO(
            customize_port=_preallocate_vf,
            expected_plug_logs={
                _PLUG_LOGGER.name: {'DEBUG': 12, 'INFO': 3, 'WARNING': 1},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            },
        )

    def test_PlugVirtIO_with_KSM_warning(self):
        for i in (0, 1):
            def _enable_fake_ksm(root_dir):
                dpath = os.path.join(root_dir, 'sys/kernel/mm/ksm')
                os.makedirs(dpath)

                fpath = os.path.join(dpath, 'run')
                with open(fpath, 'w') as fh:
                    print >>fh, i

            self._test_PlugVirtIO(
                customize_sysfs=_enable_fake_ksm,
                expected_plug_logs={
                    _PLUG_LOGGER.name: {
                        'DEBUG': 12, 'INFO': 3, 'WARNING': 1 + i
                    },
                    _VF_LOGGER.name: {'INFO': 1},
                    'tornado.access': {'INFO': 1},
                },
            )


class TestPlugAPI(_DisableGC, unittest.TestCase):
    def _test_plug_api(
        self, expected_logs, _pm_customize=None, _d_customize=None, _cm=None
    ):
        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        class Mock(object):
            pass

        u = uuid.uuid1()
        if _pm_customize:
            # customize (e.g., pre-create) plug mode.
            _pm_customize(s, u)

        conf = Mock()
        conf.uuid = u
        conf.instance_uuid = uuid.uuid1()
        conf.vn_uuid = uuid.uuid1()
        conf.vm_project_uuid = uuid.uuid1()
        conf.ip_address = '123.4.5.6'
        conf.ipv6_address = None
        conf.vm_name = 'hi there'
        conf.mac = RandMac().generate()
        conf.tap_name = config.default_devname('tap', conf.uuid)
        conf.port_type = 'NovaVMPort'
        conf.vif_type = 'Vrouter'
        conf.tx_vlan_id = conf.rx_vlan_id = -1
        conf.vhostuser_socket = None

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, '_test_plug_api')
        os.makedirs(d)
        if _d_customize:
            # customize port database directory
            _d_customize(d)

        agent_config, agent, thr = FakeAgent.boot(self.assertTrue)
        conf.no_persist = False
        conf_agent = Mock()
        setattr(conf, 'contrail-agent', conf_agent)
        conf_agent.port_dir = d
        conf_agent.api_ep = agent_config.base_url
        conf_linux_net = Mock()
        setattr(conf, 'iproute2', conf_linux_net)
        conf_linux_net.dry_run = True

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            def _fn():
                fallback_map = fallback.FallbackMap()

                # Check if the test has already configured a plug mode for us.
                # If not, configure one.
                pre_configured_plug_mode = one_or_none(
                    s.query(port.PlugMode).
                    filter(port.PlugMode.neutron_port == conf.uuid)
                )

                if pre_configured_plug_mode is None:
                    _select_plug_mode_for_port(
                        s, conf.uuid, vf.Pool(fallback_map),
                        (PM.unaccelerated,), conf.vif_type, conf.port_type,
                        root_dir=d, hw_acceleration_mode=PM.unaccelerated,
                    )

                # Now actually plug the port.
                plug.plug_port(s, vf.Pool(fallback_map), conf, _root_dir=d)

            if _cm:
                with _cm:
                    _fn()
            else:
                _fn()

            agent.sync()

        self.assertEqual(lmc.count, expected_logs)

        # Unplug is tested separately.

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    def test_plug_new_port(self):
        """Test plugging a port that doesn't exist yet."""

        self._test_plug_api(
            expected_logs={
                _CONFIG_LOGGER.name: {'INFO': 1},
                _PLUG_LOGGER.name: {'DEBUG': 4, 'INFO': 2},
                _VF_LOGGER.name: {'WARNING': 2},
                'tornado.access': {'INFO': 1},
            },
        )

    @unittest.skipIf(
        os.geteuid() == 0, "root can still write to files with mode 0"
    )
    def test_plug_new_port_with_warnings(self):
        """
        Test plugging a port that doesn't exist yet, with a recoverable error.
        """

        def chmod_0(d):
            # Make plug unable to write the port file.
            os.chmod(d, 0)

        self._test_plug_api(
            expected_logs={
                _CONFIG_LOGGER.name: {'INFO': 1},
                _PLUG_LOGGER.name: {
                    'DEBUG': 4, 'ERROR': 1, 'WARNING': 1, 'INFO': 1
                },
                _VF_LOGGER.name: {'WARNING': 2},
                'tornado.access': {'INFO': 1},
            },
            _d_customize=chmod_0,
        )

    def test_plug_old_port(self):
        """Test plugging a port that was pre-configured with a plug mode."""

        def precreate_unaccel_pm(session, uuid):
            pm = port.PlugMode(neutron_port=uuid, mode=PM.unaccelerated)
            session.add(pm)
            session.commit()

        self._test_plug_api(
            expected_logs={
                _PLUG_LOGGER.name: {'DEBUG': 4, 'INFO': 2},
                _VF_LOGGER.name: {'WARNING': 1},
                'tornado.access': {'INFO': 1},
            },
            _pm_customize=precreate_unaccel_pm,
        )

    def test_plug_old_port_mode_conflict(self):
        """
        Test plugging a port that was pre-configured with a conflicting plug
        mode.

        In the pre-VRT-536 code, this would raise a PlugModeError because the
        unaccelerated plug mode was marked "mandatory" and the port was already
        configured for SR-IOV.

        In the post-VRT-536 code, this is supposed to raise a
        vf.AllocationError because plug_port() is no longer able to change an
        existing plug mode at all, so it will honor the SR-IOV mode (and
        _test_plug_api() doesn't set up any VF resources for us).
        """

        def precreate_SRIOV_pm(session, uuid):
            pm = port.PlugMode(neutron_port=uuid, mode=PM.SRIOV)
            session.add(pm)
            session.commit()

        have_iommu_check = iommu_check is not None

        self._test_plug_api(
            expected_logs={
                _PLUG_LOGGER.name: {
                    'INFO': 1, 'CRITICAL': 1, 'DEBUG': 2 + have_iommu_check
                },
                _VF_LOGGER.name: {'ERROR': 1, 'WARNING': 1},
            },
            _pm_customize=precreate_SRIOV_pm,
            _cm=self.assertRaises(vf.AllocationError),
            _d_customize=_enable_fake_intel_iommu,
        )


class TestUnplugAPI(_DisableGC, unittest.TestCase):
    def _test_unplug_api(
        self, mode, expected_logs, nfp_status, physical_vif_count,
        fallback_map=None, include_vif_sync_options=False, _d_customize=None
    ):
        if fallback_map is None:
            fallback_map = _make_fake_sysfs_fallback_map()

        self.maxDiff = None

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        class Mock(object):
            pass

        u = uuid.uuid1()

        conf = Mock()
        conf.uuid = u
        conf.instance_uuid = uuid.uuid1()
        conf.vn_uuid = uuid.uuid1()
        conf.vm_project_uuid = uuid.uuid1()
        conf.ip_address = '123.4.5.6'
        conf.ipv6_address = None
        conf.vm_name = 'hi there'
        conf.mac = RandMac().generate()
        conf.tap_name = config.default_devname('tap', conf.uuid)
        conf.port_type = 'NovaVMPort'
        conf.vif_type = 'Vrouter'
        conf.tx_vlan_id = conf.rx_vlan_id = -1
        conf.vhostuser_socket = None

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, '_test_unplug_api')
        os.makedirs(d)

        setup_fake_sysfs = FakeSysfs(
            nfp_status=nfp_status, physical_vif_count=physical_vif_count,
            _root_dir=d
        )

        sysfs = FakeAgent.Sysfs(root_dir=d)
        agent_config, agent, thr = FakeAgent.boot(
            self.assertTrue, sysfs=sysfs
        )
        conf.no_persist = False
        conf_agent = Mock()
        setattr(conf, 'contrail-agent', conf_agent)
        conf_agent.port_dir = d
        conf_agent.api_ep = agent_config.base_url
        if include_vif_sync_options:
            setattr(conf_agent, 'vif-sync-options', {
                'timeout_ms': 10000,
            })

        conf.iproute2 = Mock()
        conf.iproute2.dry_run = True

        if _d_customize:
            # customize port database/fake sysfs directory (e.g., pretend that
            # various drivers are loaded/unloaded or the IOMMU is set to a
            # specific mode)
            _d_customize(d)

        vf_pool = vf.Pool(fallback_map)

        with attachLogHandler(logging.root):
            _select_plug_mode_for_port(
                s, conf.uuid, vf_pool, (mode,), conf.vif_type, conf.port_type,
                root_dir=d, hw_acceleration_mode=mode
            )
            plug.plug_port(s, vf.Pool(fallback_map), conf, _root_dir=d)
            agent.sync()

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug.unplug_port(s, vf_pool, conf, _root_dir=d)
            agent.sync()

        self.assertEqual(lmc.count, expected_logs)

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    def test_unaccelerated_unplug(self):
        """Test of unplugging an unaccelerated port."""

        self._test_unplug_api(
            PM.unaccelerated,
            expected_logs={
                _PLUG_LOGGER.name: {'DEBUG': 3, 'INFO': 4},
                'tornado.access': {'INFO': 1},
            },
            nfp_status=1,
            physical_vif_count=1,
        )

    def test_SRIOV_unplug(self):
        """Test of unplugging an SR-IOV port."""

        self._test_unplug_api(
            PM.SRIOV,
            expected_logs={
                _PLUG_LOGGER.name: {'DEBUG': 6, 'INFO': 5},
                _VF_LOGGER.name: {'INFO': 1},
                'tornado.access': {'INFO': 1},
            },
            nfp_status=1,
            physical_vif_count=1,
            include_vif_sync_options=True,
            _d_customize=_enable_fake_intel_iommu,
        )

    def _test_only_configured_unplug(
        self, mode, expected_logs, _d_customize=None
    ):
        """
        Really just to exercise the code path of unplugging a port that had a
        plug mode reservation, but no port associated.
        """

        self.maxDiff = None

        fallback_map = _make_fake_sysfs_fallback_map()
        vf_pool = vf.Pool(fallback_map)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        class Mock(object):
            pass

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, '_test_only_configured_unplug')
        os.makedirs(d)
        if _d_customize:
            _d_customize(d)

        conf = Mock()
        conf.uuid = uuid.uuid1()

        # Create the reservation
        _select_plug_mode_for_port(
            s, conf.uuid, vf_pool, (mode,), vif_type='vrouter',
            virt_type='kvm', root_dir=d, hw_acceleration_mode=mode,
        )

        agent_config, agent, thr = FakeAgent.boot(self.assertTrue)
        conf.no_persist = False
        conf_agent = Mock()
        setattr(conf, 'contrail-agent', conf_agent)
        conf_agent.port_dir = d
        conf_agent.api_ep = agent_config.base_url

        conf.iproute2 = Mock()
        conf.iproute2.dry_run = True

        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug.unplug_port(s, vf.Pool(fallback_map), conf, _root_dir=d)
            agent.sync()

        self.assertEqual(lmc.count, expected_logs)

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    def test_only_configured_unplug_unaccelerated(self):
        self._test_only_configured_unplug(
            mode=PM.unaccelerated,
            expected_logs={
                _PLUG_LOGGER.name: {'INFO': 3, 'WARNING': 1, 'DEBUG': 2},
                'tornado.access': {'WARNING': 1},
            }
        )

    def test_only_configured_unplug_SRIOV(self):
        def enable_acceleration(d):
            setup_fake_sysfs = FakeSysfs(
                _root_dir=d, physical_vif_count=1, nfp_status=1
            )

        self._test_only_configured_unplug(
            mode=PM.SRIOV,
            expected_logs={
                _PLUG_LOGGER.name: {'INFO': 4, 'WARNING': 1, 'DEBUG': 2},
                'tornado.access': {'WARNING': 1},
            },
            _d_customize=enable_acceleration,
        )

    def _test_unplugged_port_unplug(
        self, mode, expected_logs, _d_customize=None, create=True
    ):
        """
        Really just to exercise the code path of unplugging a port that was not
        plugged in (i.e., had no plug mode reservation).
        """

        self.maxDiff = None

        fallback_map = _make_fake_sysfs_fallback_map()
        vf_pool = vf.Pool(fallback_map)

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        class Mock(object):
            pass

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, '_test_unplugged_port_unplug')
        os.makedirs(d)
        if _d_customize:
            _d_customize(d)

        u = uuid.uuid1()

        conf = Mock()
        conf.uuid = u

        # Create the port. If "create" is false this will end up acting like a
        # fake port.
        p = port.Port(
            uuid=u,
            instance_uuid=uuid.uuid1(),
            vn_uuid=uuid.uuid1(),
            vm_project_uuid=uuid.uuid1(),
            ip_address='123.4.5.6',
            ipv6_address=None,
            vm_name='hi there',
            mac=RandMac().generate(),
            tap_name=config.default_devname('tap', u),
            port_type='NovaVMPort',
            vif_type='Vrouter',
            tx_vlan_id=-1,
            rx_vlan_id=-1,
            vhostuser_socket=None,
        )
        if create:
            s.add(p)
            s.commit()

        conf.hw_acceleration_mode = mode

        agent_config, agent, thr = FakeAgent.boot(self.assertTrue)
        conf.no_persist = False
        conf_agent = Mock()
        setattr(conf, 'contrail-agent', conf_agent)
        conf_agent.port_dir = d
        conf_agent.api_ep = agent_config.base_url

        conf.iproute2 = Mock()
        conf.iproute2.dry_run = True

        # NOTE: The agent should return a 404 for this port, since it was never
        # actually registered with the agent. We want that particular code to
        # be a warning and NOT raised as an exception (analogous with the
        # ENOENT for deleting port files).
        with attachLogHandler(logging.root, LogMessageCounter()) as lmc:
            plug.unplug_port(s, vf.Pool(fallback_map), conf, _root_dir=d)
            agent.sync()

        self.assertEqual(lmc.count, expected_logs)

        agent.stop()
        thr.join()
        self.assertFalse(agent.timeout, 'agent did not shut down within 10s')

        s.commit()

    def test_unplugged_port_unplug(self):
        self._test_unplugged_port_unplug(
            mode=PM.unaccelerated,
            expected_logs={
                _PLUG_LOGGER.name: {'INFO': 3, 'WARNING': 1, 'DEBUG': 2},
                'tornado.access': {'WARNING': 1},
            }
        )

    def test_nonexistent_port_unplug(self):
        """Check that the agent still gets messages even if the port is unknown
        (VRT-604)."""
        self._test_unplugged_port_unplug(
            mode=PM.SRIOV,
            create=False,
            expected_logs={
                _PLUG_LOGGER.name: {'DEBUG': 3, 'INFO': 2},
                'tornado.access': {'WARNING': 1},
            }
        )


class TestGetPlugDriver(unittest.TestCase):
    def test_get_plug_driver(self):
        fallback_map = _make_fake_sysfs_fallback_map()
        vf_pool = vf.Pool(fallback_map)

        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'test_get_plug_driver')

        class Mock(object):
            pass

        # minimal possible config to not crash
        conf = Mock()
        conf.no_persist = False

        conf_agent = Mock()
        setattr(conf, 'contrail-agent', conf_agent)
        conf_agent.port_dir = d
        conf_agent.api_ep = _UNREACHABLE_AGENT_URL

        conf_virtio = Mock()
        setattr(conf, 'virtio-relay', conf_virtio)
        conf_virtio.zmq_ep = 'nonexistent:///'
        conf_virtio.zmq_receive_timeout = timedelta(seconds=1)
        conf_virtio.driver = 'igb_uio'
        conf_virtio.stub_driver = 'pci-stub'

        d = plug._get_plug_driver(PM.unaccelerated, vf_pool, conf)
        self.assertIsInstance(d, plug._PlugUnaccelerated)

        d = plug._get_plug_driver(PM.SRIOV, vf_pool, conf)
        self.assertIsInstance(d, plug._PlugSRIOV)

        d = plug._get_plug_driver(PM.VirtIO, vf_pool, conf)
        self.assertIsInstance(d, plug._PlugVirtIO)

        with self.assertRaises(AssertionError):
            plug._get_plug_driver('waterpark', vf_pool, conf)


if __name__ == '__main__':
    unittest.main()
