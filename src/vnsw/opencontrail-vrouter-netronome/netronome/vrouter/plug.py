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

import copy
import errno
import inspect
import json
import logging
import os
import re
import requests
import six
import stat
import sys
import time
import urlparse

try:
    import zmq
    import netronome.virtiorelayd.virtiorelayd_pb2 as relay
except ImportError:
    relay = None

try:
    from netronome import iommu_check
except ImportError:
    iommu_check = None

from netronome.vrouter import rest as vrouter_rest
from netronome.vrouter import (
    config, config_opts, fallback, pci, plug_modes as PM, port, subprocess, vf
)
from netronome.vrouter.port import VIF_TYPE_VHOSTUSER
from netronome.vrouter.sa.helpers import one_or_none

import nova.utils as nova_utils
import oslo_concurrency.processutils as oslo_processutils

from datetime import timedelta
from nova.network import linux_net

logger = logging.getLogger(__name__)


def _exception_msg(e, *args):
    """Create log message for exception during plug/unplug/unwind."""

    msg = list(args)
    msg.append(type(e).__name__)
    e_str = str(e)
    if e_str:
        msg.append(e_str)
    return ': '.join(msg)


class _StepStatus(object):
    def __init__(self, severity, name, **kwds):
        if not isinstance(severity, int):
            raise ValueError('severity must be an int')
        elif severity == 0:
            raise ValueError('severity must be positive or negative')

        self.severity = severity

        if not re.match(r'^[A-Z_][A-Z0-9_]*\Z', name):
            raise ValueError('invalid name "{}"'.format(name))

        self.name = name

    def __repr__(self):
        return '<{}.{}>'.format(type(self).__name__, self.name)

    def __str__(self):
        return self.name

    def __cmp__(self, other):
        if not isinstance(other, _StepStatus):
            return NotImplemented
        return cmp(self.severity, other.severity)

    def is_positive(self):
        return self.severity >= 1

    def is_negative(self):
        return not self.is_positive()

    @classmethod
    def _register(cls, severity, name):
        setattr(cls, name, cls(severity, name))

_StepStatus._register(+2, 'SKIP')
_StepStatus._register(+1, 'OK')
_StepStatus._register(-1, 'ERROR')
_StepStatus._register(-2, 'EXCEPTION')
_StepStatus._register(-3, 'EXCEPTION_DURING_CLEANUP')


def _format_getdoc(s):
    if s is None:
        return None
    return re.sub(r'\s+', ' ', s.rstrip('.'), re.S).strip()


def config_section(section):
    def _fn(fn):
        fn.section = section
        return fn
    return _fn


class _Action(object):
    """A forward or reverse action performed by a plug driver step."""
    def __init__(self, fn, description, enabled=True, **kwds):
        super(_Action, self).__init__(**kwds)

        self.fn = fn
        self.description = description
        self.enabled = enabled

    def execute(self):
        """
        Perform the action.

        Raise an exception, or return a negative _StepStatus, on error.
        Negative _StepStatus indicates a warning, e.g., connect error in agent
        POST.
        """

        return self.fn()

# An action that represents a no-op.
_NullAction = _Action(lambda: _StepStatus.OK, description=None)

# An action that represents a skip.
_SkipAction = _Action(lambda: _StepStatus.SKIP, description=None)


class _Step(object):
    """A plug driver step."""

    def __init__(self, description=None, **kwds):
        super(_Step, self).__init__(**kwds)
        self.description = description

    @staticmethod
    def translate_conf(conf):
        """
        Translate config_opts configuration to options recognized by this step.
        """
        return {}

    def forward_action(self, session, port, journal):
        """
        Return an object that can perform the step.
        """

        raise NotImplementedError('_Step.forward_action')

    def reverse_action(self, session, port, journal):
        """
        Return an object that can perform the opposite of the step.
        """

        raise NotImplementedError('_Step.reverse_action')


class _RootDirKnob(object):
    """Mixin for root_dir testing knob."""

    def __init__(self, **kwds):
        super(_RootDirKnob, self).__init__(**kwds)
        self.root_dir = '/'

    def set_root_dir(self, root_dir):
        self.root_dir = root_dir


def apply_root_dir(o, root_dir):
    if hasattr(o, 'set_root_dir'):
        o.set_root_dir(root_dir)


class _CreateTAP(_Step):
    @config_section('linux_net')
    def configure(self, dry_run=False, multiqueue=False):
        self._dry_run = dry_run
        self._multiqueue = multiqueue

    @staticmethod
    def translate_conf(conf):
        ans = _Step.translate_conf(conf)

        section = ans.setdefault('linux_net', {})

        g = getattr(conf, 'iproute2', None)
        if g is not None and g.dry_run:
            section['dry_run'] = True
        if getattr(conf, 'multiqueue', False):
            section['multiqueue'] = True

        return ans

    def forward_action(self, session, port, journal):
        if port.tap_name is None:
            # VRT-604 garbage collection
            return _NullAction

        if self._multiqueue:
            fmt = 'Create multiqueue TAP interface: {}'
        else:
            fmt = 'Create standard TAP interface: {}'

        return _Action(
            lambda: self._do(session, port, journal), fmt.format(port.tap_name)
        )

    def reverse_action(self, session, port, journal):
        if port.tap_name is None:
            # VRT-604 garbage collection
            return _NullAction

        return _Action(
            lambda: self._undo(session, port, journal),
            'Delete TAP interface: {}'.format(port.tap_name)
        )

    def _do(self, session, port, journal):
        kwds = {'multiqueue': True} if self._multiqueue else {}
        if not self._dry_run:
            linux_net.create_tap_dev(port.tap_name, **kwds)

        return _StepStatus.OK

    def _undo(self, session, port, journal):
        if not self._dry_run:
            linux_net.delete_net_dev(port.tap_name)

        return _StepStatus.OK


def makedirs(d):
    try:
        os.makedirs(d)

    except (OSError, IOError) as e:
        # vrouter-port-control checks isdir() here. I'd prefer to let the
        # OS check that later, when we attempt to create the file.
        if e.errno != errno.EEXIST:
            raise


DEFAULT_AGENT_PORT_DIR = '/var/lib/contrail/ports'


