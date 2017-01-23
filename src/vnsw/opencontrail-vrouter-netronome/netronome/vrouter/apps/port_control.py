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

from netronome import subcmd
from netronome.subcmd import exception_msg
from netronome.vrouter import (
    config, config_opts, config_editor, database, fallback, flavor, glance,
    plug, plug_modes as PM, port, vf
)
from netronome.vrouter.config_opts import (choices_help, default_help)
from netronome.vrouter.sa.helpers import one_or_none

import json
import logging
import re
import sys

from datetime import timedelta
from lxml import etree
from nova.virt.libvirt.config import LibvirtConfigGuestInterface
from oslo_config import (cfg, types)
from sqlalchemy.orm.session import sessionmaker

_OPER_RE = re.compile(r'\A--oper=(\w+)\Z')


def _split_single_string_arg(args):
    """The original (unaccelerated) nova-compute and vrouter_api.py pass all
    the vrouter-port-control command line arguments lumped together as a single
    string. vrouter-port-control, in turn, attempts to split these on a regex
    that matches only whitespace followed by an option.

    This approach is vulnerable to injection attacks, see Launchpad #1584625.
    Later code introduced a workaround involving double quotes, but this is
    still vulnerable to a (different) injection attack. The correct fix is to
    pass the command-line arguments as a list rather than lumped together into
    a single string.

    For the sake of compatibility with unaccelerated nova-compute, if the
    command-line arguments are passed as a single string, we attempt to split
    on them on one of the two original regexes here, stripping quotes if
    applicable.

    Once patches are upstreamed to nova-compute and contrail-nova-vif-driver to
    pass the arguments as a list, this code should be removed.
    """

    if len(args) != 2:
        # Disable the workaround if the arguments were passed as a list.
        return args

    if '"' not in args[1]:
        # Split on original vrouter-port-control regex.
        ans = re.split(r'\s+(?=--)', args[1])

    else:
        # Split on LP1584625 regex.
        ans = re.split(r'\s+(?=--[_A-Z0-9]+=".*?")', args[1], flags=re.I)

        # Strip quotes (following LP1584265 vrouter-port-control).
        opt_re = re.compile(r'\A--(?P<opt>[_A-Z0-9]+)="(?P<arg>.*?)"\Z', re.I)
        strip_quotes = lambda m: '--{opt}={arg}'.format(**m.groupdict())
        ans = map(lambda s: opt_re.sub(strip_quotes, s), ans)

    return args[:1] + ans


def _convert_oper_to_subcmd(args):
    """Convert traditional --oper=CMD vrouter-port-control syntax to the newer
    subcommand format."""

    cmd = []
    out = []

    for a in args[1:]:
        m = _OPER_RE.match(a)
        if m:
            # Special --oper=CMD argument. Convert to subcommand.
            if cmd:
                # Multiple --oper=CMD arguments are nonsensical. Don't attempt
                # to implement compatibility support.
                return args
            cmd = ['--', m.group(1)]
        elif a == '--':
            # Assume that a command that already includes -- does NOT need
            # compatibility support.
            return args
        else:
            # Regular argument.
            out.append(a)

    return args[:1] + cmd + out


def fixup(args):
    """Apply unpatched nova-compute compatibility fixes to args."""

    fixups = (
        _split_single_string_arg,
        _convert_oper_to_subcmd,
    )
    return reduce((lambda x, y: y(x)), fixups, args)


def _make_vf_pool(fallback_map, port_dir, grace_period):
    return vf.Pool(
        fallback_map,
        gc=port.GcNotInPortDir(port_dir=port_dir, grace_period=grace_period),
    )

# Hard-coded per initial discussion 2016-11-11. This represents the time that a
# port is allowed to be in the fully created (permanent reservation) state in
# the VF database, without a corresponding entry in the vRouter agent port
# database. See VRT-604.
#
# TODO(wbrinzer): Make this configurable.
_VRT_604_GRACE_PERIOD = timedelta(minutes=5)


