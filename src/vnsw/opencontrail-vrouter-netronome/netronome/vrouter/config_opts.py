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

import json
import logging
import os
import re
import uuid

from datetime import timedelta
from oslo_config import (cfg, types)

from netronome.vrouter import plug_modes as PM
from netronome.vrouter.plug_modes import all_plug_modes

_TIMEDELTA_RE = re.compile(r'''
    ((?P<days>\d+?)d)?
    ((?P<hours>\d+?)h)?
    ((?P<minutes>\d+?)m)?
    ((?P<seconds>\d+?)s)?
    \Z
''', re.X)


def parse_timedelta(timedelta_str):
    m = _TIMEDELTA_RE.match(timedelta_str)
    mg = {} if m is None else m.groupdict()
    kwds = {k: int(v) for k, v in mg.iteritems() if v is not None}

    if not kwds:
        raise ValueError('invalid timedelta: "{}"'.format(timedelta_str))

    return timedelta(**kwds)


def format_timedelta(td):
    # (this is mostly needed for testing)
    ans = []

    if td.days:
        ans.append('{}d'.format(td.days))

    hours = int(td.seconds / 3600)
    minutes = int(td.seconds / 60) % 60
    seconds = td.seconds % 60

    if hours:
        ans.append('{}h'.format(hours))
    if minutes:
        ans.append('{}m'.format(minutes))

    if td.microseconds:
        microseconds_str = '.{:06}'.format(td.microseconds).rstrip('0')
    else:
        microseconds_str = ''

    if seconds or microseconds_str or not ans:
        ans.append('{}{}s'.format(seconds, microseconds_str))

    return ''.join(ans)


class TimeDelta(types.ConfigType):
    """
    Positive time delta type.

    Converts value to a datetime.timedelta.
    """

    BASE_TYPES = (timedelta,)

    def __call__(self, value):
        if isinstance(value, timedelta):
            return value

        return parse_timedelta(value)

    def __repr__(self):
        return 'TimeDelta'

    def __eq__(self, other):
        return self.__class__ == other.__class__


class Uuid(types.ConfigType):
    """
    UUID type.

    Converts value to UUID.uuid.
    """

    BASE_TYPES = (uuid.UUID,)

    def __call__(self, value):
        if isinstance(value, uuid.UUID):
            return value

        return uuid.UUID(value)

    def __repr__(self):
        return 'UUID'

    def __eq__(self, other):
        return self.__class__ == other.__class__


class Json(types.ConfigType):
    """
    JSON type.

    Converts value using json.loads().
    """

    def __call__(self, value):
        return json.loads(value)

    def __repr__(self):
        return 'JSON'

    def __eq__(self, other):
        return self.__class__ == other.__class__


class MacAddress(types.ConfigType):
    """
    MAC address type.

    Validates value.
    """

    __PATTERN = re.compile(r'^B(?::B){5}\Z'.replace('B', '[A-Fa-f0-9]' * 2))

    def __call__(self, value):
        try:
            if self.__PATTERN.match(value) is not None:
                return value
        except Exception as e:
            pass

        raise ValueError('invalid MAC address "{}"'.format(value))

    def __repr__(self):
        return 'MacAddress'

    def __eq__(self, other):
        return self.__class__ == other.__class__


def choices_help(s, choices):
    return s + ': {{{}}}'.format(','.join(choices))


def default_help(s, default):
    return '{} (default: {})'.format(s, default)

hw_acceleration_modes_opt = cfg.Opt(
    'hw_acceleration_modes',
    type=types.List(item_type=types.String(choices=PM.all_plug_modes)),
    default=None,
    help=choices_help(
        'List of allowed hardware acceleration modes', PM.all_plug_modes
    ),
)

hw_acceleration_mode_choices = (PM.VirtIO, PM.SRIOV, 'off')


def hw_acceleration_mode_opt_properties():
    return 'hw-acceleration-mode', {
        'type': types.String(choices=hw_acceleration_mode_choices),
        'default': None,
        'metavar': 'MODE',
        'help': choices_help(
            'Hardware acceleration mode', hw_acceleration_mode_choices
        ),
    }


def hw_acceleration_mode_opt():
    name, kwds = hw_acceleration_mode_opt_properties()
    return cfg.Opt(name, **kwds)

default_reservation_timeout = parse_timedelta('1h')

reservation_timeout_opt = cfg.Opt(
    'reservation-timeout',
    type=TimeDelta(),
    default=default_reservation_timeout,
    help=default_help(
        'SR-IOV/VirtIO reservation timeout',
        format_timedelta(default_reservation_timeout)
    ),
)

default_database_path = '/var/lib/netronome/vrouter/vrouter.sqlite3'

database_opt = cfg.Opt(
    'database',
    type=types.String(),
    default=default_database_path,
    help=default_help('Port database', default_database_path),
)

