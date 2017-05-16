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

from netronome.vrouter import fallback
from netronome.vrouter.database import metadata, DeclarativeBase
import netronome.vrouter.tc

import netronome.vrouter.sa.sqlite as sa_sqlite
from netronome.vrouter.sa.helpers import one_or_none
from netronome.vrouter.sa.types import GUID, PCI_ADDRESS

import logging
import random

from datetime import datetime

import sqlalchemy as db
from sqlalchemy.exc import IntegrityError
from sqlalchemy.sql.expression import or_
from sqlalchemy.types import DateTime

logger = logging.getLogger(__name__)

_NULL_GC_LOGGER = logging.getLogger('944a44c8-c65e-4210-86d0-7a9c5795ae71')
_NULL_GC_LOGGER.disabled = True

# strftime()/strptime() format for ISO8601 datetime stamps.
_ISOZ_FMT = '%Y-%m-%dT%H:%M:%SZ'

# a useful question for certain error/warning log messages.
IS_NFP_VROUTER_LOADED = 'is nfp_vrouter.ko loaded?'


def create_metadata(engine):
    metadata.create_all(engine)


def calculate_expiration_datetime(reservation_timeout, _now=None):
    """Calculate expiration datetime for a temporary VF checkout."""
    now = _now or datetime.utcnow()
    return now + reservation_timeout


def _default_created(created):
    return created or datetime.utcnow()


def default_fallback_map():
    return fallback.read_sysfs_fallback_map()


class VF(DeclarativeBase):
    __tablename__ = 'vf'

    addr = db.Column(PCI_ADDRESS(), nullable=False, primary_key=True)
    neutron_port = db.Column(GUID(), nullable=False, unique=True)
    expires = db.Column(DateTime())
    created = db.Column(DateTime(), nullable=False)

    def __init__(
        self, addr, neutron_port=None, expires=None, created=None, **kwds
    ):
        super(VF, self).__init__(**kwds)

        self.addr = addr
        self.neutron_port = neutron_port
        self.expires = expires
        self.created = _default_created(created)

    def __repr__(self):
        return 'VF(addr={}, neutron_port={}, expires={}, created={})'.format(
            repr(self.addr), repr(self.neutron_port), repr(self.expires),
            repr(self.created)
        )


class AllocationError(Exception):
    """Raised (optionally) on VF allocation failure."""
    def __init__(self, msg, neutron_port):
        Exception.__init__(self, msg)
        self.neutron_port = neutron_port


def _gc_find_expired(session, _now=None):
    """Find expired VF reservations."""

    now = datetime.utcnow() if _now is None else _now

    q = (
        session.query(VF).
        filter(VF.expires != None).  # noqa
        filter(VF.expires <= now).
        order_by(VF.addr)
    )

    return q.all()


class GcExpired(object):
    """
    A simple garbage collector which deletes expired VF reservations. Return a
    set of PCIe addresses indicating which VFs were reclaimed.
    """

    def __init__(self, vfset, **kwds):
        super(GcExpired, self).__init__(**kwds)
        self.vfset = vfset

    def __call__(self, session, logger, _now=None):
        evict = _gc_find_expired(session, _now)
        reclaimed = set()

        for vf in evict:
            if vf in self.vfset:
                fmt = 'GC reclaimed VF %s (expired)'
            else:
                fmt = 'GC reclaimed bogus VF %s (expired)'

            reclaimed.add((VF, vf.addr))
            logger.debug(fmt, vf.addr)

            session.delete(vf)

        return reclaimed


class RandomVFChooser(object):
    def __call__(self, avail, session, fallback_map):
        return random.choice(list(avail))

DefaultVFChooser = RandomVFChooser


