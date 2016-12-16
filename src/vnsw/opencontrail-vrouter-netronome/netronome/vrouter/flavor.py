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

from netronome.vrouter import plug_modes as PM

logger = logging.getLogger(__name__)

HW_ACCELERATION_PROPERTY = 'agilio:hw_acceleration'

# VirtIO instances require hugepage memory backing.
HW_MEM_PAGE_SIZE_PROPERTY = 'hw:mem_page_size'


def _flavor_display_name(name):
    if name is not None:
        n = name.strip()
        if n != '':
            return n

# Default to "don't care" unless the user specified that acceleration is
# required or forbidden.
_mode_map = {
    '': (PM.SRIOV, PM.VirtIO, PM.unaccelerated),
    'on': (PM.SRIOV, PM.VirtIO),
    'off': (PM.unaccelerated,),
    'SR-IOV': (PM.SRIOV,),
    'VirtIO': (PM.VirtIO,),
}


def allowed_hw_acceleration_modes(extra_specs, name=None):
    """
    Maps :param: extra_specs dict to an ordered collection of allowed hardware
    acceleration modes, sorted from high to low preference.
    """

    if not isinstance(extra_specs, dict):
        raise ValueError('extra_specs must be a dict')

    p = extra_specs.get(HW_ACCELERATION_PROPERTY, '').strip()
    if p not in _mode_map:
        msg = [
            'unknown value {!r} for extra_spec {!r}'
            .format(p, HW_ACCELERATION_PROPERTY)
        ]

        n = _flavor_display_name(name)
        if n is not None:
            msg.append('in flavor {!r}'.format(n))

        logger.warning('%s', ' '.join(msg))

    return _mode_map.get(p, _mode_map[''])


class FlavorVirtIOConfigError(Exception):
    """
    Raised when a flavor does not have the hw:mem_page_size extra_spec,
    indicating that it is not suitable for use with virtiorelayd.
    """
    pass


def assert_flavor_supports_virtio(extra_specs, name=None):
    """
    Raises FlavorVirtIOConfigError if :param: extra_specs does not contain the
    hw:mem_page_size extra_spec, indicating that the flavor :param: name is not
    configured properly for use with virtiorelayd.

    At the moment, any flavor with hw:mem_page_size configured will pass this
    test. That isn't quite accurate (the key can be configured to force
    hugepages off), but it's close enough for anti-foot-shooting purposes.
    """

    if HW_MEM_PAGE_SIZE_PROPERTY in extra_specs:
        return

    n = _flavor_display_name(name)
    flavor_display_name = 'flavor' if n is None else 'flavor {!r}'.format(n)

    msg = (
        '{} is missing the {!r} extra_spec and cannot be used with instances '
        'configured for VirtIO hardware acceleration mode'
        .format(flavor_display_name, HW_MEM_PAGE_SIZE_PROPERTY)
    )

    logger.critical(msg)
    raise FlavorVirtIOConfigError(msg)
