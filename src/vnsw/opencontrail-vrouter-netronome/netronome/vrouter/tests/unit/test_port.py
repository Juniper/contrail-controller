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

import unittest

from datetime import datetime, timedelta
from sqlalchemy.orm.session import sessionmaker

import logging
import os
import random
import tempfile
import uuid

from netronome.vrouter import (database, pci, plug_modes as PM, port, vf)
from netronome.vrouter.sa.helpers import one_or_none
from netronome.vrouter.sa.sqlite import set_sqlite_synchronous_off
from netronome.vrouter.tests.helpers.config import _random_pci_address
from netronome.vrouter.tests.helpers.vf import *
from netronome.vrouter.tests.randmac import RandMac
from netronome.vrouter.tests.unit import *

_VF_LOGGER = make_getLogger('netronome.vrouter.vf')


def _VF_LMC(cls=LogMessageCounter):
    return attachLogHandler(_VF_LOGGER(), cls())


if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'port.'
else:
    _TMP_PREFIX = 'netronome.vrouter.tests.unit.test_port.'


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


class TestTc(unittest.TestCase):
    def test_PortTypeTc(self):
        tc = port.PortTypeTc()
        self.assertTrue(tc.check('NameSpacePort'))
        self.assertTrue(tc.check('NovaVMPort'))
        self.assertFalse(tc.check('hello'))
        self.assertIsNone(tc.validate('NameSpacePort'))
        self.assertIsNone(tc.validate('NovaVMPort'))
        self.assertIsNotNone(tc.validate('hello'))

    def test_VifTypeTc(self):
        tc = port.VifTypeTc()
        self.assertTrue(tc.check('VhostUser'))
        self.assertTrue(tc.check('Vrouter'))
        self.assertFalse(tc.check('hello'))
        self.assertIsNone(tc.validate('VhostUser'))
        self.assertIsNone(tc.validate('Vrouter'))
        self.assertIsNotNone(tc.validate('hello'))


class TestPort(unittest.TestCase):
    def test_Port_ctor(self):
        p = port.Port(**_test_data())

        # Check that these fields exist by default.
        self.assertIsNone(p.vf)
        self.assertIsNone(p.plug)

    def test_Port_ctor_bad_args(self):
        def _bad_uuid(ans): ans['uuid'] = 'some_id'
        def _bad_instance_uuid(ans): ans['instance_uuid'] = 34567
        def _bad_vn_uuid(ans): ans['vn_uuid'] = 'some_vn_uuid'
        def _bad_vm_project_uuid(ans):
            ans['vm_project_uuid'] = 'the_project_id'
        def _bad_rx_vlan_id(ans): ans['rx_vlan_id'] = 'yup'
        def _bad_tx_vlan_id(ans): ans['tx_vlan_id'] = 'nope'
        def _bad_port_type(ans): ans['port_type'] = 'garbage'
        def _bad_vif_type(ans): ans['vif_type'] = 'junk'

        tweaks = [
            _bad_uuid,
            _bad_instance_uuid,
            _bad_vn_uuid,
            _bad_vm_project_uuid,
            _bad_rx_vlan_id,
            _bad_tx_vlan_id,
            _bad_port_type,
            _bad_vif_type,
        ]

        for t in tweaks:
            try:
                with self.assertRaises((ValueError, AttributeError)):
                    p = port.Port(**_test_data(t))

            except AssertionError as e:
                e.args += (t,)
                raise

    def test_Port_ctor_reject_unknown_args(self):
        # I actually had broken this at one point...
        def _tweak(ans):
            ans['hello'] = "it's me"

        with self.assertRaises(TypeError):
            p = port.Port(**_test_data(_tweak))

    def test_Port_ctor_vhostuser_socket(self):
        path = '/path/to/socket'

        # 1. should fail
        def _tweak(ans):
            ans['vhostuser_socket'] = path
        with self.assertRaises(ValueError):
            p = port.Port(**_test_data(_tweak))

        # 2. should fail
        def _tweak(ans):
            ans['vif_type'] = 'VhostUser'
        with self.assertRaises(ValueError):
            p = port.Port(**_test_data(_tweak))

        # 3. should work (and store the path in `p.vhostuser_socket`)
        def _tweak(ans):
            ans['vhostuser_socket'] = path
            ans['vif_type'] = 'VhostUser'

        p = port.Port(**_test_data(_tweak))
        self.assertEqual(p.vhostuser_socket, path)

    def test_Port_dump(self):
        p = port.Port(**_test_data())
        ans = p.dump()
        self.assertIsInstance(ans, dict)
        self.assertEqual(ans['system-name'], p.tap_name)

    def test_Port_dump_tap_name(self):
        p = port.Port(**_test_data())
        s = 'rubber duck'
        ans = p.dump(tap_name=s)
        self.assertIsInstance(ans, dict)
        self.assertNotEqual(ans['system-name'], p.tap_name)
        self.assertEqual(ans['system-name'], s)

        # test the arg checking
        with self.assertRaises(ValueError):
            p.dump(tap_name={})

    def test_Port_dump_None(self):
        # Contrail uses the literal string 'None' for "no IPv4/IPv6 address."
        p = port.Port(**_test_data())

        p.ip_address = None
        p.ipv6_address = "::2"
        ans = p.dump()
        self.assertIsInstance(ans, dict)
        self.assertIsInstance(ans['ip-address'], basestring)
        self.assertEqual(ans['ip-address'], 'None')
        self.assertIsInstance(ans['ip6-address'], basestring)
        self.assertNotEqual(ans['ip6-address'], 'None')

        p.ip_address = '127.0.0.2'
        p.ipv6_address = None
        ans = p.dump()
        self.assertIsInstance(ans, dict)
        self.assertIsInstance(ans['ip-address'], basestring)
        self.assertNotEqual(ans['ip-address'], 'None')
        self.assertIsInstance(ans['ip6-address'], basestring)
        self.assertEqual(ans['ip6-address'], 'None')