class _AgentFileWrite(_Step):
    def __init__(self, get_vif_devname=lambda p: None, **kwds):
        super(_AgentFileWrite, self).__init__(**kwds)
        self.directory = None

        self.get_vif_devname = get_vif_devname

    @config_section('vrouter-port-control')
    def configure(self, directory=DEFAULT_AGENT_PORT_DIR):
        self.directory = directory

    @staticmethod
    def translate_conf(conf):
        ans = _Step.translate_conf(conf)

        g = getattr(conf, 'contrail-agent')
        port_dir = g.port_dir if not conf.no_persist else None
        ans.update({'vrouter-port-control': {'directory': port_dir}})

        return ans

    def forward_action(self, session, port, journal):
        return _Action(
            lambda: self._do(session, port, journal),
            'Write JSON file to vRouter port database',
            enabled=self.directory is not None,
        )

    def reverse_action(self, session, port, journal):
        return _Action(
            lambda: self._undo(session, port, journal),
            'Delete JSON file from vRouter port database',
            enabled=self.directory is not None,
        )

    def _do(self, session, port, journal):
        makedirs(self.directory)

        # POSSIBLE BUG: non-atomic write
        fname = os.path.join(self.directory, str(port.uuid))

        catch = True
        try:
            with open(fname, 'w') as fh:
                catch = False
                body = json.dumps(
                    port.dump(tap_name=self.get_vif_devname(port)), indent=4
                )
                print >>fh, body

        except Exception as e:
            if not catch:
                raise
            logger.error(
                '%s', _exception_msg(e, 'Cannot write port information')
            )
            return _StepStatus.ERROR

        return _StepStatus.OK

    def _undo(self, session, port, journal):
        fname = os.path.join(self.directory, str(port.uuid))
        try:
            os.unlink(fname)

        except (OSError, IOError) as e:
            # we want the file gone, so if it's already gone, that's fine.
            if e.errno != errno.ENOENT:
                raise

            # There is nothing wrong, so we should not log anything here.

        return _StepStatus.OK


def _should_log_response_body(content_type):
    return (
        content_type == 'application/json'
        or content_type.split('/')[0] == 'text'
    )


def _log_error_response(method, e, headers, data, log_content_type=True):
    logger.error('%s response: %s', method, _exception_msg(e))
    content_type = vrouter_rest.parse_content_type(headers)[0]

    if log_content_type:
        logger.info(
            '%s response: Content-Type: %s',
            method, headers.get('content-type')
        )

    if _should_log_response_body(content_type):
        for line in data.split('\n'):
            logger.info('%s response: %s', method, line)


DEFAULT_AGENT_API_EP = 'http://localhost:9091'


class _AgentPost(_Step):
    def __init__(self, get_vif_devname=lambda p: None, vif_sync=None, **kwds):

        super(_AgentPost, self).__init__(**kwds)
        self.base_url = None

        self.get_vif_devname = get_vif_devname
        self.vif_sync = vif_sync

    @config_section('contrail-vrouter-agent')
    def configure(self, base_url=DEFAULT_AGENT_API_EP, vif_sync=None):
        self.base_url = base_url

        vif_sync_params = vif_sync
        if self.vif_sync is not None:
            c = {} if vif_sync_params is None else vif_sync_params
            self.vif_sync.configure(**c)
        elif vif_sync_params is not None:
            raise ValueError(
                'cannot have vif_sync_params when vif_sync is None'
            )

    def set_root_dir(self, root_dir):
        if self.vif_sync is not None:
            apply_root_dir(self.vif_sync, root_dir)

    @staticmethod
    def translate_conf(conf):
        ans = _Step.translate_conf(conf)

        g = getattr(conf, 'contrail-agent')
        ans.update({'contrail-vrouter-agent': {'base_url': g.api_ep}})

        # The following don't correspond to real Oslo options, but they are
        # necessary for the unit tests.
        vif_sync = getattr(g, 'vif-sync-options', None)
        if vif_sync is not None:
            if 'vif_sync' not in ans['contrail-vrouter-agent']:
                ans['contrail-vrouter-agent']['vif_sync'] = {}

            ans['contrail-vrouter-agent']['vif_sync'].update(vif_sync)

        return ans

    def forward_action(self, session, port, journal):
        tap_name = port.actual_name(self.get_vif_devname(port))
        return _Action(
            lambda: self._do(session, port, journal, tap_name),
            'Register interface with vRouter agent (via HTTP POST): {}'
            .format(tap_name),
        )

    def reverse_action(self, session, port, journal):
        return _Action(
            lambda: self._undo(session, port, journal),
            'Unregister port from vRouter agent (via HTTP DELETE)'.format(
                port.uuid
            ),
        )

    def _do(self, session, port, journal, tap_name):
        ep = urlparse.urljoin(self.base_url, 'port')
        body = json.dumps(port.dump(tap_name=tap_name))

        try:
            r = requests.post(
                ep, data=body,
                headers={'Content-Type': 'application/json'},
            )

        except requests.exceptions.RequestException as e:
            logger.error('POST error: %s', _exception_msg(e))
            return _StepStatus.ERROR

        try:
            log_content_type = True
            tc = vrouter_rest.HTTPSuccessTc()
            tc.assert_valid(r.status_code)

            log_content_type = False
            tc = vrouter_rest.ContentTypeTc('application/json')
            tc.assert_valid(r.headers)

        except ValueError as e:
            _log_error_response(
                'POST', e, r.headers, r.content,
                log_content_type=log_content_type
            )

            # vrouter-port-control also masks this error. Since it represents
            # an exceptional condition with the agent, rather than simply
            # "agent not running," we choose to report it.
            raise

        if self.vif_sync is None:
            return _StepStatus.OK
        else:
            return self.vif_sync.sync_plug(tap_name)

    def _undo(self, session, port, journal):
        ep = urlparse.urljoin(self.base_url, 'port/{}'.format(port.uuid))

        try:
            r = requests.delete(ep)

        except requests.exceptions.RequestException as e:
            logger.error('DELETE error: %s', _exception_msg(e))
            return _StepStatus.ERROR

        try:
            tc = vrouter_rest.HTTPSuccessTc()
            tc.assert_valid(r.status_code)

        except ValueError as e:
            if port.tap_name is None and r.status_code == 404:
                # VRT-604 garbage collection
                pass

            else:
                # Report HTTP DELETE errors, but downgrade 404 to a warning
                # (analogous with ENOENT handling in _AgentFileWrite). The
                # original vrouter-port-control script from Contrail masked all
                # HTTP DELETE errors.

                _log_error_response('DELETE', e, r.headers, r.content)

                if r.status_code == 404:
                    return _StepStatus.ERROR
                else:
                    raise

        if self.vif_sync is None:
            return _StepStatus.OK
        else:
            tap_name = self.get_vif_devname(port)
            return self.vif_sync.sync_unplug(tap_name)


