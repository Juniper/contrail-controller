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

import copy
import logging
import os
import random
import tempfile
import uuid

from datetime import datetime, timedelta

from netronome.vrouter import (database, fallback, pci, plug_modes as PM, vf)
from netronome.vrouter.sa.sqlite import set_sqlite_synchronous_off
from netronome.vrouter.tests.helpers.config import _random_pci_address
from netronome.vrouter.tests.helpers.vf import *
from netronome.vrouter.tests.unit import *

from sqlalchemy.orm.session import sessionmaker

_VF_LOGGER = make_getLogger('netronome.vrouter.vf')


def _VF_LMC(cls=LogMessageCounter):
    return attachLogHandler(_VF_LOGGER(), cls())


if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'vf.'
else:
    _TMP_PREFIX = 'netronome.vrouter.tests.unit.test_vf.'


class TestVF(unittest.TestCase):
    def test_VF_ctor(self):
        addr = pci.parse_pci_address('0000:04:08.0')
        v = vf.VF(addr)
        self.assertEqual(v.addr, pci.parse_pci_address('0000:04:08.0'))

    def test_create_metadata(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)

    def test_calculate_expiration_datetime(self):
        # basically just check the function signature and return value type

        a = vf.calculate_expiration_datetime(
            reservation_timeout=timedelta(minutes=10)
        )
        self.assertIsInstance(a, datetime)

        a = vf.calculate_expiration_datetime(
            reservation_timeout=timedelta(minutes=10), _now=datetime.utcnow()
        )
        self.assertIsInstance(a, datetime)


