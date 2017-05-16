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

import logging
import uuid

from netronome.vrouter import (
    config_opts, flavor, glance, pci, plug_modes as PM, vf
)
from netronome.vrouter.port import (PlugMode, Port)
from netronome.vrouter.sa.helpers import one_or_none
from netronome.vrouter.vf import VF

from nova.network import model as network_model
from nova.virt.libvirt import designer

logger = logging.getLogger(__name__)


class AccelerationModeConflict(Exception):
    """
    Raised when all hardware acceleration modes are marked as impermissible.
    """

    def __init__(self, msg, neutron_port, old_modes, allowed_modes):
        Exception.__init__(self, msg)
        self.neutron_port = neutron_port
        self.old_modes = old_modes
        self.allowed_modes = allowed_modes


def _can_accelerate_vif_type(vif_type):
    # vif_type will be one of: "vhostuser" or "vrouter" (raw Nova), or
    # "VhostUser" or "Vrouter" (Contrail Python call to vrouter-port-control).
    # In any case, only the vRouter vif types can be accelerated.
    return vif_type.lower() == 'vrouter'


def _can_accelerate_virt_type(virt_type):
    # virt_type can be "lxc" or one of numerous other hypervisors (raw Nova),
    # or "NovaVMPort" or "NameSpacePort" (Contrail Python call to
    # vrouter-port-control). In any case, for now let's pretend that only "lxc"
    # and "NameSpacePort" virt types cannot be accelerated.
    return virt_type.lower() not in ('lxc', 'namespaceport')


def _filter_modes(
    neutron_port, old_modes, allowed_modes, allowed_modes_description,
    filter_failure_description, old_modes_description=None
):
    if old_modes_description is None:
        old_modes_description = 'modes available before filtering'

    if allowed_modes is None:
        # No restriction.
        return old_modes

    new_modes = tuple(m for m in old_modes if m in allowed_modes)
    if new_modes:
        # Some modes remained after filtering.
        return new_modes

    msg = (
        'unable to select a hardware acceleration mode for port {}: {}'
        .format(neutron_port, filter_failure_description)
    )

    logger.error('%s', msg)
    logger.info('%s: %r', old_modes_description, old_modes)
    logger.info(
        'modes permitted by %s: %r', allowed_modes_description, allowed_modes
    )

    raise AccelerationModeConflict(
        msg, neutron_port=neutron_port, old_modes=old_modes,
        allowed_modes=allowed_modes
    )


def calculate_acceleration_modes_for_port(
    neutron_port,
    vif_type, virt_type, glance_allowed_modes=None, flavor_allowed_modes=None,
    hw_acceleration_mode=None
):
    """
    Calculates the prioritized list of hardware acceleration modes allowed for
    a particular port.
    """

    if not isinstance(neutron_port, uuid.UUID):
        raise ValueError(
            'expected UUID for neutron_port, got "{}" instead'.
            format(type(neutron_port).__name__)
        )

    # Get the initial set of candidate modes.
    if hw_acceleration_mode is None:
        # Default to allowing all modes. We explicitly list all the modes
        # instead of relying on PM.all_plug_modes, to ensure that they are
        # listed in priority order.
        port_modes = (PM.SRIOV, PM.VirtIO, PM.unaccelerated)
        assert frozenset(port_modes) == frozenset(PM.all_plug_modes), \
            'some plug mode missing from "all modes" priority list'
    else:
        # Restricted to a single user-specified mode.
        port_modes = (hw_acceleration_mode,)

    # Filter for LXC/Docker instance (cannot accelerate).
    if virt_type is not None and not _can_accelerate_virt_type(virt_type):
        logger.info(
            'port %s with instance virtualization type %r cannot be '
            'hardware accelerated', neutron_port, virt_type
        )
        port_modes = _filter_modes(
            neutron_port,
            port_modes,
            (PM.unaccelerated,),
            'instance virtualization type {!r}'.format(virt_type),
            'after filtering on modes permitted by the instance '
            'virtualization type, no eligible modes were found',
        )

    # Filter for DPDK vRouter port (cannot accelerate).
    if vif_type is not None and not _can_accelerate_vif_type(vif_type):
        logger.info(
            'port %s with vif_type %r cannot be hardware accelerated',
            neutron_port, vif_type
        )
        port_modes = _filter_modes(
            neutron_port,
            port_modes,
            (PM.unaccelerated,),
            'vif_type {!r}'.format(vif_type),
            'after filtering on modes permitted by the vif_type, '
            'no eligible modes were found',
        )

    # Filter by supported modes of the image (e.g., disable SR-IOV for images
    # not marked with "agilio.hw_acceleration_features: SR-IOV").
    port_modes = _filter_modes(
        neutron_port,
        port_modes,
        glance_allowed_modes, 'image metadata',
        'after filtering on image metadata, no eligible modes were found',
    )

    # Filter by supported modes of the flavor. Flavors can be restricted to
    # accelerated-only or unaccelerated-only operation.
    port_modes = _filter_modes(
        neutron_port,
        port_modes,
        flavor_allowed_modes, 'flavor metadata',
        'after filtering on flavor metadata, no eligible modes were found',
    )

    return port_modes