def _interface_plug_fpath(fname, _root_dir):
    return os.path.join(_root_dir, 'sys/module/nfp_vrouter/control', fname)


def _spin_on_interface_plug_file(sysfs_fpath, condition, timeout_ms):
    """
    Wait for `condition` to become true on the interface plug file `fname`.

    Return the number of milliseconds waited if `condition` became true, or
    None if the timeout expires. Note that if the function does not sleep (0ms
    wait time), the return value can be false.
    """

    def read():
        with open(sysfs_fpath, 'r') as fh:
            return frozenset(map(lambda s: s.strip(), fh.readlines()))

    slept_ms = 0
    while slept_ms < timeout_ms:
        s = read()
        if condition(s):
            return slept_ms

        time.sleep(0.050)
        slept_ms += 50

        # FIXME(wbrinzer): Extra sleep at the end for no reason.

    # Timeout.
    return None


class InterfacePlugTimeoutError(Exception):
    """
    Timeout waiting for interface to appear in the firmware VIF table.
    """
    pass


_DEFAULT_INTERFACE_PLUG_FNAME = 'virtual_vifs_plugged'
_DEFAULT_INTERFACE_PLUG_TIMEOUT_MS = 15000


class _WaitForFirmwareVIFTable(_RootDirKnob):
    """Wait for the interface to appear in the firmware VIF table."""

    def __init__(self, **kwds):
        super(_WaitForFirmwareVIFTable, self).__init__(**kwds)

    # No @config_section. This is not a _Step, so configure() is called
    # manually.
    def configure(self, timeout_ms=_DEFAULT_INTERFACE_PLUG_TIMEOUT_MS):
        self._timeout_ms = timeout_ms

    def interface_plug_fpath(self):
        return _interface_plug_fpath(
            _DEFAULT_INTERFACE_PLUG_FNAME, _root_dir=self.root_dir
        )

    def sync_plug(self, tap_name):
        plug_fpath = self.interface_plug_fpath()

        timeout_str = config_opts.format_timedelta(
            timedelta(milliseconds=self._timeout_ms)
        )
        logger.debug(
            'Waiting up to %s for interface %s to be added to the firmware '
            'VIF table', timeout_str, tap_name
        )

        t = _spin_on_interface_plug_file(
            sysfs_fpath=plug_fpath,
            condition=lambda s: tap_name in s,
            timeout_ms=self._timeout_ms
        )

        if t is None:
            raise InterfacePlugTimeoutError(
                'Interface {} did not appear in {} within {}'.format(
                    tap_name, plug_fpath, timeout_str
                )
            )

        else:
            wait_time_str = config_opts.format_timedelta(
                timedelta(milliseconds=t)
            )

            logger.debug(
                'Interface %s has been added to the firmware VIF table '
                '(wait time: %s)', tap_name, wait_time_str
            )

            return _StepStatus.OK

    def sync_unplug(self, tap_name):
        plug_fpath = self.interface_plug_fpath()

        timeout_str = config_opts.format_timedelta(
            timedelta(milliseconds=self._timeout_ms)
        )
        logger.debug(
            'Waiting up to %s for interface %s to be removed from the '
            'firmware VIF table', timeout_str, tap_name
        )

        t = _spin_on_interface_plug_file(
            sysfs_fpath=plug_fpath,
            condition=lambda s: tap_name not in s,
            timeout_ms=self._timeout_ms
        )

        if t is None:
            logger.error(
                'Interface %s did not disappear from %s within %s',
                tap_name, plug_fpath, timeout_str
            )
            return _StepStatus.ERROR

        else:
            wait_time_str = config_opts.format_timedelta(
                timedelta(milliseconds=t)
            )
            logger.debug(
                'Interface %s has been removed from the firmware VIF table '
                '(wait time: %s)', tap_name, wait_time_str
            )
            return _StepStatus.OK


class CompleteAgentFailure(Exception):
    """
    No agent-related operations succeeded during the Contrail vRouter port plug
    operation.
    """
    pass


class _AgentRaiseOnCompleteFailure(_Step):
    """Check that at least one agent-related operation succeeded."""

    def forward_action(self, session, port, journal):
        return _Action(
            lambda: self._do(session, port, journal),
            description=None,  # don't log this step
        )

    def reverse_action(self, session, port, journal):
        # Nothing to undo.
        return _NullAction

    def _do(self, session, port, journal):
        if (
            journal[_AgentPost] != _StepStatus.OK
            and journal[_AgentFileWrite] != _StepStatus.OK
        ):
            error_str = _format_getdoc(inspect.getdoc(CompleteAgentFailure))
            raise CompleteAgentFailure(error_str)

        return _StepStatus.OK


class SocketWaitError(Exception):
    """
    Timeout during DPDK/VirtIO socket wait, or an object appeared at the socket
    path but it was not a Unix domain socket.
    """
    pass


DEFAULT_DPDK_SOCKET_TIMEOUT_MS = 15 * 1000


class _DPDKSocketWait(_Step):
    def __init__(self, **kwds):
        super(_DPDKSocketWait, self).__init__(**kwds)
        self.socket_timeout_ms = None

    @config_section('DPDK')
    def configure(self, socket_timeout_ms=DEFAULT_DPDK_SOCKET_TIMEOUT_MS):
        self.socket_timeout_ms = int(socket_timeout_ms)

    @staticmethod
    def translate_conf(conf):
        ans = _Step.translate_conf(conf)
        if hasattr(conf, 'vhostuser_socket_timeout'):
            ans.update({
                'DPDK': {
                    'socket_timeout_ms': int(round(
                        conf.vhostuser_socket_timeout.total_seconds() * 1000
                    )),
                },
            })
        return ans

    def forward_action(self, session, port, journal):
        if port.vif_type != VIF_TYPE_VHOSTUSER:
            # Not in DPDK mode.
            return _NullAction

        elif journal[_AgentPost] != _StepStatus.OK:
            # Agent POST failed, should not wait for DPDK socket.
            return _SkipAction

        else:
            timeout_str = config_opts.format_timedelta(
                timedelta(milliseconds=self.socket_timeout_ms)
            )
            return _Action(
                lambda: self._do(session, port, journal),
                'Waiting up to {} for vRouter agent to create vhostuser socket'
                .format(timeout_str)
            )

    def reverse_action(self, session, port, journal):
        # Nothing to undo.
        return _NullAction

    def _do(self, session, port, journal):
        st = None
        slept_ms = 0

        while st is None and slept_ms < self.socket_timeout_ms:
            try:
                st = os.stat(port.vhostuser_socket)

            except (OSError, IOError) as e:
                if e.errno != errno.ENOENT:
                    raise

                time.sleep(0.050)
                slept_ms += 50

                # FIXME(wbrinzer): Extra sleep at the end for no reason.

        if st is None:
            raise SocketWaitError(
                '{} did not appear within {}'.format(
                    port.vhostuser_socket,
                    config_opts.format_timedelta(
                        timedelta(milliseconds=self.socket_timeout_ms)
                    )
                )
            )

        if not stat.S_ISSOCK(st.st_mode):
            raise SocketWaitError(
                '{} appeared within the timeout period, but it is not a '
                'Unix domain socket'.format(port.vhostuser_socket)
            )

        logger.debug(
            '%s',
            '{} has appeared (wait time: {})'.format(
                port.vhostuser_socket,
                config_opts.format_timedelta(timedelta(milliseconds=slept_ms))
            )
        )

        return _StepStatus.OK