class OsloConfigSubcmd(subcmd.Subcmd):
    """A subcommand with command-line parsing based on oslo_config."""

    cli_opts = ()
    opts = ()

    def __init__(self, **kwds):
        super(OsloConfigSubcmd, self).__init__(**kwds)

        self.conf = None

    def create_conf(self):
        conf = cfg.ConfigOpts()
        conf.register_cli_opts(self.cli_opts)
        conf.register_opts(self.opts)

        # Register config_opts options.
        conf.register_opts(config_opts.opts)

        self.apply_features('create_conf', conf)

        # Do NOT register the other config_opts command-line options. Clients
        # are responsible for including these options in their cli_opts, if
        # they want them.

        return conf

    def parse_args(self, prog, command, args, default_config_files=None):
        self.conf = config_opts.parse_conf(
            conf=self.create_conf(),
            prog=prog,
            args=args,
            default_config_files=default_config_files
        )


def _init_Session(conf, logger):
    """Setup SQLAlchemy Session factory object."""

    engine, dsn = database.create_engine(conf.database)
    logger.debug('using DSN: %s', dsn)
    try:
        port.create_metadata(engine)
        vf.create_metadata(engine)
    except Exception as e:
        logger.critical('%s', exception_msg(e, 'DSN: {}'.format(dsn)))
        return None

    return sessionmaker(bind=engine)


def _get_nova_object_data(v, namespace, name):
    """
    Check for an acceptable value of nova_object.namespace and
    nova_object.name (i.e., the type name sent over from Nova). If the check
    passes, return nova_object.data, else None.

    For now, we only allow one expected type (namespace.name) at a time (unlike
    config_editor.deserialize()).
    """

    if 'nova_object.version' not in v:
        # Note that we currently check only check that nova_object.version is
        # present; we don't check its value.
        return

    ns, n = v['nova_object.namespace'], v['nova_object.name']
    if ns != namespace or n != name:
        raise ValueError(
            'nova_object type {}.{} is not allowed; expected type {}.{}'.
            format(ns, n, namespace, name)
        )

    return v['nova_object.data']


def _decode_nova_object(value, decoders, error_msg):
    """
    Attempt to decode `value` using each of a sequence of `decoders`.
    Raise ValueError(error_msg) if no decoder succeeds.
    """

    for d in decoders:
        decoded = d(value)
        if decoded is not None:
            return decoded

    raise ValueError(error_msg)


def _decode_image_metadata_mitaka(v):
    try:
        d = _get_nova_object_data(v, 'nova', 'ImageMeta')
        if d is None:
            return

        # All of name, id, properties are optional. Let's attempt to sanity
        # check # the data by making sure that it has a disk_format and
        # container_format.
        if 'disk_format' not in d or 'container_format' not in d:
            return

        if 'properties' in d:
            props = _get_nova_object_data(
                d['properties'], 'nova', 'ImageMetaProps'
            )
        else:
            props = {}

        return {
            'name': d.get('name'),
            'id': d.get('id'),
            'properties': props,
        }

    except KeyError:
        pass  # decode failed


def _decode_image_metadata_kilo(v):
    # All of name, id, properties are optional. Let's attempt to sanity check
    # the data by making sure that it has a disk_format and container_format.
    if 'disk_format' not in v or 'container_format' not in v:
        return

    return {
        'name': v.get('name'),
        'id': v.get('id'),
        'properties': v.get('properties', {}),
    }


def _decode_image_metadata(v):
    return _decode_nova_object(
        value=v,
        decoders=(
            _decode_image_metadata_mitaka,
            _decode_image_metadata_kilo,
        ),
        error_msg='unable to decode image metadata',
    )


def _image_metadata_to_allowed_modes(image_metadata, logger):
    if image_metadata is None:
        return

    logger.debug('image metadata: %s', image_metadata)
    t, v = config_editor.deserialize(image_metadata, allowed_types=('dict',))
    decoded = _decode_image_metadata(v)

    return glance.allowed_hw_acceleration_modes(
        name=decoded['name'],
        id=decoded['id'],
        image_properties=decoded['properties'],
    )


def _decode_flavor_metadata_kilo(v):
    if 'nova_object.version' not in v:
        return

    try:
        d = _get_nova_object_data(v, 'nova', 'Flavor')

        return {
            'name': d['name'],
            'extra_specs': d['extra_specs'],
        }

    except KeyError:
        pass  # decode failed


def _decode_flavor_metadata(v):
    return _decode_nova_object(
        value=v,
        decoders=(
            _decode_flavor_metadata_kilo,
        ),
        error_msg='unable to decode flavor metadata',
    )