class TestPool(unittest.TestCase):
    def test_Pool_ctor(self):
        # attach null handler in case we are running on a system without
        # nfp_vrouter.ko loaded
        with _VF_LMC():
            p = vf.Pool(vf.default_fallback_map())

    def test_Pool_ctor_empty_vfset(self):
        with _VF_LMC() as lmc:
            p = vf.Pool(fallback.FallbackMap())
            self.assertEqual(lmc.count, {_VF_LOGGER.name: {'WARNING': 1}})

    def test_allocate_vf_empty_pool(self):
        with _VF_LMC() as lmc:
            p = vf.Pool(fallback.FallbackMap())
            self.assertEqual(lmc.count, {_VF_LOGGER.name: {'WARNING': 1}})

            engine = database.create_engine('tmp')[0]
            set_sqlite_synchronous_off(engine)
            vf.create_metadata(engine)
            Session = sessionmaker(bind=engine)

            s = Session()

            addr = p.allocate_vf(
                session=s, neutron_port=uuid.uuid1(), expires=None,
                plug_mode=PM.SRIOV,
            )
            self.assertIsNone(addr)
            self.assertEqual(lmc.count, {
                _VF_LOGGER.name: {'WARNING': 1, 'ERROR': 1}
            })

            s.commit()

    def test_allocate_vf(self):
        with _VF_LMC() as lmc:
            p = vf.Pool(fake_FallbackMap(TWO_VFS))
            self.assertEqual(lmc.count, {})

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        # let's add one by hand to aid in initial debugging
        s.add(vf.VF(pci.parse_pci_address('eeee:22:33.4'), uuid.uuid1()))

        with _VF_LMC() as lmc:
            addr = p.allocate_vf(
                session=s, neutron_port=uuid.uuid1(), expires=None,
                plug_mode=PM.SRIOV,
            )
            self.assertIsInstance(addr, pci.PciAddress)
            self.assertEqual(
                lmc.count, {_VF_LOGGER.name: {'WARNING': 1, 'INFO': 1}}
            )

            # this one expires just to sanity check adding expiration dates to
            # the tables
            addr = p.allocate_vf(
                session=s, neutron_port=uuid.uuid1(),
                expires=datetime.utcnow() + timedelta(days=100),
                plug_mode=PM.SRIOV,
            )
            self.assertIsInstance(addr, pci.PciAddress)
            self.assertEqual(
                lmc.count, {_VF_LOGGER.name: {'WARNING': 2, 'INFO': 2}}
            )

            addr = p.allocate_vf(
                session=s, neutron_port=uuid.uuid1(), expires=None,
                plug_mode=PM.SRIOV,
            )
            self.assertIsNone(addr)
            self.assertEqual(lmc.count, {
                _VF_LOGGER.name: {'WARNING': 3, 'ERROR': 1, 'INFO': 2}
            })

        s.commit()

    def test_allocate_vf_expire_bogus(self):
        # Test logging of expiring a bogus VF.
        with _VF_LMC() as lmc:
            p = vf.Pool(fake_FallbackMap(make_vfset('1111:22:33.4')))
            self.assertEqual(lmc.count, {})

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        now = datetime.utcnow()
        now_m1h   = now - timedelta(hours=1)

        s.add(vf.VF(
            pci.parse_pci_address('eeee:22:33.8'), uuid.uuid1(),
            expires=now_m1h
        ))  # bogus
        s.add(vf.VF(
            pci.parse_pci_address('1111:22:33.4'), uuid.uuid1(),
            expires=now_m1h
        ))

        with _VF_LMC() as lmc:
            addr = p.allocate_vf(
                session=s, neutron_port=uuid.uuid1(), expires=None,
                plug_mode=PM.SRIOV,
            )
            self.assertIsInstance(addr, pci.PciAddress)
            self.assertEqual(
                lmc.count, {
                    _VF_LOGGER.name: {'DEBUG': 2, 'INFO': 1}
                }
            )

    def test_allocate_vf_exhaustion(self):
        with _VF_LMC() as lmc:
            p = vf.Pool(fake_FallbackMap(VFSET_REAL))
            self.assertEqual(lmc.count, {})

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        with _VF_LMC() as lmc:
            # when do we run out?
            n = 0
            while True:
                addr = p.allocate_vf(
                    session=s, neutron_port=uuid.uuid1(), expires=None,
                    plug_mode=PM.SRIOV,
                )
                if addr is None:
                    break
                n += 1

        self.assertEqual(lmc.count, {
            _VF_LOGGER.name: {'ERROR': 1, 'INFO': 62}
        })
        self.assertEqual(n, 62)

        s.commit()

    def test_allocate_vf_exhaustion_with_gc(self):
        self.maxDiff = None

        with _VF_LMC() as lmc:
            p = vf.Pool(fake_FallbackMap(VFSET_REAL))
            self.assertEqual(lmc.count, {})

        N_POOL = p.fallback_map_len()

        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        now = datetime.utcnow()
        now_p1h   = now + timedelta(hours=1)
        now_p1h1s = now + timedelta(hours=1, seconds=1)
        now_p2h   = now + timedelta(hours=2)
        now_p2h1s = now + timedelta(hours=2, seconds=1)

        with _VF_LMC() as lmc:
            # when do we run out?
            n = 0
            while True:
                addr = p.allocate_vf(
                    session=s, neutron_port=uuid.uuid1(), expires=now_p1h,
                    _now=now,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                )
                if addr is None:
                    break
                n += 1
                self.assertLess(n, 1000)  # sanity check

        self.assertEqual(lmc.count, {
            _VF_LOGGER.name: {'ERROR': 1, 'INFO': N_POOL}
        })
        self.assertEqual(n, N_POOL)

        # make sure that allocation failure raises an exception if we ask it to
        with attachLogHandler(_VF_LOGGER()):
            with self.assertRaises(vf.AllocationError) as cm:
                u = uuid.uuid1()
                addr = p.allocate_vf(
                    session=s, neutron_port=u, expires=now_p1h,
                    raise_on_failure=True, _now=now,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                )
            self.assertEqual(cm.exception.neutron_port, u)

        # once they expire, we should get all 62 back
        with _VF_LMC() as lmc:
            # when do we run out?
            n = 0
            while True:
                addr = p.allocate_vf(
                    session=s, neutron_port=uuid.uuid1(), expires=now_p2h,
                    _now=now_p1h1s,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                )
                if addr is None:
                    break
                n += 1
                self.assertLess(n, 1000)  # sanity check

        self.assertEqual(lmc.count, {
            _VF_LOGGER.name: {'DEBUG': N_POOL, 'ERROR': 1, 'INFO': N_POOL}
        })
        self.assertEqual(n, N_POOL)

        # if we are adding VFs that expire in the past, we should never run out
        N = 400
        with _VF_LMC() as lmc:
            # when do we run out?
            for n in xrange(1, N + 1):  # "never"
                addr = p.allocate_vf(
                    session=s, neutron_port=uuid.uuid1(), expires=now_p2h,
                    _now=now_p2h1s,
                    plug_mode=random.choice(PM.accelerated_plug_modes),
                )
                self.assertIsNotNone(addr)

        self.assertEqual(lmc.count, {
            _VF_LOGGER.name: {'WARNING': N, 'DEBUG': N + N_POOL - 1}
        })
        self.assertEqual(n, N)

        s.commit()

if __name__ == '__main__':
    unittest.main()