def apply_compute_node_restrictions(
    neutron_port, port_modes, hw_acceleration_modes, _root_dir=None
):
    """
    Given the set of :param: port_modes allowed for a particular port,
    restricts them to the modes available for the current compute node.
    """

    # Check port modes against the compute node configuration file.
    port_modes = _filter_modes(
        neutron_port,
        port_modes,
        hw_acceleration_modes, 'compute node configuration',
        'the compute node does not support any of the acceleration modes '
        'permitted by the instance',
    )

    # Check that the operational state of the compute node allows it to satisfy
    # its own modes.
    #
    # TODO(wbrinzer): We could also check for virtiorelayd running here.
    acceleration_possible = config_opts.is_acceleration_enabled(
        _root_dir=_root_dir
    )
    nfp_ok = config_opts.is_nfp_ok(_root_dir=_root_dir)

    if not acceleration_possible:
        logger.info(
            'hardware acceleration is not enabled on this compute node'
        )
        operational_status = (PM.unaccelerated,)
    elif not nfp_ok:
        logger.critical('the NFP is reporting an error status')
        operational_status = (PM.unaccelerated,)
    else:
        operational_status = PM.all_plug_modes

    # Check compute node modes against the compute node operational status.
    _filter_modes(
        neutron_port,
        hw_acceleration_modes,
        operational_status, 'compute node operational status',
        'none of the hardware acceleration modes configured for this compute '
        'node are operational',
        old_modes_description='modes permitted by configuration',
    )

    # Check instance modes against the compute node operational status.
    port_modes = _filter_modes(
        neutron_port,
        port_modes,
        operational_status, 'compute node operational status',
        'the compute node supports one or more acceleration modes permitted '
        'by the instance, but none of the supported mode are operational',
    )

    return port_modes


def set_acceleration_mode_for_port(
    session, neutron_port, mode, vf_pool, reservation_timeout
):
    """
    Acquires if VF, if necessary, and stores configuration information for
    :param: neutron_port in the database.

    It is OK to run this function even if there is no port object corresponding
    to :param: neutron_port in the database.
    """

    if mode in PM.accelerated_plug_modes:
        if reservation_timeout is not None:
            expires = vf.calculate_expiration_datetime(reservation_timeout)
        else:
            # e.g., for a plug operation
            expires = None

        # Allocate a VF (can raise vf.AllocationError). Note, this can perform
        # a session commit, so it is NOT safe to add the plug mode to the
        # database before calling this.
        vf_pool.allocate_vf(
            session=session, neutron_port=neutron_port, plug_mode=mode,
            expires=expires, raise_on_failure=True
        )

    # Find existing plug mode, if any.
    plug = one_or_none(
        session.query(PlugMode).
        filter(PlugMode.neutron_port == neutron_port)
    )

    if plug is None:
        # New.
        plug = PlugMode(neutron_port=neutron_port, mode=mode)
        session.add(plug)

    else:
        # Already had a plug mode.
        if plug.mode != mode:
            logger.warning(
                'changed plug mode for port %s from %s to %s', neutron_port,
                plug.mode, mode
            )
        plug.mode = mode

    return plug