def _flavor_metadata_to_allowed_modes(flavor_metadata, logger):
    if flavor_metadata is None:
        return

    logger.debug('flavor information: %s', flavor_metadata)
    t, v = config_editor.deserialize(
        flavor_metadata, allowed_types=('dict',)
    )
    decoded = _decode_flavor_metadata(v)

    return flavor.allowed_hw_acceleration_modes(
        extra_specs=decoded['extra_specs'], name=decoded['name']
    )


def _assert_flavor_supports_mode(flavor_metadata, mode):
    if flavor_metadata is None:
        return

    if mode == PM.VirtIO:
        t, v = config_editor.deserialize(
            flavor_metadata, allowed_types=('dict',)
        )
        decoded = _decode_flavor_metadata(v)

        flavor.assert_flavor_supports_virtio(
            extra_specs=decoded['extra_specs'], name=decoded['name']
        )


def _check_plug_mode_conflict(uuid, existing_mode, hw_acceleration_mode):
    if hw_acceleration_mode != existing_mode:
        raise plug.PlugModeError(
            'plug mode for port {} is already set to {!r}, cannot '
            'change it to {!r}'.format(
                uuid, existing_mode, hw_acceleration_mode
            )
        )


def _check_pre_configured_plug_mode_for_config(session, conf):
    # Retrieve any existing plug mode and cross check it against the
    # --hw-acceleration-mode command-line parameter.
    pm = one_or_none(
        session.query(port.PlugMode).
        filter(port.PlugMode.neutron_port == conf.neutron_port)
    )

    if pm is None:
        # Initial config, let the code apply its full selection logic.
        pass

    else:
        # Reconfig. Constrain the reconfig to the originally selected mode.
        if conf.hw_acceleration_mode is None:
            # Pretend that the user specified the originally selected mode on
            # the command line.
            conf.hw_acceleration_mode = pm.mode

        else:
            # Check for conflict between the originally selected mode and the
            # mode specified on the command line.
            _check_plug_mode_conflict(
                conf.neutron_port, pm.mode, conf.hw_acceleration_mode
            )


AgentGroup = cfg.OptGroup(
    name='contrail-agent',
    title='Contrail vRouter Agent options',
)

AgentEp = cfg.Opt(
    'api-ep',
    type=types.String(),
    metavar='ENDPOINT',
    default=plug.DEFAULT_AGENT_API_EP,
    help=default_help(
        'HTTP endpoint to plug/unplug ports in Contrail vRouter Agent',
        plug.DEFAULT_AGENT_API_EP
    )
)

AgentPortDir = cfg.Opt(
    'port-dir',
    type=types.String(),
    metavar='DIR',
    default=plug.DEFAULT_AGENT_PORT_DIR,
    help=default_help(
        'Directory with JSON port files used by Contrail vRouter Agent',
        plug.DEFAULT_AGENT_PORT_DIR
    )
)

AGENT_CLI_OPTS = (
    AgentEp,
    AgentPortDir,
)