opts = (
    hw_acceleration_modes_opt,
)

cli_opts = (
    hw_acceleration_mode_opt(),
    database_opt,
    reservation_timeout_opt,
)

cli_logging_group = cfg.OptGroup(
    name='log',
    title='Logging options',
)

log_level_default = 'info'
log_level_choices = ('debug', 'info', 'warning', 'error', 'critical', 'none')

log_level_opt = cfg.Opt(
    'level',
    type=types.String(choices=log_level_choices),
    metavar='LEVEL',
    default=log_level_default,
    help=default_help(
        choices_help('Granularity of logging output', log_level_choices),
        log_level_default,
    ),
)

log_file_opt = cfg.Opt(
    'file',
    type=types.String(),
    metavar='FILENAME',
    help='Log filename',
)

cli_logging_opts = (
    log_level_opt,
    log_file_opt,
)


def create_conf():
    conf = cfg.ConfigOpts()
    conf.register_opts(opts)
    conf.register_cli_opts(cli_opts)
    return conf


def iommu_mode_sort(modes):
    """
    Sort :param: modes from most to least IOMMU restrictive.

    (This makes it easy for the "iommu_check" subcommand to restrict itself to
    the most restrictive IOMMU mode specified in vrouter-port-control.conf, per
    ICONIC-19 design notes.)
    """
    return [m for m in (PM.VirtIO, PM.SRIOV, PM.unaccelerated) if m in modes]


def parse_conf(
    conf=None, prog=None, version=None, args=None, default_config_files=None
):
    conf = create_conf() if conf is None else conf
    conf(
        project='contrail',
        prog=prog,
        version=version,
        args=args,
        default_config_files=default_config_files,
        validate_default_values=True,
    )

    # Set default value for hw_acceleration_modes. The Oslo defaults don't
    # quite do what we want.
    #
    #   - If the list is present but empty, Oslo defaults the value to [].
    #   - If the list contains configuration errors Oslo deletes the option
    #     from conf completely, causing client code to raise NoSuchOptError.
    #
    # Ideally in the second case we would raise an error while reading the
    # configuration file, but at least we can provide a sensible default rather
    # than creating a set of confusing action at a distance bugs.
    if not hasattr(conf, 'hw_acceleration_modes') \
       or conf.hw_acceleration_modes is None \
       or conf.hw_acceleration_modes == []:
        conf.hw_acceleration_modes = [PM.unaccelerated]

    # Sort hw_acceleration_modes from most to least IOMMU restrictive.
    conf.hw_acceleration_modes = iommu_mode_sort(conf.hw_acceleration_modes)

    # Convert hw_acceleration_mode of "off" to internal value "unaccelerated."
    if hasattr(conf, 'hw_acceleration_mode'):
        if conf.hw_acceleration_mode == 'off':
            conf.hw_acceleration_mode = PM.unaccelerated
    else:
        conf.hw_acceleration_mode = None

    return conf

LOGGING_FORMAT = '[%(asctime)s] %(levelname)s: %(name)s: %(message)s'


def apply_logging_conf(conf):
    if conf.log.level == 'none':
        logging.root.addHandler(logging.NullHandler())
        return

    kwds = {
        'format': LOGGING_FORMAT,
        'level': getattr(logging, conf.log.level.upper()),
    }
    if conf.log.file is not None:
        kwds['filename'] = conf.log.file

    logging.basicConfig(**kwds)


def _check_sysfs_int(relative_path, check, _root_dir=None):
    assert not relative_path.startswith('/'), 'relative_path must be relative'

    if _root_dir is None:
        _root_dir = '/'

    sysfs_fpath = os.path.join(_root_dir, relative_path)
    try:
        with open(sysfs_fpath, 'r') as fh:
            return check(int(fh.next()))

    except (StopIteration, IOError, OSError, ValueError):
        return False


def is_nfp_ok(_root_dir=None):
    """
    Returns true if the NFP is enabled and the status reports OK (e.g., there
    have not been any PCIe errors, ...)
    """

    return _check_sysfs_int(
        'sys/module/nfp_vrouter/control/nfp_status', lambda i: i != 0,
        _root_dir=_root_dir
    )


def is_acceleration_enabled(_root_dir=None):
    """
    Returns true if this machine is currently able to use an NFP-accelerated
    plug mode.
    """

    return _check_sysfs_int(
        'sys/module/nfp_vrouter/control/physical_vif_count', lambda i: i > 0,
        _root_dir=_root_dir
    )


def is_ksm_enabled(_root_dir=None):
    """
    Returns true if Kernel Shared Memory (KSM) is enabled on this machine.
    This feature can contribute to VirtIO instability, and possibly poor
    performance.
    """

    return _check_sysfs_int(
        'sys/kernel/mm/ksm/run', lambda i: i != 0, _root_dir=_root_dir
    )
