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

from netronome.vrouter import (plug, plug_modes as PM, port, vf)

try:
    import zmq
    import netronome.virtiorelayd.virtiorelayd_pb2 as relay
    from netronome.virtiorelayd import vf_allocator

except ImportError:
    relay = vf_allocator = None


def _format_ConfigResponse(response):
    if response.status == response.OK:
        return 'OK'

    if response.status == response.EBADR:
        msg = 'bad request'
    else:
        msg = 'unknown error code'

    try:
        status_str = response.Status.Name(response.status)
    except ValueError:
        status_str = str(response.status)  # e.g., '123'

    if msg is None:
        return 'ERROR: {}'.format(status_str)
    else:
        return 'ERROR: {} ({})'.format(msg, status_str)


class LoadBalancingVFChooser(object):
    def __init__(self, zmq_config_ep, rcvtimeo_ms, **kwds):
        super(LoadBalancingVFChooser, self).__init__(**kwds)

        req = relay.ConfigRequest()
        response = relay.ConfigResponse()

        plug._zmq_transaction(
            ep=zmq_config_ep, req=req, req_to_str=None, response=response,
            response_to_str=_format_ConfigResponse, rcvtimeo_ms=rcvtimeo_ms,
            logger=vf.logger,
        )
        self.relay_cpu_map = {
            entry.relay_number: (entry.vf2virtio_cpu, entry.virtio2vf_cpu)
            for entry in response.relay_cpu_map
        }

    def __call__(self, avail, session, fallback_map):
        # Look in the database to get the list of VFs currently allocated to
        # virtiorelayd.
        q = (
            session.query(vf.VF)
            .join(
                port.PlugMode, vf.VF.neutron_port == port.PlugMode.neutron_port
            )
            .filter(port.PlugMode.mode == PM.VirtIO)
        )
        active_virtio_vfs = set(map(lambda v: v.addr, q.all()))

        # Get the map of VFs → relays. This will need to be changed for
        # multi-card support.
        #
        # TAGS: VIO-22
        vf_relay_map = {
            o.vf_addr: o.vf_number for o in fallback_map.vfmap.itervalues()
        }

        # Prioritize VFs by CPU usage.
        prio = vf_allocator.prioritize_vfs(
            active_virtio_vfs=active_virtio_vfs,
            inactive_vfs=avail,
            vf_relay_map=vf_relay_map,
            relay_cpu_map=self.relay_cpu_map,
        )

        # Warn if we were forced to select a VF without knowing its CPU usage.
        addr, weight = prio[0]
        if weight is None:
            vf.logger.warning(
                'unable to estimate CPU usage for VF %s; network performance '
                'may be suboptimal', addr
            )
        else:
            vf.logger.debug(
                'allocated VF %s with dimensionless CPU usage %s', addr, weight
            )

        return addr


class SequentialVFChooser(object):
    def __call__(self, avail, session, fallback_map):
        return sorted(list(avail))[-1]


def vf_chooser(zmq_config_ep, rcvtimeo_ms):
    def chooser(m):
        if m == PM.VirtIO and vf_allocator is not None:
            return LoadBalancingVFChooser(
                zmq_config_ep=zmq_config_ep, rcvtimeo_ms=rcvtimeo_ms
            )
        else:
            # Even in SR-IOV mode, we want sequential allocation, not random.
            # See VRT-802.
            return SequentialVFChooser()

    return chooser