class ConfigCmd(OsloConfigSubcmd):
    """Edit libvirt configuration to allow plugging accelerated vRouter."""

    NeutronPortOpt = cfg.Opt(
        'neutron-port',
        type=config_opts.Uuid(),
        required=True,
        help='Target Neutron port UUID',
    )

    InputJsonOpt = cfg.Opt(
        'input',
        type=config_opts.Json(),
        help='Original configuration object (from Nova)',
    )

    InputFormatOpt_choices = ('json',)
    InputFormatOpt = cfg.Opt(
        'input-format',
        type=types.String(choices=InputFormatOpt_choices),
        default='json',
        help=choices_help('Input format', InputFormatOpt_choices),
        metavar='FORMAT',
    )

    ImageMetadataOpt = cfg.Opt(
        'image-metadata',
        type=config_opts.Json(),
        help='Glance image information for the instance being launched',
        metavar='META',
    )

    ImageMetadataFormatOpt_choices = InputFormatOpt_choices
    ImageMetadataFormatOpt = cfg.Opt(
        'image-metadata-format',
        type=types.String(choices=ImageMetadataFormatOpt_choices),
        default='json',
        help=choices_help(
            'Glance image information format', ImageMetadataFormatOpt_choices
        ),
        metavar='FORMAT',
    )

    FlavorOpt = cfg.Opt(
        'flavor',
        type=config_opts.Json(),
        help='Flavor information for the instance being launched',
        metavar='META',
    )

    FlavorFormatOpt_choices = InputFormatOpt_choices
    FlavorFormatOpt = cfg.Opt(
        'flavor-format',
        type=types.String(choices=FlavorFormatOpt_choices),
        default='json',
        help=choices_help(
            'Flavor information format', FlavorFormatOpt_choices
        ),
        metavar='FORMAT',
    )

    OutputFormatOpt_choices = ('json', 'xml', 'none')
    OutputFormatOpt = cfg.Opt(
        'output-format',
        type=types.String(choices=OutputFormatOpt_choices),
        default='json',
        help=choices_help('Output format', OutputFormatOpt_choices),
        metavar='FORMAT',
    )

    ConfigVifTypeOpt_choices = ('vrouter', 'vhostuser')
    ConfigVifTypeOpt = cfg.Opt(
        'vif-type',
        type=types.String(choices=ConfigVifTypeOpt_choices),
        help=choices_help('Nova VIF type', ConfigVifTypeOpt_choices),
    )

    ConfigVirtTypeOpt = cfg.Opt(
        'virt-type',
        type=types.String(),
        help='Nova virtualization type (e.g., kvm)',
    )

    def HwAccelerationModeOpt():
        name, kwds = config_opts.hw_acceleration_mode_opt_properties()
        kwds['help'] = (
            '{}. '
            'If this option is specified and the compute node does not '
            'support the requested mode, or if the requested mode is an '
            'accelerated mode and the acceleration system is deinitialized or '
            'encounters an error, the config operation will fail.'
            .format(kwds['help'])
        )
        return cfg.Opt(name, **kwds)

    cli_opts = (
        NeutronPortOpt,
        InputJsonOpt,
        InputFormatOpt,
        ImageMetadataOpt,
        ImageMetadataFormatOpt,
        FlavorOpt,
        FlavorFormatOpt,
        OutputFormatOpt,
        ConfigVifTypeOpt,
        ConfigVirtTypeOpt,
        HwAccelerationModeOpt(),
        config_opts.database_opt,
        config_opts.reservation_timeout_opt,
    )

    def __init__(
        self, _fh=None, _fallback_map_str=None, _root_dir=None, **kwds
    ):
        kwds.setdefault('name', 'config')
        super(ConfigCmd, self).__init__(**kwds)

        # Testing knobs.
        self._fh = sys.stdout if _fh is None else _fh
        self._fallback_map_str = _fallback_map_str
        self._root_dir = _root_dir

    def create_conf(self):
        conf = super(ConfigCmd, self).create_conf()

        conf.register_group(AgentGroup)
        conf.register_cli_opt(AgentPortDir, group=AgentGroup)

        return conf

    def run(self):
        """
        Binds the port with UUID `conf.neutron_port_uuid` to a plug mode and a
        VF.

        Edits `nova_interface_object` so that when Nova serializes it to XML,
        it produces the correct tree to make libvirt plug the port in the mode
        that we want.

        The port needn't actually exist yet.
        """

        assert self.conf is not None, 'PRE: parse_args()'
        conf = self.conf

        # Deserialize the interface object.
        nova_interface_object = LibvirtConfigGuestInterface()
        if conf.input is not None:
            t, v = config_editor.deserialize(
                conf.input, allowed_types=('LibvirtConfigGuestInterface',)
            )
            config_editor._apply_changes(nova_interface_object, v)
        else:
            # minimal object setup needed for testing without Nova input
            nova_interface_object.mac_addr = '00:00:00:00:00:00'

        old_editor_vars = (
            config_editor.serialize(nova_interface_object)['vars']
        )

        # Calculate the hardware acceleration modes allowed by the image
        # metadata.
        glance_allowed_modes = _image_metadata_to_allowed_modes(
            conf.image_metadata, self.logger
        )

        # Calculate the hardware acceleration modes allowed by the flavor.
        flavor_allowed_modes = _flavor_metadata_to_allowed_modes(
            conf.flavor, self.logger
        )

        # Read the fallback map.
        fallback_map = fallback.read_sysfs_fallback_map(
            # Testing knob.
            _in=self._fallback_map_str
        )
        vf_pool = _make_vf_pool(
            fallback_map,
            port_dir=getattr(conf, 'contrail-agent').port_dir,
            grace_period=_VRT_604_GRACE_PERIOD,
        )

        # Perform the configuration.
        Session = _init_Session(conf, self.logger)
        if Session is None:
            return 1

        s = Session()

        # Set up the desired behavior for reconfigs.
        _check_pre_configured_plug_mode_for_config(s, conf)

        # Figure out what acceleration mode to use.
        modes = config.calculate_acceleration_modes_for_port(
            conf.neutron_port, conf.vif_type, conf.virt_type,
            glance_allowed_modes, flavor_allowed_modes,
            conf.hw_acceleration_mode
        )
        modes = config.apply_compute_node_restrictions(
            conf.neutron_port, modes, conf.hw_acceleration_modes,
            _root_dir=self._root_dir
        )

        m = modes[0]
        _assert_flavor_supports_mode(conf.flavor, m)
        try:
            config.set_acceleration_mode_for_port(
                s, conf.neutron_port, m, vf_pool, conf.reservation_timeout
            )

        except vf.AllocationError as e:
            if PM.unaccelerated not in modes:
                raise

            # Retry in unaccelerated mode (VRT-720).
            self.logger.warning(
                'configuring port %s in unaccelerated mode', conf.neutron_port
            )
            m = PM.unaccelerated
            config.set_acceleration_mode_for_port(
                s, conf.neutron_port, m, vf_pool, conf.reservation_timeout
            )

        # Edit the configuration to match.
        config.set_config_for_port(
            s, nova_interface_object, conf.neutron_port, fallback_map
        )

        # Prepare to send changes back to the caller.
        new_editor_vars = (
            config_editor.serialize(nova_interface_object)['vars']
        )

        editor_vars_diff = config_editor.create_delta(
            old_editor_vars, new_editor_vars
        )

        try:
            dom = nova_interface_object.format_dom()
        except TypeError as e:
            self.logger.error('%s', exception_msg(e, 'format_dom()'))
            self.logger.critical(
                'editing operation resulted in incomplete %s',
                type(nova_interface_object).__name__
            )
            return 1

        # Send changes back to the caller.
        if conf.output_format == 'json':
            print >>self._fh, json.dumps(editor_vars_diff, indent=4)
        elif conf.output_format == 'xml':
            print >>self._fh, etree.tostring(dom, pretty_print=True)
        elif conf.output_format == 'none':
            pass
        else:
            assert False, 'unknown output format {}'.format(conf.output_format)

        # Record changes in the database.
        s.commit()

        return 0