class _CheckIOMMU(_Step, _RootDirKnob):
    def __init__(self, check_fn, mode, **kwds):
        super(_CheckIOMMU, self).__init__(**kwds)
        self._check_fn = check_fn
        self.mode = mode
        self.kernel_version = None

    @config_section('iommu')
    def configure(self, kernel_version=None):
        self.kernel_version = kernel_version

    def forward_action(self, session, port, journal):
        if self._check_fn is None:
            # iommu_check module not imported.
            return _SkipAction

        return _Action(
            lambda: self._do(session, port, journal),
            'Check that the IOMMU is set appropriately for {} plug mode'
            .format(self.mode)
        )

    def reverse_action(self, session, port, journal):
        # Nothing to undo.
        return _NullAction

    def _do(self, session, port, journal):
        kwds = {}
        if self.kernel_version is not None:
            kwds['_r'] = self.kernel_version

        self._check_fn(_root_dir=self.root_dir, **kwds)
        return _StepStatus.OK


class _CheckKSM(_Step, _RootDirKnob):
    def __init__(self, **kwds):
        super(_CheckKSM, self).__init__(**kwds)

    def forward_action(self, session, port, journal):
        return _Action(
            lambda: self._do(session, port, journal),
            description=None,  # don't log this step
        )

    def reverse_action(self, session, port, journal):
        # Nothing to undo.
        return _NullAction

    def _do(self, session, port, journal):
        if config_opts.is_ksm_enabled(_root_dir=self.root_dir):
            logger.warning(
                'Kernel Shared Memory (KSM) is enabled on this system. This '
                'feature may degrade performance when used with VirtIO '
                'hardware accelerated networking'
            )

        return _StepStatus.OK


class _AllocateVF(_Step):
    def __init__(self, vf_pool, **kwds):
        super(_AllocateVF, self).__init__(**kwds)
        self.vf_pool = vf_pool

    def forward_action(self, session, port, journal):
        return _Action(
            lambda: self._do(session, port, journal),
            'Create VF reservation',
        )

    def reverse_action(self, session, port, journal):
        if port.vf is None:
            return _NullAction
        else:
            return _Action(
                lambda: self._undo(session, port, journal),
                'Remove VF reservation',
            )

    def _do(self, session, port, journal):
        # Even if the port already has a VF, call allocate_vf() again to
        # convert temporary reservations to permanent.

        addr = self.vf_pool.allocate_vf(
            session, port.uuid, expires=None, raise_on_failure=True
        )
        port.vf = session.query(vf.VF).get(addr)

        return _StepStatus.OK

    def _undo(self, session, port, journal):
        self.vf_pool.deallocate_vf(session, port.vf.addr)
        session.refresh(port)

        return _StepStatus.OK


class _BringUpFallback(_Step):
    def __init__(self, fallback_map, **kwds):
        super(_BringUpFallback, self).__init__(**kwds)
        self.fallback_map = fallback_map

    @config_section('fallback')
    def configure(self, execute=None):
        if execute is not None:
            self._execute_impl = execute

    @staticmethod
    def translate_conf(conf):
        ans = _Step.translate_conf(conf)

        g = getattr(conf, 'iproute2', None)
        if g is not None and g.dry_run:
            def _execute_dry_run(cmd):
                pass
            ans.update({'fallback': {'execute': _execute_dry_run}})

        return ans

    def forward_action(self, session, port, journal):
        interface = self.fallback_map.vfmap[port.vf.addr]

        return _Action(
            lambda: self._do(session, port, journal, interface),
            'Bring up interface: {} and set its MAC address to {}'
            .format(interface, port.mac),
        )

    def reverse_action(self, session, port, journal):
        interface = self.fallback_map.vfmap[port.vf.addr]

        return _Action(
            lambda: self._undo(session, port, journal, interface),
            'Bring down interface: {}'.format(interface),
        )

    def _do(self, session, port, journal, interface):
        ip_link = ('ip', 'link', 'set', 'dev', interface.netdev)
        self.execute(ip_link + ('address', port.mac))
        self.execute(ip_link + ('up',))
        return _StepStatus.OK

    def _undo(self, session, port, journal, interface):
        self.execute(('ip', 'link', 'set', 'dev', interface.netdev, 'down'))
        return _StepStatus.OK

    def execute(self, cmd):
        try:
            self._execute_impl(cmd)

        except oslo_processutils.ProcessExecutionError as e:
            logger.exception('%s', "command failed: {}".format(cmd))
            raise

    # testing hook
    def _execute_impl(self, cmd):
        nova_utils.execute(*cmd, run_as_root=True)

DEFAULT_VIRTIO_DRIVER = 'igb_uio'
DEFAULT_VIRTIO_STUB_DRIVER = 'pci-stub'


class DriverLoadError(Exception):
    """Required device driver not loaded."""
    pass