def default_devname(prefix, interface_uuid):
    return (prefix + '{}').format(interface_uuid)[:network_model.NIC_NAME_LEN]


def set_config_unaccelerated(interface, interface_uuid):
    """
    Input: base config, or "ethernet" config created by unmodified
    get_config_vrouter().

    Output: "ethernet" config.
    """

    if not isinstance(interface_uuid, uuid.UUID):
        raise ValueError('expected UUID for interface_uuid')

    devname = default_devname('tap', interface_uuid)
    designer.set_vif_host_backend_ethernet_config(interface, devname)


def _clear_ethernet_config(interface):
    """
    Clear out "ethernet" config created by unmodified get_config_vrouter().
    """

    interface.script = interface.target_dev = interface.net_type = None


def set_config_sriov(interface, interface_uuid, vf_addr):
    if not isinstance(interface_uuid, uuid.UUID):
        raise ValueError('expected UUID for interface_uuid')
    if not isinstance(vf_addr, pci.PciAddress):
        raise ValueError('expected PciAddress for vf_addr')

    _clear_ethernet_config(interface)

    # FIXME(wbrinzer): In the vfio case I think we need to set this to "vfio"
    # to generate <driver name='vfio'/>.
    interface.driver_name = 'kvm'

    interface.net_type = 'hostdev'
    interface.source_dev = str(vf_addr)

    # libvirt device name. (We don't actually use this because it turns out
    # that libvirt tries to manipulate the device using this name if it's
    # specified in an <interface type='hostdev'> block.)
    # #devname = default_devname('nfp', interface_uuid)
    # #interface.target_dev = devname
    interface.target_dev = None

    interface.model = None  # not virtio!
    interface.script = None


def set_config_virtio(interface, interface_uuid, vf_number):
    if not isinstance(interface_uuid, uuid.UUID):
        raise ValueError('expected UUID for interface_uuid')
    if not isinstance(vf_number, int):
        raise ValueError('expected int for vf_number')

    _clear_ethernet_config(interface)

    # libvirt device name
    devname = default_devname('vio', interface_uuid)
    interface.target_dev = devname
    interface.model = 'virtio'
    interface.script = None

    interface.net_type = 'vhostuser'
    interface.vhostuser_type = 'unix'
    interface.vhostuser_mode = 'client'

    # TODO(wbrinzer): Make this configurable.
    interface.vhostuser_path = '/run/virtiorelayd/vf/{}'.format(vf_number)


def set_config_for_port(session, interface, neutron_port, fallback_map):
    if not isinstance(neutron_port, uuid.UUID):
        raise ValueError(
            'expected UUID for neutron_port, got "{}" instead'.
            format(type(neutron_port).__name__)
        )

    plug = session.query(PlugMode). \
        filter(PlugMode.neutron_port == neutron_port).one()

    mode = plug.mode
    if mode == PM.unaccelerated:
        set_config_unaccelerated(interface, neutron_port)
        return

    vf = session.query(VF).filter(VF.neutron_port == neutron_port).one()

    assert vf.addr is not None
    assert vf.addr in fallback_map.vfmap  # (HBD: ASSERTION MAY BE WRONG)

    if mode == PM.SRIOV:
        set_config_sriov(interface, neutron_port, vf.addr)
    elif mode == PM.VirtIO:
        set_config_virtio(
            interface, neutron_port, fallback_map.vfmap[vf.addr].vf_number
        )
    else:
        raise ValueError('unknown plug mode "{}"'.format(mode))