NoPersistOpt = cfg.Opt(
    'no_persist',
    type=types.Boolean(),
    help="Don't store port information in files",
    default=False,
)

VirtioRelayGroup = cfg.OptGroup(
    name='virtio-relay',
    title='Netronome VirtIO Relay options',
)

VirtioRelayZmqEpOpt = cfg.Opt(
    'zmq-ep',
    type=types.String(),
    metavar='ENDPOINT',
    default=plug.DEFAULT_VIRTIO_ZMQ_EP,
    help=default_help(
        'ZeroMQ endpoint to plug/unplug ports in Netronome VirtIO Relay',
        plug.DEFAULT_VIRTIO_ZMQ_EP
    )
)

VirtioRelayRcvtimeoOpt_default = config_opts.format_timedelta(
    timedelta(milliseconds=plug.DEFAULT_VIRTIO_RCVTIMEO_MS)
)
VirtioRelayRcvtimeoOpt = cfg.Opt(
    'zmq-receive-timeout',
    type=config_opts.TimeDelta(),
    metavar='TIMEOUT',
    default=VirtioRelayRcvtimeoOpt_default,
    help=default_help(
        'ZeroMQ receive timeout for RPC to Netronome VirtIO Relay',
        VirtioRelayRcvtimeoOpt_default
    )
)

VirtioRelayDriverOpt = cfg.Opt(
    'driver',
    type=types.String(),
    metavar='DRIVER',
    default=plug.DEFAULT_VIRTIO_DRIVER,
    help=default_help(
        'Driver to bind Netronome VFs when plugged',
        plug.DEFAULT_VIRTIO_DRIVER
    ),
)

VirtioRelayStubDriverOpt = cfg.Opt(
    'stub-driver',
    type=types.String(),
    metavar='DRIVER',
    default=plug.DEFAULT_VIRTIO_STUB_DRIVER,
    help=default_help(
        'Driver to bind Netronome VFs when unplugged',
        plug.DEFAULT_VIRTIO_STUB_DRIVER
    )
)