class _AttachDriver(_Step, _RootDirKnob):
    """Attach driver (e.g., igb_uio or pci-stub) to a device."""

    # NOTE: This step requires root privileges for correct operation.

    def __init__(self, **kwds):
        super(_AttachDriver, self).__init__(**kwds)
        self.driver = self.stub_driver = None

    @config_section('virtio.drivers')
    def configure(
        self,
        driver=DEFAULT_VIRTIO_DRIVER, stub_driver=DEFAULT_VIRTIO_STUB_DRIVER
    ):
        self.driver = driver
        self.stub_driver = stub_driver

    @staticmethod
    def translate_conf(conf):
        ans = _Step.translate_conf(conf)

        g = getattr(conf, 'virtio-relay')
        ans.update({
            'virtio.drivers': {
                'driver': g.driver,
                'stub_driver': g.stub_driver,
            },
        })

        return ans

    def _device_path(self, addr, *tail):
        return os.path.join(
            self.root_dir, 'sys/bus/pci/devices', str(addr), *tail
        )

    def _driver_path(self, driver_name, *tail):
        return os.path.join(
            self.root_dir, 'sys/bus/pci/drivers', driver_name, *tail
        )

    def _read_device_int(self, addr, fname):
        with open(self._device_path(addr, fname), 'r') as fh:
            return int(fh.readline(), 0)

    def _new_id(self, addr, driver):
        vendor = self._read_device_int(addr, 'vendor')
        device = self._read_device_int(addr, 'device')
        new_id_path = self._driver_path(driver, 'new_id')
        with open(new_id_path, 'w') as fh:
            new_id_data = '{:x} {:x}'.format(vendor, device)
            print >>fh, new_id_data
            logger.debug('wrote %s to %s', new_id_data, new_id_path)

    def _rebind(self, addr, expected_old_driver, new_driver):
        # Sanity check: `new_driver` must be loaded. (If it's not, _new_id()
        # would fail immediately anyway, but this gives a clearer error
        # message.)
        if not os.path.isdir(self._driver_path(new_driver)):
            raise DriverLoadError(
                'Required device driver "{}" does not appear to be loaded'.
                format(new_driver)
            )

        # NOTE: new_driver will probe immediately upon executing this step.
        # Source:
        # <https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-bus-pci>
        self._new_id(addr, new_driver)

        addr_str = str(addr)

        # Per
        # <https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-bus-pci>,
        # older kernels may not accept sysfs bind/unbind store() data that ends
        # in newlines.

        # Unbind from old driver.
        old_driver_path = self._device_path(addr, 'driver')
        try:
            old_driver = os.path.split(os.readlink(old_driver_path))[1]

        except (OSError, IOError) as e:
            if e.errno != errno.ENOENT:
                raise

            old_driver = None
            logger.warning('VF %s was not bound to any driver', addr_str)

        else:
            if old_driver == new_driver:
                logger.info(
                    'VF %s is already bound to %s; not rebinding',
                    addr_str, new_driver
                )
                return _StepStatus.SKIP

            if old_driver != expected_old_driver:
                logger.warning(
                    'VF %s was bound to %s; expected it to be bound to %s '
                    'instead', addr_str, old_driver, expected_old_driver
                )

        if old_driver is not None:
            logger.info('unbinding VF %s from %s', addr_str, old_driver)

            try:
                with open(os.path.join(old_driver_path, 'unbind'), 'w') as fh:
                    fh.write(addr_str)

            except Exception:
                logger.error(
                    'failed to unbind VF %s from %s', addr_str, old_driver
                )
                raise

        # Bind to new driver.
        logger.info('binding VF %s to %s', addr_str, new_driver)
        bind_path = self._driver_path(new_driver, 'bind')
        try:
            with open(bind_path, 'w') as fh:
                fh.write(addr_str)

        except Exception:
            logger.error('failed to bind VF %s to %s', addr_str, new_driver)
            raise

        return _StepStatus.OK

    def forward_action(self, session, port, journal):
        args = (port.vf.addr, self.stub_driver, self.driver)
        return _Action(
            lambda: self._rebind(*args),
            'Rebind PCIe device {} from {} to {}'.format(*args)
        )

    def reverse_action(self, session, port, journal):
        args = (port.vf.addr, self.driver, self.stub_driver)
        return _Action(
            lambda: self._rebind(*args),
            'Rebind PCIe device {} from {} to {}'.format(*args)
        )


def _format_PortControlRequest(req):
    return '[op={}, pci_addr={}, vf={}]'.format(
        relay.PortControlRequest.Op.Name(req.op),
        pci.PciAddress(
            domain=req.pci_addr.domain,
            bus=req.pci_addr.bus,
            slot=req.pci_addr.slot,
            function=req.pci_addr.function
        ),
        req.vf,
    )


def _format_PortControlResponse(response):
    if response.status == response.OK:
        return 'OK'
    elif response.error_code_source is not None:
        if response.error_code is not None:
            return 'ERROR: {} returned {}'.format(
                response.error_code_source, response.error_code
            )
        else:
            return 'ERROR: unspecified error from {}'.format(
                response.error_code_source
            )
    else:
        return 'ERROR: unspecified error (possibly a protocol error)'


class VirtIORelayError(Exception):
    """
    Error while talking to virtiorelayd.
    """
    pass


class VirtIORelayPlugError(VirtIORelayError):
    """
    Error while plugging port into virtiorelayd.
    """
    pass


class VirtIORelayUnplugError(VirtIORelayError):
    """
    Error while unplugging port from virtiorelayd.
    """
    pass


DEFAULT_VIRTIO_ZMQ_EP = 'ipc:///run/virtiorelayd/port_control'
DEFAULT_VIRTIO_RCVTIMEO_MS = 60000


