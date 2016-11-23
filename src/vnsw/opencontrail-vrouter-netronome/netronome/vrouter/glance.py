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

HW_ACCELERATION_FEATURES_PROPERTY = 'agilio.hw_acceleration_features'

SRIOV_FEATURE_TOKEN = 'SR-IOV'


def allowed_hw_acceleration_modes(image_properties, name=None, id=None):
    """
    Maps :param: image_properties dict to an ordered collection of allowed
    hardware acceleration modes, sorted from high to low preference.
    """

    if not isinstance(image_properties, dict):
        raise ValueError('image_properties must be a dict')

    tokens = filter(len, map(
        lambda s: s.strip(),
        image_properties.get(HW_ACCELERATION_FEATURES_PROPERTY, '').split(',')
    ))

    ans = (PM.VirtIO, PM.unaccelerated)
    seen = set()
    unknown = []

    for t in tokens:
        if t in seen:
            continue

        seen.add(t)
        if t == SRIOV_FEATURE_TOKEN:
            ans = (PM.SRIOV,) + ans
        else:
            unknown.append(t)

    if unknown:
        if name is not None or id is not None:
            display_name = 'image {!r}'.format({
                k: v for k, v in (('name', name), ('id', id)) if v is not None
            })
        else:
            display_name = 'image'

        msg = (
            '{} has unknown token(s) in {!r} property: {!r}'
            .format(display_name, HW_ACCELERATION_FEATURES_PROPERTY, unknown)
        )

        logger.warning('%s', msg)

    return ans