VIRTIO_CLI_OPTS = (
    VirtioRelayZmqEpOpt,
)

VIRTIO_OPTS = (
    VirtioRelayRcvtimeoOpt,
    VirtioRelayDriverOpt,
    VirtioRelayStubDriverOpt,
)

PortUuidOpt = cfg.Opt(
    'uuid',
    type=config_opts.Uuid(),
    required=True,
    help='Target Neutron port UUID',
)


class IPAddressOrNone(types.IPAddress):
    """Contrail uses the literal value "None" to represent "no address."
    """
    def __call__(self, value):
        if str(value) == 'None':
            return None
        else:
            return super(IPAddressOrNone, self).__call__(value)


def _check_pre_configured_plug_mode_for_add_pm_None(session, conf, logger):
    if conf.hw_acceleration_mode is not None:
        # Create a plug mode record for hw_acceleration_mode.
        pm = port.PlugMode(
            neutron_port=conf.uuid, mode=conf.hw_acceleration_mode
        )
        session.add(pm)
        return

    # No existing plug mode, and no hw_acceleration_mode. This can indicate
    # that we are running with a non-hardware acceleration aware version of
    # Nova.

    if PM.unaccelerated not in conf.hw_acceleration_modes:
        # Compute node is configured for accelerated operation only.
        # Don't configure any plug mode. plug_port() will raise PlugModeError.
        return

    if set(conf.hw_acceleration_modes) & set(PM.accelerated_plug_modes):
        # Compute node is configured to allow, but not require, accelerated
        # operation. Log a warning on the theory that we might be running with
        # a non-hardware acceleration aware version of Nova.
        logger.warning(
            'disabling hardware acceleration for port %s because the port '
            'was not configured before plugging', conf.uuid
        )

    else:
        # Compute node is configured for only unaccelerated operation. Don't
        # log any message.
        pass

    # Create a plug mode record for unaccelerated operation.
    pm = port.PlugMode(neutron_port=conf.uuid, mode=PM.unaccelerated)
    session.add(pm)


def _check_pre_configured_plug_mode_for_add_pm(session, conf, pm):
    if conf.hw_acceleration_mode is None:
        # plug_port() will use the pre-configured plug mode.
        return

    # Check for conflict between the originally selected mode and the mode
    # specified on the command line.
    _check_plug_mode_conflict(conf.uuid, pm.mode, conf.hw_acceleration_mode)


def _check_pre_configured_plug_mode_for_add(session, conf, logger):
    # Retrieve any existing plug mode and cross check it against the
    # --hw-acceleration-mode command-line parameter.
    pm = one_or_none(
        session.query(port.PlugMode).
        filter(port.PlugMode.neutron_port == conf.uuid)
    )

    if pm is None:
        _check_pre_configured_plug_mode_for_add_pm_None(session, conf, logger)
    else:
        _check_pre_configured_plug_mode_for_add_pm(session, conf, pm)