def _create_Port_with_VF(test, session, plug_mode):
    addr_in = _random_pci_address()
    pool = vf.Pool(fake_FallbackMap([addr_in]))

    u = uuid.uuid1()

    def _tweak(ans):
        ans['uuid'] = u
        ans['tap_name'] = 'nfp{}'.format(random.randint(100, 199))

    p = port.Port(**_test_data(_tweak))
    addr_out = pool.allocate_vf(
        session=session, neutron_port=u, plug_mode=plug_mode, expires=None
    )
    test.assertIsNotNone(addr_out)
    test.assertEqual(addr_in, addr_out)
    p.vf = session.query(vf.VF).get(addr_out)

    session.add(p)
    session.commit()

    return u, addr_in


class TestPortDB(unittest.TestCase):
    def test_create_metadata(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)

    def test_Port_db(self):
        """Basic test that Port objects can be stored in databases."""
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        p = port.Port(**_test_data())
        s.add(p)
        s.commit()

    def _create_Port_without_VF(self, session):
        u = uuid.uuid1()

        def _tweak(ans):
            ans['uuid'] = u
            ans['tap_name'] = 'xnfpx{}'.format(random.randint(0, 99999))

        p = port.Port(**_test_data(_tweak))
        session.add(p)
        session.commit()
        return u

    def test_Port_VF_interaction(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        # 1. adding a port without a VF is possible (for unaccelerated mode)
        u1 = self._create_Port_without_VF(Session())

        s = Session()
        p1 = s.query(port.Port).get(u1)
        self.assertIsNone(p1.vf)

        # 2. adding a port with a VF is possible, and links the VF to the port
        u2, addr2 = _create_Port_with_VF(self, Session(), plug_mode=PM.SRIOV)

        p2 = s.query(port.Port).get(u2)
        self.assertIsNotNone(p2.vf)
        self.assertEqual(p2.vf.addr, addr2)
        s.commit()

        # add another VF for use in the "delete port only deletes one VF" test
        # (note: this will cause the "port not in fallback map" warning)
        addr3 = pci.parse_pci_address('07de:05:02.2')
        u3 = uuid.uuid1()
        v3 = vf.VF(addr3, u3, expires=None)
        s.add(v3)
        s.commit()

        # 3. deleting a port with a VF associated deletes the VF
        v2 = one_or_none(s.query(vf.VF).filter(vf.VF.addr == addr2))
        self.assertIsNotNone(v2)
        self.assertEqual(v2.addr, addr2)  # sanity check
        self.assertEqual(v2.addr, p2.vf.addr)
        s.delete(p2)
        s.commit()
        v2 = one_or_none(s.query(vf.VF).filter(vf.VF.addr == addr2))
        self.assertIsNone(v2)

        # 4. deleting a port with a VF associated does not affect other VFs
        v3 = one_or_none(s.query(vf.VF).filter(vf.VF.addr == addr3))
        self.assertIsNotNone(v3)
        self.assertEqual(v3.neutron_port, u3)

        # 5. it is possible to delete a port that does not have an associated
        #    VF
        p1_alias = one_or_none(s.query(port.Port).filter(port.Port.uuid == u1))
        self.assertIsNotNone(p1_alias)
        self.assertIsNone(p1.vf)
        s.delete(p1_alias)
        s.commit()
        p1_alias = one_or_none(s.query(port.Port).filter(port.Port.uuid == u1))
        self.assertIsNone(p1_alias)

        # 6. deleting a port without a VF associated does not affect other VFs
        v3 = one_or_none(s.query(vf.VF).filter(vf.VF.addr == addr3))
        self.assertIsNotNone(v3)
        self.assertEqual(v3.neutron_port, u3)

        # 7. changing the expiration time on the VF associated with a port
        #    updates it in the VF object too (and in the database)
        with attachLogHandler(_VF_LOGGER()):
            u4, addr4 = _create_Port_with_VF(
                self, Session(), plug_mode=PM.SRIOV
            )
        p4 = s.query(port.Port).get(u4)
        self.assertIsNotNone(p4)
        self.assertIsNotNone(p4.vf)
        self.assertEqual(p4.vf.addr, addr4)
        self.assertIsNone(p4.vf.expires)
        v4 = s.query(vf.VF).get(addr4)
        self.assertEqual(v4.neutron_port, p4.uuid)
        self.assertIsNone(v4.expires)
        now = datetime.utcnow()
        expires1 = now + timedelta(hours=1)
        p4.vf.expires = expires1
        self.assertEqual(v4.expires, expires1)
        s.commit()

        s = Session()
        p4 = s.query(port.Port).get(u4)
        self.assertIsNotNone(p4)
        self.assertIsNotNone(p4.vf)
        self.assertEqual(p4.vf.addr, addr4)
        self.assertIsNotNone(p4.vf.expires)
        self.assertEqual(p4.vf.expires, expires1)

        # check the other direction too (updating VF object updates port)
        v4 = s.query(vf.VF).get(addr4)
        self.assertEqual(v4.neutron_port, p4.uuid)
        self.assertIsNotNone(v4.expires)
        self.assertEqual(v4.expires, expires1)
        expires2 = now + timedelta(days=1)
        v4.expires = expires2
        self.assertEqual(p4.vf.expires, expires2)
        s.commit()

    def test_create_standalone_PlugMode(self):
        # test that we can create a PlugMode for a port that doesn't exist yet.
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        u = uuid.uuid1()
        pm1 = port.PlugMode(u, PM.SRIOV)
        s.add(pm1)

        s.commit()

        s = Session()
        pm2 = s.query(port.PlugMode).get(u)
        self.assertIsNotNone(pm2)
        self.assertEqual(pm2.mode, PM.SRIOV)

        po = s.query(port.Port).get(u)
        self.assertIsNone(po)

        s.commit()

    def _create_Port_with_PlugMode(self, session, mode):
        p = port.Port(**_test_data())
        u = p.uuid
        p.plug = port.PlugMode(u, mode)

        session.add(p)
        session.commit()

        return u

    def test_Port_PlugMode_interaction(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        # 1. adding a port with a PlugMode is possible
        mu = {}
        for mode in PM.all_plug_modes:
            u = self._create_Port_with_PlugMode(Session(), mode)

            s = Session()
            p1 = s.query(port.Port).get(u)
            self.assertIsNotNone(p1.plug)
            self.assertEqual(p1.plug.mode, mode)
            s.commit()

            mu[mode] = u

        # 2. deleting a port deletes the associated PlugMode
        s = Session()
        po = s.query(port.Port).filter(port.Port.uuid == mu[PM.SRIOV]).one()
        s.delete(po)
        s.commit()
        pm = one_or_none(
            s.query(port.PlugMode).
            filter(port.PlugMode.neutron_port == mu[PM.SRIOV])
        )
        self.assertIsNone(pm)

        # 3. check that we didn't delete the other ones
        for mode in (PM.VirtIO, PM.unaccelerated):
            pm = s.query(port.PlugMode).filter(
                port.PlugMode.neutron_port == mu[mode]).one()

        s.commit()
        s = Session()

        # 4. changing the plug mode updates it on the port too (also via the
        #    database)
        po = s.query(port.Port).filter(port.Port.uuid == mu[PM.VirtIO]).one()
        pm = s.query(port.PlugMode).filter(
            port.PlugMode.neutron_port == mu[PM.VirtIO]
        ).one()
        self.assertEqual(po.plug.mode, PM.VirtIO)
        self.assertNotEqual(po.plug.mode, PM.SRIOV)
        pm.mode = PM.SRIOV
        self.assertEqual(po.plug.mode, PM.SRIOV)
        s.commit()
        po = s.query(port.Port).filter(port.Port.uuid == mu[PM.VirtIO]).one()
        self.assertEqual(po.plug.mode, PM.SRIOV)

        # check the other direction too (updating PlugMode object updates port)
        pm = s.query(port.PlugMode).filter(
            port.PlugMode.neutron_port == mu[PM.VirtIO]
        ).one()
        self.assertEqual(pm.mode, PM.SRIOV)
        pm.mode = PM.unaccelerated
        self.assertEqual(po.plug.mode, PM.unaccelerated)
        pm.mode = PM.VirtIO
        self.assertEqual(po.plug.mode, PM.VirtIO)

        s.commit()
        s = Session()

        modes = frozenset(m[0] for m in s.query(port.PlugMode.mode).all())
        self.assertEqual(modes, frozenset((PM.VirtIO, PM.unaccelerated)))

        u = uuid.uuid1()
        pm = port.PlugMode(neutron_port=u, mode=PM.SRIOV)
        s.add(pm)
        u = uuid.uuid1()
        pm = port.PlugMode(neutron_port=u, mode=PM.SRIOV)
        s.add(pm)

        modes = frozenset(m[0] for m in s.query(port.PlugMode.mode).all())
        self.assertEqual(
            modes, frozenset((PM.VirtIO, PM.unaccelerated, PM.SRIOV))
        )

        s.commit()
        s = Session()

        # 5. deleting a PlugMode DOES NOT delete the associated Port
        u = self._create_Port_with_PlugMode(s, PM.VirtIO)
        po = s.query(port.Port).filter(port.Port.uuid == u).one()
        pm = s.query(port.PlugMode).filter(port.PlugMode.neutron_port == u).\
            one()
        s.delete(pm)
        s.commit()
        s.refresh(po)
        self.assertIsNone(po.plug)


class Test_VRT_604_VF_gc(unittest.TestCase):
    def _test_gc_not_in_port_dir(self, k):
        N = 60
        vfset = set()
        while len(vfset) < 60:
            vfset.add(_random_pci_address())

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        now = datetime.utcnow()
        now_p1d = now + timedelta(days=1)
        now_m5m = now - timedelta(minutes=5)
        now_m9m = now - timedelta(minutes=9)

        # Make a bunch of non-expiring VFs (half expiring in the "far future,"
        # half never expiring) that are NOT in the "port dir."
        ce = (
            # Created, Expiring, Reclaim
            (now_m5m, now_p1d, False, 'expires, do not reclaim'),
            (now_m9m, now_p1d, False, 'expires, do not reclaim'),
            (now_m5m, None, False, 'too new, do not reclaim'),
            (now_m9m, None, True, 'old, reclaim'),
        )

        # Set up the port_dir.
        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'port_dir')
        os.makedirs(d)

        # Set up the database.
        should_reclaim = {}
        should_reclaim_n = {True: 0, False: 0}
        i = 0
        port_files_to_touch = random.sample(vfset, k)

        for addr in vfset:
            np = uuid.uuid1()
            vo = vf.VF(
                addr, neutron_port=np, created=ce[i][0], expires=ce[i][1]
            )
            s.add(vo)
            sr = ce[i][2]

            # Create files for selected ports.
            randmac = RandMac()
            if addr in port_files_to_touch:
                f = os.path.join(d, str(np))
                with open(f, 'w'):
                    pass

                # We also have to create port objects for these since otherwise
                # the garbage collector will figure out that they don't
                # correspond to anything else in the database, and delete them.
                #
                # TODO 2016-11-15: Cannot figure out why VFs are getting
                # reclaimed despite my doing this. Maybe the "get list of all
                # active ports" thing doesn't work correctly??
                po = port.Port(
                    uuid=np,
                    instance_uuid=None,
                    vn_uuid=None,
                    vm_project_uuid=None,
                    ip_address=None,
                    ipv6_address=None,
                    vm_name=None,
                    mac=randmac.generate(),
                    tap_name=str(np),
                    port_type='NovaVMPort',
                    vif_type='Vrouter',
                    tx_vlan_id=-1,
                    rx_vlan_id=-1,
                    created=ce[i][0],
                )
                po.vf = vo
                s.add(po)

                sr = False

            should_reclaim[addr] = sr
            should_reclaim_n[sr] += 1
            i = (i + 1) % len(ce)

        s.commit()

        # Test that WITHOUT the GcNotInPortDir, none of them can be reclaimed.
        with _VF_LMC() as lmc:
            pool = vf.Pool(fake_FallbackMap(vfset))
            np = uuid.uuid1()
            with self.assertRaises(vf.AllocationError):
                pool.allocate_vf(
                    session=s, neutron_port=np, expires=None,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                    raise_on_failure=True, _now=now
                )
            self.assertEqual(lmc.count, {
                _VF_LOGGER.name: {'ERROR': 1}
            })
        del pool

        # Test that WITH it, all the non-expiring ones that were created more
        # than 5 minutes ago are reclaimed.
        with _VF_LMC() as lmc:
            pool = vf.Pool(
                fake_FallbackMap(vfset),
                gc=port.GcNotInPortDir(
                    port_dir=d, grace_period=timedelta(minutes=5)
                ),
            )
            np = uuid.uuid1()
            addr = pool.allocate_vf(
                session=s, neutron_port=np, expires=None,
                plug_mode=random.choice(PM.accelerated_plug_modes),
                raise_on_failure=True, _now=now
            )
            self.assertIsNotNone(addr)
            self.assertEqual(lmc.count, {
                _VF_LOGGER.name: {'DEBUG': should_reclaim_n[True], 'INFO': 1}
            })
        del pool

        # Make sure that the correct VFs expired. The VF that was just
        # allocated is allowed to be in the database.
        expected_not_reclaimed = set([addr])
        for addr, sr in should_reclaim.iteritems():
            if not sr:
                expected_not_reclaimed.add(addr)

        for v in s.query(vf.VF).all():
            # KeyError from the following line means that a VF was found in the
            # database when GC should have evicted it.
            expected_not_reclaimed.remove(v.addr)
        self.assertEqual(expected_not_reclaimed, set())

        s.commit()

    def test_gc_not_in_port_dir(self):
        # Make a bunch of non-expiring VFs that are NOT in the "port dir."
        # Test that WITHOUT the GcNotInPortDir, none of them can be reclaimed,
        # and that WITH it, all of them can be reclaimed.
        self._test_gc_not_in_port_dir(0)

        # Same thing except put some of them in the "port dir."
        self._test_gc_not_in_port_dir(20)

    def test_gc_find_expired_type(self):
        p = vf.Pool(fake_FallbackMap(VFSET1))

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        now = datetime.utcnow()
        now_m1w = now - timedelta(days=7)

        v = vf.VF(pci.parse_pci_address('0001:06:08.2'), uuid.uuid1(), now_m1w)
        s.add(v)
        expired = vf._gc_find_expired(s, _now=now)
        for e in expired:
            self.assertIsInstance(e, vf.VF)
            self.assertIsInstance(e.addr, pci.PciAddress)

        s.commit()

    def test_gc_find_expired(self):
        p = vf.Pool(fake_FallbackMap(VFSET1))

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        now = datetime.utcnow()
        now_m1s = now - timedelta(seconds=1)
        now_m3m = now - timedelta(minutes=3)
        now_m1w = now - timedelta(days=7)
        before_now = (now_m1s, now_m3m, now_m1w)
        now_p1s = now + timedelta(seconds=1)
        now_p3m = now + timedelta(seconds=3)
        now_p1w = now + timedelta(days=7)
        after_now = (now_p1s, now_p3m, now_p1w)

        # 1. nothing expired on empty table
        expired = vf._gc_find_expired(s, _now=now)
        self.assertTrue(len(expired) == 0)

        # 2. nothing expired on table with non-expiring rows
        v1 = vf.VF(pci.parse_pci_address('0001:06:08.1'), uuid.uuid1())
        s.add(v1)

        expired = vf._gc_find_expired(s, _now=now)
        self.assertTrue(len(expired) == 0)

        # add a VF that expires now and then pretend that we are in the
        # past/future
        v2 = vf.VF(pci.parse_pci_address('0001:06:08.2'), uuid.uuid1(), now)
        s.add(v2)

        def assert_expired(times, expected):
            for t in times:
                expired = vf._gc_find_expired(s, _now=t)
                self.assertEqual(
                    frozenset(expired), expected,
                    't={}, now={}, expired={}, expected={}'.format(
                        t, now, expired, expected
                    )
                )

        # 3. nothing expired if it's before the expiration time;
        #    everything expired if it's on or after the expiration time
        assert_expired(before_now, frozenset())
        assert_expired((now,), frozenset([v2]))
        assert_expired(after_now, frozenset([v2]))

        # let's add some VFs that expire in the future and try again
        v3 = vf.VF(
            pci.parse_pci_address('0001:06:08.3'), uuid.uuid1(), now_p1s
        )
        s.add(v3)

        assert_expired(before_now, frozenset())
        assert_expired((now,), frozenset([v2]))
        assert_expired(after_now, frozenset([v2, v3]))

        v4 = vf.VF(
            pci.parse_pci_address('0001:06:08.4'), uuid.uuid1(), now_p1s
        )
        s.add(v4)

        assert_expired(before_now, frozenset())
        assert_expired((now,), frozenset([v2]))
        assert_expired(after_now, frozenset([v2, v3, v4]))

        v5 = vf.VF(pci.parse_pci_address('0001:06:08.5'), uuid.uuid1(), now)
        s.add(v5)

        assert_expired(before_now, frozenset())
        assert_expired((now,), frozenset([v2, v5]))
        assert_expired(after_now, frozenset([v2, v3, v4, v5]))

        v6 = vf.VF(
            pci.parse_pci_address('0001:06:08.6'), uuid.uuid1(), now_m1w
        )
        s.add(v6)
        assert_expired(before_now, frozenset([v6]))
        assert_expired((now,), frozenset([v2, v5, v6]))
        assert_expired(after_now, frozenset([v2, v3, v4, v5, v6]))

        s.commit()

    def test_gc(self):
        p = vf.Pool(fake_FallbackMap(VFSET1))

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        now = datetime.utcnow()
        now_m1s = now - timedelta(seconds=1)
        now_p1s = now + timedelta(seconds=1)

        # 1. nothing expired on empty table
        expired = p.gc(s, _now=now)
        self.assertEqual(expired, set())

        # 2. nothing expired on table with non-expiring rows
        ppa = pci.parse_pci_address
        s.add(vf.VF(ppa('0001:06:08.1'), uuid.uuid1()))

        expired = p.gc(s, _now=now)
        self.assertEqual(expired, set())

        # add some VFs that expires at various times, expire them, and see what
        # happens
        def assert_reclaimed(t, expected):
            reclaimed = p.gc(s, _now=t)
            self.assertEqual(
                reclaimed, expected,
                't={}, now={}, reclaimed={}, expected={}'.format(
                    t, now, reclaimed, expected
                )
            )

        # expires now and checking a second ago
        s.add(vf.VF(ppa('0001:06:08.2'), uuid.uuid1(), now))
        assert_reclaimed(now_m1s, frozenset())
        assert_reclaimed(now_m1s, frozenset())

        # expires now and it's now
        assert_reclaimed(now, frozenset([(vf.VF, ppa('0001:06:08.2'))]))
        assert_reclaimed(now, frozenset())
        assert_reclaimed(now, frozenset())

        # expires now and checking a second from now
        s.add(vf.VF(ppa('0001:06:08.2'), uuid.uuid1(), now))
        assert_reclaimed(now_p1s, frozenset([(vf.VF, ppa('0001:06:08.2'))]))
        assert_reclaimed(now_p1s, frozenset())
        assert_reclaimed(now_p1s, frozenset())

        # add another one that expires now (on the same VF) and expire it
        s.add(vf.VF(ppa('0001:06:08.2'), uuid.uuid1(), now))
        assert_reclaimed(now, frozenset([(vf.VF, ppa('0001:06:08.2'))]))
        assert_reclaimed(now, frozenset())
        assert_reclaimed(now, frozenset())

        # let's add 2 that expire at slightly different times
        s.add(vf.VF(ppa('0001:06:08.3'), uuid.uuid1(), now))
        s.add(vf.VF(ppa('0001:06:08.4'), uuid.uuid1(), now_p1s))
        assert_reclaimed(now, frozenset([(vf.VF, ppa('0001:06:08.3'))]))
        assert_reclaimed(now, frozenset())
        assert_reclaimed(now_m1s, frozenset())
        assert_reclaimed(now_p1s, frozenset([(vf.VF, ppa('0001:06:08.4'))]))
        assert_reclaimed(now_p1s, frozenset())

        # let's add one that doesn't expire
        s.add(vf.VF(ppa('0001:06:08.5'), uuid.uuid1()))
        assert_reclaimed(now, frozenset())

        # let's add three, one with a bogus (not in fallback map) address
        s.add(vf.VF(ppa('0001:06:08.3'), uuid.uuid1(), now))
        s.add(vf.VF(ppa('0001:06:08.4'), uuid.uuid1(), now))
        s.add(vf.VF(ppa('eeee:06:08.4'), uuid.uuid1(), now))
        assert_reclaimed(
            now_p1s,
            frozenset([
                (vf.VF, ppa('0001:06:08.4')),
                (vf.VF, ppa('eeee:06:08.4')),
                (vf.VF, ppa('0001:06:08.3')),
            ])
        )
        assert_reclaimed(now_p1s, frozenset())

        s.commit()

    def test_list_vfs(self):
        with _VF_LMC() as lmc:
            p = vf.Pool(fake_FallbackMap(VFSET_REAL))
            self.assertEqual(lmc.count, {})

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 0)

        # expires: none
        v = vf.VF(pci.parse_pci_address('0000:04:0a.0'), uuid.uuid1(), None)
        s.add(v)
        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 1)

        v = vf.VF(pci.parse_pci_address('0000:04:0b.0'), uuid.uuid1(), None)
        s.add(v)
        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 2)

        now = datetime.utcnow()
        now_p1s = now + timedelta(seconds=1)
        now_p2s = now + timedelta(seconds=2)

        # add one that expires
        v = vf.VF(pci.parse_pci_address('0000:04:0c.0'), uuid.uuid1(), now_p1s)
        s.add(v)
        vfs = p.list_vfs(s, _now=now)
        self.assertEqual(len(vfs), 3)
        vfs = p.list_vfs(s, _now=now_p2s)
        self.assertEqual(len(vfs), 2)
        vfs = p.list_vfs(s, _now=now)
        self.assertEqual(len(vfs), 3)
        vfs = p.list_vfs(s, include_expired=True, _now=now_p2s)
        self.assertEqual(len(vfs), 3)
        self.assertEqual(
            frozenset([v.addr for v in vfs]),
            make_vfset('0000:04:0a.0 0000:04:0b.0 0000:04:0c.0')
        )
        vfs = p.list_vfs(s, include_expired=False, _now=now_p2s)
        self.assertEqual(len(vfs), 2)

        self.assertEqual(
            frozenset([v.addr for v in vfs]),
            make_vfset('0000:04:0a.0 0000:04:0b.0')
        )

        s.commit()

    def test_deallocate_vf(self):
        with _VF_LMC() as lmc:
            p = vf.Pool(fake_FallbackMap(VFSET_REAL))
            self.assertEqual(lmc.count, {})

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 0)

        v = vf.VF(pci.parse_pci_address('0000:04:0a.0'), uuid.uuid1(), None)
        s.add(v)
        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 1)

        now = datetime.utcnow()
        now_p1w = now + timedelta(weeks=1)

        # Let's also try with a temporary (expiring) VF allocation.
        #
        # NOTE: deallocate_vf() is allowed to perform garbage collection.
        # Therefore this test would be invalid if it used a VF that expired
        # in the past.
        v = vf.VF(pci.parse_pci_address('0000:04:0b.0'), uuid.uuid1(), now_p1w)
        s.add(v)
        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 2)

        with _VF_LMC() as lmc:
            p.deallocate_vf(s, pci.parse_pci_address('0000:04:0a.0'))
            self.assertEqual(lmc.count, {_VF_LOGGER.name: {'INFO': 1}})

        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 1)
        self.assertEqual(vfs[0].addr, pci.parse_pci_address('0000:04:0b.0'))

        with _VF_LMC() as lmc:
            # deallocate one that's not there
            p.deallocate_vf(s, pci.parse_pci_address('4310:06:0c.0'))
            self.assertEqual(lmc.count, {})

        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 1)
        self.assertEqual(vfs[0].addr, pci.parse_pci_address('0000:04:0b.0'))

        # deallocate the last one
        with _VF_LMC() as lmc:
            p.deallocate_vf(s, pci.parse_pci_address('0000:04:0B.0'))
            self.assertEqual(lmc.count, {_VF_LOGGER.name: {'INFO': 1}})

        vfs = p.list_vfs(s)
        self.assertEqual(len(vfs), 0)

        s.commit()

    def test_vf_identity(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        with _VF_LMC() as lmc:
            p = vf.Pool(fake_FallbackMap(VFSET_REAL))
            self.assertEqual(lmc.count, {})

            d = {}
            for i in xrange(0, 62):
                u = uuid.uuid1()
                addr = p.allocate_vf(
                    session=s, neutron_port=u, expires=None,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                )
                self.assertIsNotNone(addr)
                d[u] = addr

            for u, v in d.iteritems():
                addr = p.allocate_vf(
                    session=s, neutron_port=u, expires=None,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                )
                self.assertIsNotNone(addr)
                self.assertEqual(addr, v)

            self.assertEqual(lmc.count, {_VF_LOGGER.name: {'INFO': 62}})

        s.commit()

    def test_vf_log_fuzzer(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        now = datetime.utcnow()
        expires = (
            now,
            now + timedelta(minutes=1),
            now - timedelta(minutes=1),
            None
        )
        uuids = set()

        # create an entry with a bogus PCIe address to trigger the "NFP moved"
        # warning
        u = uuid.uuid1()
        uuids.add(u)
        s.add(vf.VF(pci.parse_pci_address('3676:05:36.0'), u))

        with _VF_LMC():
            p = vf.Pool(fake_FallbackMap(VFSET_REAL))

            while len(uuids) < p.fallback_map_len():
                e = random.choice(expires)

                if random.random() < 0.5 or not uuids:
                    # new VF
                    u = uuid.uuid1()
                    uuids.add(u)
                else:
                    # reuse existing VF
                    u = random.choice(list(uuids))

                addr = p.allocate_vf(
                    session=s, neutron_port=u, expires=e, _now=now,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                )
                self.assertIsNotNone(addr)


class Test_VRT_604_gc(unittest.TestCase):
    def _make_gc_pool(self, addr):
        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'port_dir')

        return vf.Pool(
            fake_FallbackMap([] if addr is None else [addr]),
            gc=port.GcNotInPortDir(port_dir=d, grace_period=None),
        )

    def test_Port_PlugMode_gc(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()
        u, addr = _create_Port_with_VF(self, s, plug_mode=PM.VirtIO)

        po = s.query(port.Port).filter(port.Port.uuid == u).one()
        self.assertIsNone(po.plug)
        pm = port.PlugMode(
            mode=PM.VirtIO, created=po.created, neutron_port=po.uuid
        )
        s.add(pm)
        po.mode = pm

        s.commit()

        po = s.query(port.Port).filter(port.Port.uuid == u).one()
        self.assertIsNotNone(po.mode)
        self.assertIsNotNone(po.vf.addr)
        self.assertEqual(po.vf.addr, addr)

        # Create a VF pool that thinks that it should garbage collect this
        # port.
        pool = self._make_gc_pool(addr)

        # Perform the garbage collection and check the results.
        reclaimed = pool.gc(s, _now=None, logger=vf.logger)
        self.assertEqual(reclaimed, frozenset([
            (vf.VF, addr),
            (port.Port, u),
            (port.PlugMode, u),
        ]))

        s.commit()

        # Check that GC cleaned up the abandoned port and plug mode.
        pm = one_or_none(
            s.query(port.PlugMode).filter(port.PlugMode.neutron_port == u)
        )
        self.assertIsNone(pm)
        po = one_or_none(s.query(port.Port).filter(port.Port.uuid == u))
        self.assertIsNone(po)

    def test_Port_gc(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()
        u, addr = _create_Port_with_VF(self, s, plug_mode=PM.VirtIO)

        po = s.query(port.Port).filter(port.Port.uuid == u).one()
        self.assertIsNone(po.plug)

        s.commit()

        po = s.query(port.Port).filter(port.Port.uuid == u).one()
        self.assertIsNone(po.plug)
        self.assertIsNotNone(po.vf.addr)
        self.assertEqual(po.vf.addr, addr)

        # Create a VF pool that thinks that it should garbage collect this
        # port.
        pool = self._make_gc_pool(addr)

        # Perform the garbage collection and check the results.
        reclaimed = pool.gc(s, _now=None, logger=vf.logger)
        self.assertEqual(
            reclaimed, frozenset([
                (vf.VF, addr),
                (port.Port, u),
            ])
        )

        s.commit()

        # Check that GC cleaned up the abandoned port.
        pm = one_or_none(
            s.query(port.PlugMode).filter(port.PlugMode.neutron_port == u)
        )
        self.assertIsNone(pm)
        po = one_or_none(s.query(port.Port).filter(port.Port.uuid == u))
        self.assertIsNone(po)

    def test_VF_PlugMode_gc(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()
        u = uuid.uuid1()
        pm = port.PlugMode(u, PM.SRIOV)
        s.add(pm)

        addr = _random_pci_address()
        v = vf.VF(
            addr=addr, neutron_port=u, expires=None, created=datetime.utcnow()
        )
        s.add(v)

        s.commit()

        pm = (
            s.query(port.PlugMode).
            filter(port.PlugMode.neutron_port == u).one()
        )
        self.assertIsNotNone(pm)  # redundant with one()
        v = s.query(vf.VF).filter(vf.VF.neutron_port == u).one()
        self.assertIsNotNone(v)  # redundant with one()

        # Create a VF pool that thinks that it should garbage collect this
        # port.
        pool = self._make_gc_pool(addr)

        # Perform the garbage collection and check the results.
        reclaimed = pool.gc(s, _now=None, logger=vf.logger)
        self.assertEqual(reclaimed, frozenset([
            (vf.VF, addr),
            (port.PlugMode, u),
        ]))

        s.commit()

        # Check that GC cleaned up the abandoned plug mode.
        pm = one_or_none(
            s.query(port.PlugMode).filter(port.PlugMode.neutron_port == u)
        )
        self.assertIsNone(pm)

    def test_PlugMode_gc(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()
        u = uuid.uuid1()
        pm = port.PlugMode(u, PM.SRIOV)
        s.add(pm)

        s.commit()

        pm = (
            s.query(port.PlugMode).
            filter(port.PlugMode.neutron_port == u).one()
        )
        self.assertIsNotNone(pm)  # redundant with one()

        # Create a VF pool that thinks that it should garbage collect this
        # port.
        with _VF_LMC() as lmc:  # suppress empty pool warning
            pool = self._make_gc_pool(addr=None)
            self.assertEqual(lmc.count, {_VF_LOGGER.name: {'WARNING': 1}})

        # Perform the garbage collection and check the results.
        reclaimed = pool.gc(s, _now=None, logger=vf.logger)
        self.assertEqual(reclaimed, frozenset([
            (port.PlugMode, u),
        ]))

        s.commit()

        # Check that GC cleaned up the abandoned plug mode.
        pm = one_or_none(
            s.query(port.PlugMode).filter(port.PlugMode.neutron_port == u)
        )
        self.assertIsNone(pm)

if __name__ == '__main__':
    unittest.main()
