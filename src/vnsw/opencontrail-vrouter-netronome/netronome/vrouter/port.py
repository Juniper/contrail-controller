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

from datetime import datetime

import errno
import logging
import os
import uuid

import sqlalchemy_ as db
from sqlalchemy_.orm import relationship, foreign, remote
from sqlalchemy_.types import CHAR, DateTime

import netronome.vrouter.sa.sqlite as sa_sqlite
from netronome.vrouter.sa.types import GUID, PCI_ADDRESS
from netronome.vrouter.database import metadata, DeclarativeBase
from netronome.vrouter import (plug_modes as PM, tc, vf)

logger = logging.getLogger(__name__)

# Order is important.
_PORT_TYPES = ('NovaVMPort', 'NameSpacePort')


def create_metadata(engine):
    metadata.create_all(engine)


class PortTypeTc(tc.Tc):
    """
    Check values for PortType according to the Juniper vrouter-port-control
    script's rules.
    """

    def __init__(self, **kwds):
        super(PortTypeTc, self).__init__(**kwds)

    def check(self, value):
        return isinstance(value, basestring) and value in _PORT_TYPES

    def get_message(self, value):
        return 'unknown port type: "{}"'.format(value)

VIF_TYPE_VHOSTUSER = 'VhostUser'
_VIF_TYPE_VROUTER = 'Vrouter'
_VIF_TYPES = (VIF_TYPE_VHOSTUSER, _VIF_TYPE_VROUTER)


class VifTypeTc(tc.Tc):
    """
    Check values for VifType according to the Juniper vrouter-port-control
    script's rules.
    """

    def __init__(self, **kwds):
        super(VifTypeTc, self).__init__(**kwds)

    def check(self, value):
        return isinstance(value, basestring) and value in _VIF_TYPES

    def get_message(self, value):
        return 'unknown VIF type: "{}"'.format(value)


def coerce_uuid(item, nullable=False):
    if isinstance(item, uuid.UUID):
        return item
    elif item is None and nullable:
        return None
    else:
        return uuid.UUID(item)


def _default_created(created):
    return created or datetime.utcnow()


class PlugMode(DeclarativeBase):
    __tablename__ = 'plug_mode'

    neutron_port = db.Column(GUID(), nullable=False, primary_key=True)
    mode         = db.Column(
        db.Enum(*PM.all_plug_modes), nullable=False
    )
    created = db.Column(DateTime(), nullable=False)

    def __init__(self, neutron_port=None, mode=None, created=None, **kwds):
        super(PlugMode, self).__init__(**kwds)

        self.neutron_port = neutron_port
        self.mode = mode
        self.created = _default_created(created)

    def __repr__(self):
        return 'PlugMode({}, mode={})'.format(
            repr(self.neutron_port), repr(self.mode)
        )


def _default(o, default):
    return o if o is not None else default


class Port(DeclarativeBase):
    __tablename__ = 'port'

    # not using db.ForeignKey('vf.neutron_port') here because it is legal to
    # have ports without an associated VF (for unaccelerated plug mode).
    uuid             = db.Column(GUID(), primary_key=True)

    instance_uuid    = db.Column(GUID())
    vn_uuid          = db.Column(GUID())
    vm_project_uuid  = db.Column(GUID())
    ip_address       = db.Column(db.String(length=15))
    ipv6_address     = db.Column(db.String(length=45, collation='nocase'))
    vm_name          = db.Column(db.String())
    mac              = db.Column(CHAR(17, collation='nocase'), nullable=False)
    tap_name         = db.Column(db.String(), unique=True, nullable=False)
    port_type        = db.Column(db.Enum(*_PORT_TYPES), nullable=False)
    vif_type         = db.Column(db.Enum(*_VIF_TYPES), nullable=False)
    tx_vlan_id       = db.Column(db.Integer(), nullable=False)
    rx_vlan_id       = db.Column(db.Integer(), nullable=False)
    vhostuser_socket = db.Column(db.String())

    plug = relationship(
        'PlugMode', uselist=False, cascade='all',

        # primaryjoin is required here because we don't have a true ForeignKey
        # constraint on Port.uuid.
        primaryjoin='foreign(Port.uuid)==remote(PlugMode.neutron_port)'
    )

    vf = relationship(
        'VF', uselist=False, cascade='all',

        # primaryjoin is required here because we don't have a true ForeignKey
        # constraint on Port.uuid.
        primaryjoin='foreign(Port.uuid)==remote(VF.neutron_port)'
    )

    created = db.Column(DateTime(), nullable=False)

    def __init__(
        self, uuid, instance_uuid, vn_uuid, vm_project_uuid, ip_address,
        ipv6_address, vm_name, mac, tap_name, port_type, vif_type, tx_vlan_id,
        rx_vlan_id, vhostuser_socket=None, created=None, **kwds
    ):
        super(Port, self).__init__(**kwds)

        self.uuid             = coerce_uuid(uuid)
        self.instance_uuid    = coerce_uuid(instance_uuid, nullable=True)
        self.vn_uuid          = coerce_uuid(vn_uuid, nullable=True)
        self.vm_project_uuid  = coerce_uuid(vm_project_uuid, nullable=True)
        self.ip_address       = ip_address
        self.ipv6_address     = ipv6_address
        self.vm_name          = vm_name
        self.mac              = mac
        self.tap_name         = tap_name
        self.port_type        = port_type
        self.vif_type         = vif_type
        self.tx_vlan_id       = int(tx_vlan_id)
        self.rx_vlan_id       = int(rx_vlan_id)
        self.vhostuser_socket = vhostuser_socket

        self.created          = _default_created(created)

        # SQLAlchemy tries to blank-out the primary key (self.uuid) if we
        # perform these assignments. The ORM nulls out the fields for us by
        # default anyway.
        # self.plug             = None
        # self.vf               = None

        PortTypeTc().assert_valid(port_type)
        VifTypeTc().assert_valid(vif_type)

        # (following vrouter-port-control)
        if vhostuser_socket is None and vif_type != _VIF_TYPE_VROUTER:
            raise ValueError(
                'expected vif_type "{}" when vhostuser_socket is None'.
                format(_VIF_TYPE_VROUTER)
            )
        elif vhostuser_socket is not None and vif_type != VIF_TYPE_VHOSTUSER:
            raise ValueError(
                'expected vif_type "{}" when vhostuser_socket is not None'.
                format(VIF_TYPE_VHOSTUSER)
            )

    def actual_name(self, tap_name):
        if tap_name is None:
            return self.tap_name

        # This came up in development.
        elif not isinstance(tap_name, basestring):
            raise ValueError('tap_name must be None or basestring')

        else:
            return tap_name

    def dump(self, tap_name=None, _now=None):
        """
        Format the port as a Contrail vRouter Agent port create POST request.
        Following vrouter-port-control's GetJSonDict function.
        """

        # Unfortunately this uses the local timezone, but we have to since we
        # are following vrouter-port-control.
        now = datetime.now() if _now is None else _now

        ans = {
            'id': self.uuid,
            'instance-id': self.instance_uuid,

            # Contrail uses the literal string 'None' for None.
            'ip-address': _default(self.ip_address, 'None'),
            'ip6-address': _default(self.ipv6_address, 'None'),

            'vn-id': self.vn_uuid,
            'display-name': self.vm_name,
            'vm-project-id': self.vm_project_uuid,
            'mac-address': self.mac,
            'system-name': self.actual_name(tap_name),
            'type': _PORT_TYPES.index(self.port_type),
            'rx-vlan-id': self.rx_vlan_id,
            'tx-vlan-id': self.tx_vlan_id,
            'author': __name__,
            'time': now,
        }

        for var in ('id', 'instance-id', 'vn-id', 'vm-project-id', 'time'):
            if ans[var] is not None:
                ans[var] = str(ans[var])

        return ans