class AddCmd(OsloConfigSubcmd):
    """Add a port to the vRouter."""

    InstanceUuidOpt = cfg.Opt(
        'instance_uuid',
        type=config_opts.Uuid(),
        required=True,
        help='Instance UUID',
    )

    VnUuidOpt = cfg.Opt(
        'vn_uuid',
        type=config_opts.Uuid(),
        required=True,
        help='Virtual network UUID',
    )

    VmProjectUuidOpt = cfg.Opt(
        'vm_project_uuid',
        type=config_opts.Uuid(),
        required=True,
        help='UUID of project containing this instance',
    )

    IpAddressOpt = cfg.Opt(
        'ip_address',
        type=IPAddressOrNone(version=4),
        required=False,
        help='IP address of this Neutron port',
    )

    Ipv6AddressOpt = cfg.Opt(
        'ipv6_address',
        type=IPAddressOrNone(version=6),
        required=False,
        help='IPv6 address of this Neutron port',
    )

    VmNameOpt = cfg.Opt(
        'vm_name',
        type=types.String(),
        required=False,
        help='Human-readable instance name',
    )

    MacOpt = cfg.Opt(
        'mac',
        type=config_opts.MacAddress(),
        required=True,
        help='MAC address of this Neutron port',
    )

    TapNameOpt = cfg.Opt(
        'tap_name',
        type=types.String(),
        required=True,
        help='System name of TAP device for this Neutron port',
    )

    MultiqueueOpt = cfg.Opt(
        'multiqueue',
        type=types.Boolean(),
        default=False,
        help=default_help('Configure TAP devices in multiqueue mode', False)
    )

    PortTypeOpt_choices = ('NovaVMPort', 'NameSpacePort')
    PortTypeOpt = cfg.Opt(
        'port_type',
        type=types.String(choices=PortTypeOpt_choices),
        help=choices_help('Contrail port type', PortTypeOpt_choices),
        required=True,
    )

    TxVlanIdOpt = cfg.Opt(
        'tx_vlan_id',
        type=types.Integer(),
        help='Transmit VLAN ID for this Neutron port',
        default=-1,
    )

    RxVlanIdOpt = cfg.Opt(
        'rx_vlan_id',
        type=types.Integer(),
        help='Receive VLAN ID for this Neutron port',
        default=-1,
    )

    AddVifTypeOpt_choices = ('Vrouter', 'VhostUser')
    AddVifTypeOpt = cfg.Opt(
        'vif_type',
        type=types.String(choices=AddVifTypeOpt_choices),
        help=choices_help('Contrail VIF type', AddVifTypeOpt_choices),
        required=True,
    )

    VhostUserSocketOpt = cfg.Opt(
        'vhostuser_socket',
        type=types.String(),
        help=(
            'Path to socket file for ports with vif_type "VhostUser" '
            '(i.e., DPDK vRouter ports)'
        ),
    )

    VhostUserSocketTimeoutOpt_default = config_opts.format_timedelta(
        timedelta(milliseconds=plug.DEFAULT_DPDK_SOCKET_TIMEOUT_MS)
    )
    VhostUserSocketTimeoutOpt = cfg.Opt(
        'vhostuser_socket_timeout',
        type=config_opts.TimeDelta(),
        default=VhostUserSocketTimeoutOpt_default,
        help=default_help(
            'Time to wait for vhostuser_socket to appear in DPDK mode',
            VhostUserSocketTimeoutOpt_default
        ),
    )

    def HwAccelerationModeOpt():
        name, kwds = config_opts.hw_acceleration_mode_opt_properties()
        kwds['help'] = (
            '{}. '
            'If this option is not specified, then the port must have a '
            'hardware acceleration mode that was set by a prior call to the '
            '"config" subcommand.'.format(kwds['help'])
        )
        return cfg.Opt(name, **kwds)

    cli_opts = (
        PortUuidOpt,
        InstanceUuidOpt,
        VnUuidOpt,
        VmProjectUuidOpt,
        IpAddressOpt,
        Ipv6AddressOpt,
        VmNameOpt,
        MacOpt,
        TapNameOpt,
        MultiqueueOpt,
        PortTypeOpt,
        TxVlanIdOpt,
        RxVlanIdOpt,
        NoPersistOpt,
        AddVifTypeOpt,
        VhostUserSocketOpt,
        VhostUserSocketTimeoutOpt,
        HwAccelerationModeOpt(),
        config_opts.database_opt,
    )

    def __init__(self, _fallback_map_str=None, _root_dir=None, **kwds):
        kwds.setdefault('name', 'add')
        super(AddCmd, self).__init__(**kwds)

        # Testing knobs.
        self._fallback_map_str = _fallback_map_str
        self._root_dir = _root_dir

    def create_conf(self):
        conf = super(AddCmd, self).create_conf()

        conf.register_group(AgentGroup)
        conf.register_cli_opts(AGENT_CLI_OPTS, group=AgentGroup)

        conf.register_group(VirtioRelayGroup)
        conf.register_cli_opts(VIRTIO_CLI_OPTS, group=VirtioRelayGroup)
        # FIXME: should not be CLI
        conf.register_cli_opts(VIRTIO_OPTS, group=VirtioRelayGroup)

        return conf

    def run(self):
        assert self.conf is not None, 'PRE: parse_args()'
        conf = self.conf

        # Read the fallback map.
        fallback_map = fallback.read_sysfs_fallback_map(
            # Testing knob.
            _in=self._fallback_map_str
        )
        vf_pool = _make_vf_pool(
            fallback_map,
            port_dir=getattr(conf, 'contrail-agent').port_dir,
            grace_period=_VRT_604_GRACE_PERIOD,
        )

        Session = _init_Session(conf, self.logger)
        if Session is None:
            return 1

        s = Session()

        _check_pre_configured_plug_mode_for_add(s, conf, self.logger)

        plug.plug_port(s, vf_pool, conf, _root_dir=self._root_dir)
        s.commit()

        return 0