class _SetupVirtIO(_Step):
    # NOTE: This step requires root privileges for correct operation (due to
    # needing to write to virtiorelayd socket owned by root).

    def __init__(self, fallback_map, **kwds):
        super(_SetupVirtIO, self).__init__(**kwds)
        self.fallback_map = fallback_map
        self.zmq_ep = None

    @config_section('virtio.zmq')
    def configure(
        self, ep=DEFAULT_VIRTIO_ZMQ_EP, rcvtimeo_ms=DEFAULT_VIRTIO_RCVTIMEO_MS
    ):
        self.zmq_ep = ep
        self.rcvtimeo_ms = int(rcvtimeo_ms)

    @staticmethod
    def translate_conf(conf):
        ans = _Step.translate_conf(conf)

        g = getattr(conf, 'virtio-relay')
        ans.update({
            'virtio.zmq': {
                'ep': g.zmq_ep,
                'rcvtimeo_ms': int(round(
                    g.zmq_receive_timeout.total_seconds() * 1000
                )),
            },
        })

        return ans

    def _send_request(self, vf_addr, op):
        context = zmq.Context()

        socket = context.socket(zmq.REQ)
        socket.setsockopt(zmq.LINGER, 0)
        socket.setsockopt(zmq.SNDTIMEO, 0)
        socket.setsockopt(zmq.RCVTIMEO, self.rcvtimeo_ms)

        logger.debug('ZeroMQ: connecting to %s', self.zmq_ep)
        socket.connect(self.zmq_ep)

        req = relay.PortControlRequest(
            op=op,
            vf=self.fallback_map.vfmap[vf_addr].vf_number,
        )
        vf_addr.copy_to(req.pci_addr)

        logger.info(
            'sending PortControlRequest: %s', _format_PortControlRequest(req)
        )

        socket.send(req.SerializeToString())

        timeout_str = config_opts.format_timedelta(
            timedelta(milliseconds=self.rcvtimeo_ms)
        )
        logger.debug(
            'waiting up to %s for PortControlResponse from virtiorelayd',
            timeout_str
        )
        response = relay.PortControlResponse()
        try:
            response_data = socket.recv()
        except zmq.error.Again as e:
            msg = 'timeout waiting for PortControlResponse from virtiorelayd'
            logger.error('%s', msg)
            raise VirtIORelayPlugError(msg)

        try:
            response.ParseFromString(response_data)
        except Exception as e:
            msg = _exception_msg(e, 'PortControlResponse')
            logger.error('%s', msg)
            raise VirtIORelayPlugError(msg)

        if not response.IsInitialized():
            e = ValueError('incomplete PortControlResponse')
            logger.error('%s', _exception_msg(e, 'PortControlResponse'))
            raise e

        response_str = _format_PortControlResponse(response)
        log = logger.info if response.status is response.OK else logger.error
        log('PortControlResponse: %s', response_str)
        if response.status != response.OK:
            raise VirtIORelayPlugError(response_str)

        return _StepStatus.OK

    def forward_action(self, session, port, journal):
        # Do a second (unconditional) import of virtiorelayd_pb2 here to make
        # the error messages clearer if it is not installed. (VRT-747)
        import netronome.virtiorelayd.virtiorelayd_pb2 as relay

        vf_addr = port.vf.addr
        return _Action(
            lambda: self._send_request(vf_addr, relay.PortControlRequest.ADD),
            'Register PCIe device {} with virtiorelayd'.format(vf_addr),
        )

    def reverse_action(self, session, port, journal):
        # Do a second (unconditional) import of virtiorelayd_pb2 here to make
        # the error messages clearer if it is not installed. (VRT-747)
        import netronome.virtiorelayd.virtiorelayd_pb2 as relay

        vf_addr = port.vf.addr
        return _Action(
            lambda: self._send_request(
                vf_addr, relay.PortControlRequest.REMOVE
            ),
            'Unregister PCIe device {} from virtiorelayd'.format(vf_addr),
        )


class _PlugDriver(object):
    """A particular version of the port plug/unplug process."""

    STEP_TYPES = frozenset()

    def __init__(self, **kwds):
        super(_PlugDriver, self).__init__(**kwds)

        self.steps = None
        self._configured = False

    def set_steps(self, config, steps):
        config = copy.deepcopy(config)

        def _assert_empty(d, msg):
            if d:
                raise ValueError(': '.join((msg, ', '.join(sorted(d.keys())))))

        # Global configuration is handled internally by set_steps().
        c_global = config.pop('*', {})
        root_dir = c_global.pop('root_dir', None)
        _assert_empty(c_global, 'unknown option(s) in global config section')

        for s in steps:
            # We don't want duck typing here. We want them all to be _Steps,
            # so that they get the _Step.__str__(). (Bug affected
            # _AttachDriver, 2016-06-29.)
            assert isinstance(s, _Step), \
                '{} is not a _Step'.format(repr(s))

            # Make sure that someone did not accidentally put an object into
            # STEP_TYPES. (This actually happened at one point.)
            assert \
                all(map(lambda t: isinstance(t, type), self.STEP_TYPES)), \
                'one or more elements of {}.STEP_TYPES is not a type'.format(
                    type(self).__name__
                )

            # Make sure that every step type is listed in
            # type(self).STEP_TYPES. (Safety check for translate_conf().)
            assert \
                type(s) in type(self).STEP_TYPES, \
                '{} is not listed in {}.STEP_TYPES'.format(
                    type(s).__name__, type(self).__name__
                )

            if root_dir is not None:
                apply_root_dir(s, root_dir)

            if hasattr(s, 'configure'):
                section = s.configure.section
                c = config.pop(section, {})
                s.configure(**c)

        _assert_empty(config, 'unknown config section(s)')

        self.steps = steps
        self._configured = True

    def plug(self, session, port):
        """
        Perform plug operation and return journal.
        """

        assert self._configured, '__init__ must call set_steps()'

        done = []
        journal = {}
        t = None

        try:
            for step in self.steps:
                t = type(step)
                old_journal = copy.deepcopy(journal)

                action = step.forward_action(session, port, journal)
                assert isinstance(action, _Action), (
                    '{}.forward_action() did not return an _Action'.
                    format(t.__name__)
                )

                if action.enabled:
                    if action.description is not None:
                        logger.debug(
                            '[plugging: %s] %s', port.uuid, action.description
                        )
                    status = action.execute()

                    assert isinstance(status, _StepStatus), (
                        '{} forward action did not return a _StepStatus'.
                        format(t.__name__)
                    )
                    assert journal == old_journal, (
                        '{} forward action changed the journal'.
                        format(t.__name__)
                    )
                else:
                    status = _StepStatus.SKIP

                journal[t] = status

                if status != _StepStatus.SKIP:
                    done.append(step)

        except Exception as eouter:
            if t is not None:
                journal[t] = _StepStatus.EXCEPTION

            # Selected "unexpected" exceptions.
            log_exc_info = isinstance(
                eouter, (AttributeError, KeyError, NameError, TypeError)
            )
            logger.critical(
                '%s', _exception_msg(eouter, 'exception during plug'),
                exc_info=log_exc_info
            )
            if isinstance(eouter, NotImplementedError) and t is not None:
                logger.debug('step type: %s', t.__name__)

            # undo all previously applied steps
            for step in reversed(done):
                t = type(step)

                action = step.reverse_action(session, port, journal)
                assert isinstance(action, _Action), (
                    '{}.reverse_action() did not return an _Action'.
                    format(t.__name__)
                )

                if action.enabled:
                    if action.description is not None:
                        logger.debug(
                            '[cleanup after failure: %s] %s',
                            port.uuid, action.description
                        )

                    try:
                        status = action.execute()
                        assert isinstance(status, _StepStatus), (
                            '{} reverse action did not return a _StepStatus'.
                            format(t.__name__)
                        )

                    except Exception as einner:
                        status = _StepStatus.EXCEPTION_DURING_CLEANUP
                        logger.critical(
                            '%s',
                            _exception_msg(einner, 'exception during cleanup')
                        )
                        if isinstance(einner, NotImplementedError):
                            logger.debug('step type: %s', t.__name__)

                else:
                    status = _StepStatus.SKIP

                if t not in journal or status < journal[t]:
                    journal[t] = status

            raise eouter

        return journal

    def unplug(self, session, port):
        """
        Perform unplug operation and return journal.
        """

        assert self._configured, '__init__ must call set_steps()'

        journal = {}
        t = None
        einfo = None

        for step in reversed(self.steps):
            t = type(step)
            old_journal = copy.deepcopy(journal)

            action = step.reverse_action(session, port, journal)
            assert isinstance(action, _Action), (
                '{}.reverse_action() did not return an _Action'.
                format(t.__name__)
            )

            if action.enabled:
                if action.description is not None:
                    logger.debug(
                        '[unplugging: %s] %s', port.uuid, action.description
                    )

                try:
                    status = action.execute()

                    assert isinstance(status, _StepStatus), (
                        '{} reverse action did not return a _StepStatus'.
                        format(t.__name__)
                    )
                    assert journal == old_journal, (
                        '{} reverse action changed the journal'.
                        format(t.__name__)
                    )

                except Exception as e:
                    status = _StepStatus.EXCEPTION

                    logger.critical(
                        '%s', _exception_msg(e, 'exception during unplug')
                    )
                    if isinstance(e, NotImplementedError) and t is not None:
                        logger.debug('step type: %s', t.__name__)

                    if einfo is None:
                        einfo = sys.exc_info()

            else:
                status = _StepStatus.SKIP

            journal[type(step)] = status

        if einfo is not None:
            six.reraise(*einfo)

        return journal

    @classmethod
    def translate_conf(cls, conf):
        """
        Translate config_opts configuration to options recognized by this plug
        driver's steps.
        """

        ans = {}
        for step_type in cls.STEP_TYPES:
            ans.update(step_type.translate_conf(conf))
        return ans


