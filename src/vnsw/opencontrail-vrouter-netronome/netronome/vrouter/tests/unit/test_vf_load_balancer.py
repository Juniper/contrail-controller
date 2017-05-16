# vim: set expandtab shiftwidth=4 fileencoding=UTF-8:

# Copyright 2017 Netronome Systems, Inc.
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

import contextlib
import logging
import os
import tempfile
import uuid

from netronome.vrouter import (
    database, fallback, plug, plug_modes as PM, port, vf, vf_load_balancer
)
from netronome.vrouter.pci import parse_pci_address as pci_address
from netronome.vrouter.sa.sqlite import set_sqlite_synchronous_off
from netronome.vrouter.tests.helpers.plug import FakeVirtIOConfigServer

from sqlalchemy.orm.session import sessionmaker

try:
    import netronome.virtiorelayd.virtiorelayd_pb2 as relay
except ImportError:
    relay = None

if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'vf_lb.'
else:
    _TMP_PREFIX = 'netronome.vrouter.tests.unit.test_vf_load_balancer.'

FALLBACK_MAP_DATA = r'''
0000:04:00.0 0000:04:08.0 nfp_v0.0 0
0000:04:00.0 0000:04:08.1 nfp_v0.1 1
0000:04:00.0 0000:04:08.2 nfp_v0.2 2
0000:04:00.0 0000:04:08.3 nfp_v0.3 3
0000:04:00.0 0000:04:08.4 nfp_v0.4 4
0000:04:00.0 0000:04:08.5 nfp_v0.5 5
0000:04:00.0 0000:04:08.6 nfp_v0.6 6
0000:04:00.0 0000:04:08.7 nfp_v0.7 7
0000:04:00.0 0000:04:08.8 nfp_v0.8 8
0000:04:00.0 0000:04:08.9 nfp_v0.9 9
'''.translate(None, '$').lstrip()


@unittest.skipIf(
    vf_load_balancer.relay is None, 'virtiorelay interface not installed'
)
class TestVFLoadBalancer(unittest.TestCase):
    def test_format_ConfigResponse(self):
        resp = vf_load_balancer.relay.ConfigResponse()

        s = vf_load_balancer._format_ConfigResponse(resp)
        self.assertEqual(s, 'OK')

        resp.status = resp.EBADR
        s = vf_load_balancer._format_ConfigResponse(resp)
        self.assertRegexpMatches(s, r'bad request')
        self.assertRegexpMatches(s, r'EBADR')

        resp.status = 12345
        s = vf_load_balancer._format_ConfigResponse(resp)
        self.assertRegexpMatches(s, r'unknown')
        self.assertRegexpMatches(s, r'12345')

    @contextlib.contextmanager
    def _rig(self, cls, **kwds):
        d_root = tempfile.mkdtemp(prefix=_TMP_PREFIX)
        d = os.path.join(d_root, 'TestVFLoadBalancer/virtiorelayd')
        plug.makedirs(d)
        zmq_config_ep = 'ipc://' + os.path.join(d, 'config')

        kwds.setdefault('server_zmq_config_ep', zmq_config_ep)
        server, thr = cls.boot(assertTrue=self.assertTrue, **kwds)

        yield (kwds['server_zmq_config_ep'], server)

        server.stop()
        JOIN_TIMEOUT_SEC = 9
        thr.join(JOIN_TIMEOUT_SEC)
        self.assertFalse(
            thr.isAlive(),
            'server did not shut down within {}s'.format(JOIN_TIMEOUT_SEC)
        )

    def test(self):
        engine = database.create_engine('tmp')[0]
        set_sqlite_synchronous_off(engine)
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        # Let's put some VFs in use.
        np = uuid.uuid1()
        s.add(vf.VF(addr=pci_address('0000:04:08.1'), neutron_port=np))
        s.add(port.PlugMode(neutron_port=np, mode=PM.VirtIO))

        np = uuid.uuid1()
        s.add(vf.VF(addr=pci_address('0000:04:08.2'), neutron_port=np))
        s.add(port.PlugMode(neutron_port=np, mode=PM.VirtIO))

        np = uuid.uuid1()
        s.add(vf.VF(addr=pci_address('0000:04:08.3'), neutron_port=np))
        s.add(port.PlugMode(neutron_port=np, mode=PM.SRIOV))

        np = uuid.uuid1()
        s.add(vf.VF(addr=pci_address('0000:04:08.4'), neutron_port=np))
        s.add(port.PlugMode(neutron_port=np, mode=PM.SRIOV))

        np = uuid.uuid1()
        s.add(vf.VF(addr=pci_address('0000:04:08.8'), neutron_port=np))
        s.add(port.PlugMode(neutron_port=np, mode=PM.VirtIO))

        # --

        np = uuid.uuid1()

        def h(request):
            # Fake relay_cpu_map.
            relay_cpu_map = {n: (n % 4, (n + 1) % 4) for n in xrange(60)}
            m = [
                relay.ConfigResponse.RelayCPU(
                    relay_number=k, vf2virtio_cpu=v[0], virtio2vf_cpu=v[1]
                ) for k, v in relay_cpu_map.iteritems()
            ]

            response = relay.ConfigResponse(relay_cpu_map=m)
            response.status = response.OK

            return response.SerializePartialToString()

        with self._rig(FakeVirtIOConfigServer, handle_request=h) as rig:
            config_ep, server = rig

            fallback_map = fallback.read_sysfs_fallback_map(
                _in=FALLBACK_MAP_DATA
            )
            p = vf.Pool(
                fallback_map,
                vf_chooser=vf_load_balancer.vf_chooser(
                    zmq_config_ep=config_ep, rcvtimeo_ms=5000,
                ),
            )
            addr = p.allocate_vf(
                neutron_port=np, plug_mode=PM.VirtIO, expires=None,
                raise_on_failure=True, session=s
            )

        self.assertEqual(addr, pci_address('0000:04:08.7'))

    # def test_not_VirtIO(self):

if __name__ == '__main__':
    unittest.main()