class DeleteCmd(OsloConfigSubcmd):
    """Delete a port from the vRouter."""

    cli_opts = (
        PortUuidOpt,
        NoPersistOpt,
        config_opts.database_opt,
    )

    def __init__(self, **kwds):
        kwds.setdefault('name', 'delete')
        super(DeleteCmd, self).__init__(**kwds)

    def create_conf(self):
        conf = super(DeleteCmd, self).create_conf()

        conf.register_group(AgentGroup)
        conf.register_cli_opts(AGENT_CLI_OPTS, group=AgentGroup)

        conf.register_group(VirtioRelayGroup)
        conf.register_cli_opts(VIRTIO_CLI_OPTS, group=VirtioRelayGroup)
        # FIXME: should not be CLI
        conf.register_cli_opts(VIRTIO_OPTS, group=VirtioRelayGroup)

        return conf

    def run(self):
        assert self.conf is not None, 'PRE: parse_args()'
        conf = self.conf

        fallback_map = fallback.read_sysfs_fallback_map()
        vf_pool = _make_vf_pool(
            fallback_map,
            port_dir=getattr(conf, 'contrail-agent').port_dir,
            grace_period=_VRT_604_GRACE_PERIOD,
        )

        Session = _init_Session(conf, self.logger)
        if Session is None:
            return 1

        s = Session()
        plug.unplug_port(s, vf_pool, conf)
        s.commit()

        return 0


class IommuCheckCmd(OsloConfigSubcmd):
    """Checks IOMMU settings for the current plug mode."""

    def HwAccelerationModeOpt():
        name, kwds = config_opts.hw_acceleration_mode_opt_properties()
        kwds['help'] = (
            '{}. '
            'If this option is not specified, the code will perform an IOMMU '
            'check for the most restrictive mode listed in '
            'vrouter-port-control.conf.'.format(kwds['help'])
        )
        return cfg.Opt(name, **kwds)

    cli_opts = (
        HwAccelerationModeOpt(),
    )

    def __init__(self, **kwds):
        kwds.setdefault('name', 'iommu_check')
        super(IommuCheckCmd, self).__init__(**kwds)

    def run(self):
        assert self.conf is not None, 'PRE: parse_args()'
        conf = self.conf

        # Re-enable sys.stderr logging if the config file turned it off.
        #
        # TODO(wbrinzer): Add command-line knobs so that the caller (e.g., the
        # OpenStack integration installer) can drive this.
        h = logging.StreamHandler()
        h.setFormatter(logging.Formatter(logging.BASIC_FORMAT, None))
        logging.root.addHandler(h)
        logging.root.setLevel(logging.DEBUG)

        from netronome import iommu_check

        # Figure out what hardware acceleration mode we should use to perform
        # the check.
        if conf.hw_acceleration_mode is not None:
            mode = conf.hw_acceleration_mode
        else:
            mode = conf.hw_acceleration_modes[0]  # most restrictive

        self.logger.debug('checking IOMMU for mode: %s', mode)

        if mode == PM.SRIOV:
            iommu_check.sriov_iommu_check()
        elif mode == PM.VirtIO:
            iommu_check.virtio_iommu_check()
        elif mode == PM.unaccelerated:
            pass
        else:
            assert 0, 'unknown hardware acceleration mode "{}"'.format(mode)

        return 0


class LoggingFeature(object):
    def create_conf(self, o, conf):
        # Register config_opts command-line logging options.
        conf.register_cli_opts(
            config_opts.cli_logging_opts, group=config_opts.cli_logging_group
        )

    def run(self, o):
        # Configure logging according to command-line options.
        config_opts.apply_logging_conf(o.conf)


class vRouterPortControl(subcmd.SubcmdApp):
    def __init__(self, **kwds):
        super(vRouterPortControl, self).__init__(**kwds)

        f = (LoggingFeature(),)

        self.cmds = (
            ConfigCmd(logger=self.logger, features=f),
            AddCmd(logger=self.logger, features=f),
            DeleteCmd(logger=self.logger, features=f),
            IommuCheckCmd(logger=self.logger, features=f),
        )