class Pool(object):
    def __init__(self, fallback_map, gc=None, vf_chooser=None, **kwds):
        super(Pool, self).__init__(**kwds)

        self.fallback_map = fallback_map

        if vf_chooser is not None:
            self.vf_chooser = vf_chooser
        else:
            self.vf_chooser = lambda m: DefaultVFChooser()

        self._vfset = frozenset(fallback_map.vfset)
        if self.fallback_map_len() == 0:
            logger.warning('fallback map is empty (%s)', IS_NFP_VROUTER_LOADED)

        self._gc = filter(
            lambda g: g is not None, (gc, GcExpired(vfset=self._vfset))
        )

    def fallback_map_len(self):  # primarily for testing
        return len(self._vfset)

    def _check_if_addr_in_vfset(self, vf):
        if vf.addr in self._vfset:
            return True

        logger.warning(
            '%s',
            'VF {} is marked in-use by port {}, but this VF does not '
            'exist in the fallback map. Was the NFP moved to a '
            'different PCIe slot?'.format(
                vf.addr, vf.neutron_port
            )
        )
        return False

    def gc(self, session, _now, logger=None):
        logger = _NULL_GC_LOGGER if logger is None else logger

        reclaimed = set()
        for gc in self._gc:
            reclaimed.update(gc(session=session, logger=logger, _now=_now))

        return reclaimed

    def allocate_vf(
        self, session, neutron_port, plug_mode, expires,
        raise_on_failure=False, _now=None
    ):
        """
        Allocate a VF. Perform GC as needed.

        Return the PCIe address of the VF allocated, or None if no VF was
        available. If `raise_on_failure` is True, raises AllocationError on
        failure instead.

        This function may commit `session` one or more times.
        """

        # format the expiration timestamp for logging
        if expires is None:
            expires_str = 'permanent'
        else:
            expires_str = expires.strftime(_ISOZ_FMT)

        # calculate our own value of "now" so that we can warn when allocating
        # a VF with an expiration timestamp in the past
        now = datetime.utcnow() if _now is None else _now

        # see if there is an existing VF allocation for this port
        q = session.query(VF).filter(VF.neutron_port == neutron_port)
        vf = one_or_none(q)
        if vf is not None:
            self._check_if_addr_in_vfset(vf)

            if vf.expires != expires:
                log = logger.info
                msg = ['VF {} (port {}):'.format(vf.addr, vf.neutron_port)]

                if vf.expires is None:
                    msg.append('converted permanent allocation to temporary,')

                if expires is None:
                    msg.append('converted temporary allocation to permanent')
                else:
                    msg.append('now expiring at {}'.format(expires))

                    if expires <= now:
                        msg.append('(*already expired*)')
                        log = logger.warning

                log('%s', ' '.join(msg))

                vf.expires = expires

            return vf.addr

        # No existing allocation, make a new one.

        # Run garbage collection before attempting to allocate. (The original
        # code only ran GC when needed. Running it every time makes the code
        # more deterministic, and also keeps the database in a cleaner state in
        # the case of a crash+port leak. See VRT-604.)
        reclaimed = self.gc(session, _now=now, logger=logger)
        session.commit()

        def _not_avail():
            msg = (
                'no VF resources available for port {}'
                .format(neutron_port)
            )
            logger.error('%s', msg)

            if raise_on_failure:
                raise AllocationError(msg, neutron_port=neutron_port)
            else:
                return None

        # Set up an object that can perform the allocation. vf_chooser()
        # allowed to perform virtiorelayd configuration queries (VRT-802).
        choose_vf = self.vf_chooser(m=plug_mode)

        # Perform the allocation. Retry on concurrent allocation conflict
        # (VRT-755).
        RETRIES = 10
        for retries in xrange(RETRIES):
            q = session.query(VF)

            # Find available VFs.
            avail = set(self._vfset)
            for vf in q:
                if not self._check_if_addr_in_vfset(vf):
                    continue

                avail.discard(vf.addr)

            if not avail:
                return _not_avail()

            # Choose one.
            addr = choose_vf(
                avail=avail, session=session, fallback_map=self.fallback_map
            )
            try:
                session.add(VF(addr, neutron_port, expires))
                session.commit()
                break

            except IntegrityError as e:
                logger.debug(
                    'port {}: concurrent allocation conflict on VF {}'.
                    format(neutron_port, addr)
                )
                session.rollback()

        else:
            logger.error(
                'port {}: too many concurrent allocation conflicts; '
                'giving up'.format(neutron_port)
            )
            return _not_avail()

        # Log the result.
        log = logger.info
        if expires is not None:
            if expires > now:
                expires_str = 'expires at {}'.format(expires_str)
            else:
                expires_str = '*already expired* at {}'.format(expires_str)
                log = logger.warning

        log(
            '%s (%s)',
            'allocated VF {} to port {}'.format(addr, neutron_port),
            expires_str
        )

        return addr

    def deallocate_vf(self, session, addr):
        """
        Deallocate a VF. Perform GC as needed.

        This function does NOT commit `session`.
        """

        # NOTE: deallocate_vf() is allowed to perform garbage collection if
        # desired (but it doesn't do so, as of 2016-06-13).

        vf = session.query(VF).get(addr)
        if vf is not None:
            session.delete(vf)
            logger.info('%s', 'deallocated VF {}'.format(addr))

    def list_vfs(self, session, include_expired=False, _now=None):
        """
        List all VFs. Perform GC as needed.
        """

        now = datetime.utcnow() if _now is None else _now

        q = session.query(VF)
        if not include_expired:
            q = q.filter(or_(VF.expires == None, VF.expires > now))  # noqa

        return q.all()