def _in_port_dir(port_dir, fname):
    try:
        os.stat(os.path.join(port_dir, fname))
        return True

    except (IOError, OSError) as e:
        # Only ENOENT proves that GC is safe.
        return e.errno != errno.ENOENT


class GcNotInPortDir(object):
    """
    A garbage collector that deletes VF reservations for ports not found in the
    agent port database.

    Objects that have existed for less than a certain minimum time (the "grace
    period," presumably on the order of the time it takes to complete the plug
    process) are exempt.

    If no grace period is specified, objects become eligible for GC
    immediately.
    """

    def __init__(self, port_dir, grace_period, **kwds):
        super(GcNotInPortDir, self).__init__(**kwds)
        self.port_dir = port_dir

        # Amount of time that a port is allowed to sit in the "created" state
        # without being present in the agent port database.
        self.grace_period = grace_period

    def __call__(self, session, logger, _now=None):
        """
        Find and eliminate objects whose associated Neutron ports are not in
        the agent port database.
        """

        now = datetime.utcnow() if _now is None else _now
        reclaimed = set()

        def _q(t):
            q = session.query(t)
            if self.grace_period is not None:
                q = q.filter(t.created < now - self.grace_period)

            return q

        # Cross-reference our port database against the agent database.
        # Deleting ports cascades to the associated VFs and plug modes.
        q = _q(Port).order_by(Port.created)
        evict = [o for o in q if not _in_port_dir(self.port_dir, str(o.uuid))]

        for o in evict:
            fmt = 'GC reclaimed port %s (not in %s)'
            logger.debug(fmt, o.uuid, self.port_dir)

            if o.vf is not None:
                fmt = 'GC reclaimed VF %s (cascade from port %s)'
                logger.debug(fmt, o.vf.addr, o.uuid)
                reclaimed.add((vf.VF, o.vf.addr))

            if o.plug is not None:
                fmt = 'GC reclaimed plug mode for %s (cascade)'
                logger.debug(fmt, o.uuid)
                reclaimed.add((PlugMode, o.uuid))

            reclaimed.add((Port, o.uuid))
            session.delete(o)

        # Make a list of ports that still exist. Check that we are correctly
        # building a list of ports and not a list of single-element tuples
        # (which actually happened at one point during development).
        good_ports = frozenset([o[0] for o in session.query(Port.uuid)])
        assert all(map(lambda o: isinstance(o, uuid.UUID), good_ports))

        # Check permanent VF reservations (we assume that there should be a
        # port for these) against the set of ports known to *our* database. (We
        # assume that temporary reservations will be expired by some other
        # garbage collector.)
        q = (
            _q(vf.VF).
            filter(vf.VF.expires == None).order_by(vf.VF.addr)  # noqa
        )
        evict = [o for o in q if o.neutron_port not in good_ports]

        for o in evict:
            fmt = (
                'GC reclaimed VF %s '
                '(associated with inactive port %s)'
            )
            logger.debug(fmt, o.addr, o.neutron_port)

            reclaimed.add((vf.VF, o.addr))
            session.delete(o)

        # Same thing for plug modes.
        q = _q(PlugMode).order_by(PlugMode.created)
        evict = [o for o in q if o.neutron_port not in good_ports]

        for o in evict:
            fmt = (
                'GC reclaimed plug mode for %s '
                '(associated with inactive port)'
            )
            logger.debug(fmt, o.neutron_port)

            reclaimed.add((PlugMode, o.neutron_port))
            session.delete(o)

        return reclaimed