class _PlugUnaccelerated(_PlugDriver):
    """Perform unaccelerated plug operation."""

    STEP_TYPES = frozenset((
        _CreateTAP,
        _AgentFileWrite,
        _AgentPost,
        _AgentRaiseOnCompleteFailure,
        _DPDKSocketWait,
    ))

    def __init__(self, config={}, **kwds):
        super(_PlugUnaccelerated, self).__init__(**kwds)

        self.set_steps(
            config, (
                _CreateTAP(),
                _AgentFileWrite(),
                _AgentPost(),
                _AgentRaiseOnCompleteFailure(),
                _DPDKSocketWait(),
            )
        )


def _sriov_get_vif_devname(fallback_map):
    def _fn(port):
        return fallback_map.vfmap[port.vf.addr].netdev
    return _fn


class _PlugSRIOV(_PlugDriver):
    """Perform SR-IOV NFP plug operation."""

    STEP_TYPES = frozenset((
        _CheckIOMMU,
        _AllocateVF,
        _BringUpFallback,
        _AgentFileWrite,
        _AgentPost,
        _AgentRaiseOnCompleteFailure,
    ))

    def __init__(self, vf_pool, config={}, **kwds):
        super(_PlugSRIOV, self).__init__(**kwds)

        sriov_vif_devname = _sriov_get_vif_devname(vf_pool.fallback_map)
        sriov_iommu_check = (
            None if iommu_check is None else iommu_check.sriov_iommu_check
        )
        self.set_steps(
            config, (
                _CheckIOMMU(sriov_iommu_check, mode=PM.SRIOV),
                _AllocateVF(vf_pool=vf_pool),
                _BringUpFallback(fallback_map=vf_pool.fallback_map),
                _AgentFileWrite(get_vif_devname=sriov_vif_devname),
                _AgentPost(
                    get_vif_devname=sriov_vif_devname,
                    vif_sync=_WaitForFirmwareVIFTable(),
                ),
                _AgentRaiseOnCompleteFailure(),
            )
        )

_virtio_get_vif_devname = _sriov_get_vif_devname


class _PlugVirtIO(_PlugDriver):
    """Perform VirtIO NFP plug operation."""

    STEP_TYPES = frozenset((
        _CheckIOMMU,
        _CheckKSM,
        _AllocateVF,
        _AttachDriver,
        _BringUpFallback,
        _SetupVirtIO,
        _AgentFileWrite,
        _AgentPost,
        _AgentRaiseOnCompleteFailure,
    ))

    def __init__(self, vf_pool, config={}, **kwds):
        super(_PlugVirtIO, self).__init__(**kwds)

        virtio_vif_devname = _virtio_get_vif_devname(vf_pool.fallback_map)
        virtio_iommu_check = (
            None if iommu_check is None else iommu_check.virtio_iommu_check
        )
        self.set_steps(
            config, (
                _CheckIOMMU(virtio_iommu_check, mode=PM.VirtIO),
                _CheckKSM(),
                _AllocateVF(vf_pool=vf_pool),
                _AttachDriver(),
                _BringUpFallback(fallback_map=vf_pool.fallback_map),
                _SetupVirtIO(fallback_map=vf_pool.fallback_map),
                _AgentFileWrite(get_vif_devname=virtio_vif_devname),
                _AgentPost(
                    get_vif_devname=virtio_vif_devname,
                    vif_sync=_WaitForFirmwareVIFTable(),
                ),
                _AgentRaiseOnCompleteFailure(),
            )
        )


class PlugModeError(Exception):
    pass


def _sanity_check_vf_pool(vf_pool):
    """
    Sanity check for VRT-604 consolidation of VF pool constructor calls at
    the top level of the call graph.
    """
    if not hasattr(vf_pool, 'fallback_map'):
        raise ValueError('vf_pool must have a fallback_map')


def _get_plug_driver(plug_mode, vf_pool, conf, _root_dir=None):
    _sanity_check_vf_pool(vf_pool)

    if plug_mode == PM.unaccelerated:
        cls = _PlugUnaccelerated
        kwds = {}

    elif plug_mode == PM.SRIOV:
        cls = _PlugSRIOV
        kwds = {'vf_pool': vf_pool}

    elif plug_mode == PM.VirtIO:
        cls = _PlugVirtIO
        kwds = {'vf_pool': vf_pool}

    else:
        assert False, 'unknown plug mode {}'.format(plug_mode)

    # Translate Oslo configuration to internal configuration structures, and
    # set global options (e.g., "root_dir" testing knob).
    config = cls.translate_conf(conf)
    assert '*' not in config, \
        'translate_conf() is not allowed to set global options'
    c = config['*'] = {}
    if _root_dir is not None:
        c['root_dir'] = _root_dir

    return cls(config=config, **kwds)

_NON_ERROR_STATUSES = frozenset((_StepStatus.OK, _StepStatus.SKIP))


def _log_plug(p, journal, operation):
    msg = ['{} plug operation succeeded'.format(p.plug.mode)]
    log = logger.info

    if frozenset(journal.itervalues()) - _NON_ERROR_STATUSES != frozenset():
        msg.append('(with one or more steps reporting a recoverable error)')
        log = logger.warning

    log('[%s: %s] %s', operation, p.uuid, ' '.join(msg))


def _log_unplug(p, journal, operation):
    mode = 'partial' if p.tap_name is None else p.plug.mode  # VRT-604
    msg = ['{} unplug operation succeeded'.format(mode)]
    log = logger.info

    if frozenset(journal.itervalues()) - _NON_ERROR_STATUSES != frozenset():
        msg.append('(with one or more steps reporting a recoverable error)')
        log = logger.warning

    log('[%s: %s] %s', operation, p.uuid, ' '.join(msg))


def plug_port(session, vf_pool, conf, _root_dir=None):
    """
    :param _root_dir: testing knob
    """
    logger.info('[plugging: %s] Start of plug operation', conf.uuid)

    _sanity_check_vf_pool(vf_pool)

    # Verify that the port's plug mode has already been configured.
    pm = one_or_none(
        session.query(port.PlugMode).
        filter(port.PlugMode.neutron_port == conf.uuid)
    )

    if pm is None:
        raise PlugModeError(
            'no plug mode was found for port {}'.format(conf.uuid)
        )

    logger.debug(
        '[plugging: %s] '
        'Port is configured for plug mode "%s"', conf.uuid, pm.mode
    )

    # Now create (or update) the port object.
    p = one_or_none(
        session.query(port.Port).filter(port.Port.uuid == conf.uuid)
    )

    if p is None:
        p = port.Port(
            uuid=conf.uuid,
            instance_uuid=conf.instance_uuid,
            vn_uuid=conf.vn_uuid,
            vm_project_uuid=conf.vm_project_uuid,
            ip_address=conf.ip_address,
            ipv6_address=conf.ipv6_address,
            vm_name=conf.vm_name,
            mac=conf.mac,
            tap_name=conf.tap_name,
            port_type=conf.port_type,
            vif_type=conf.vif_type,
            tx_vlan_id=conf.tx_vlan_id,
            rx_vlan_id=conf.rx_vlan_id,
            vhostuser_socket=conf.vhostuser_socket
        )
        p.plug = pm
        session.add(p)

    assert p.plug is not None, 'p.plug is unexpectedly None'
    plug_driver = _get_plug_driver(
        p.plug.mode, vf_pool, conf, _root_dir=_root_dir
    )
    journal = plug_driver.plug(session, p)
    _log_plug(p, journal, 'plugging')


def unplug_port(session, vf_pool, conf, _root_dir=None):
    logger.info('[unplugging: %s] Start of unplug operation', conf.uuid)

    _sanity_check_vf_pool(vf_pool)

    pm = one_or_none(
        session.query(port.PlugMode).
        filter(port.PlugMode.neutron_port == conf.uuid)
    )
    p = one_or_none(
        session.query(port.Port).filter(port.Port.uuid == conf.uuid)
    )
    v = one_or_none(
        session.query(vf.VF).filter(vf.VF.neutron_port == conf.uuid)
    )

    # Keep track of whether we are able to figure out how to unplug the port.
    # If we didn't know how to unplug the port because it had incomplete
    # records, send an unplug message to the agent anyway.
    #
    # This covers the case where the agent knows about the port but we don't
    # (which can happen after a power loss or a kernel panic, if the agent's
    # database in /var/lib/contrail/ports were to recover with the JSON port
    # file intact, but our SQLite database recovered by undoing the port add
    # transaction).
    #
    # See VRT-604.
    unplug_for_gc = True

    if p is not None and pm is not None:
        assert p.plug == pm
        plug_driver = _get_plug_driver(
            p.plug.mode, vf_pool, conf, _root_dir=_root_dir
        )
        journal = plug_driver.unplug(session, p)
        _log_unplug(p, journal, 'unplugging')
        unplug_for_gc = False

    elif p is None and pm is not None:
        logger.warning(
            '[unplugging: %s] '
            'Port had a plug mode configured, but was never actually plugged',
            conf.uuid
        )

    elif p is not None and pm is None:
        logger.warning(
            '[unplugging: %s] Port does not appear to be plugged', conf.uuid
        )

    else:
        pass

    found = False

    if p is not None:
        p.plug = None
        p.vf = None

    if pm is not None:
        logger.info(
            '[unplugging: %s] '
            'Remove record that port was configured in %s mode',
            conf.uuid, pm.mode
        )
        session.delete(pm)
        found = True

    if v is not None:
        logger.info(
            '[unplugging: %s] Remove VF reservation %s', conf.uuid, v.addr
        )
        session.delete(v)
        found = True

    if p is not None:
        logger.info(
            '[unplugging: %s] Remove record for port', conf.uuid
        )
        session.delete(p)
        found = True

    if not found:
        logger.debug('[unplugging: %s] Port not found', conf.uuid)

    if unplug_for_gc:
        class Mock(object):
            pass

        # Create a fake port object since the plug driver requires one.
        p = port.Port(
            uuid=conf.uuid,
            instance_uuid=None,
            vn_uuid=None,
            vm_project_uuid=None,
            ip_address=None,
            ipv6_address=None,
            vm_name=None,
            mac=None,
            tap_name=None,  # Disables TAP creation/deletion.
            port_type='NovaVMPort',
            vif_type='Vrouter',
            tx_vlan_id=-1,
            rx_vlan_id=-1,
            vhostuser_socket=None,
        )
        plug_driver = _get_plug_driver(
            PM.unaccelerated, vf_pool, conf, _root_dir=_root_dir
        )
        journal = plug_driver.unplug(session, p)
        _log_unplug(p, journal, 'unplugging')
