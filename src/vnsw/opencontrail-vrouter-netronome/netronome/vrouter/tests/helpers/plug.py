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

import gc
import logging
import os
import random
import socket
import threading
import uuid

from datetime import timedelta
from tornado.web import HTTPError

import tornado.httpserver
import tornado.ioloop
import tornado.netutil
import tornado.options
import tornado.web

try:
    import zmq
    import netronome.virtiorelayd.virtiorelayd_pb2 as relay
except ImportError:
    relay = None

from netronome.vrouter import (fallback, plug, rest as vrouter_rest)
from netronome.vrouter.tests.unit import *
from netronome.vrouter.tests.helpers.config import _random_pci_address

# urllib3 log messages can appear under various names depending on the versions
# of the requests and/or urllib3 libraries (e.g., as they may change between
# different releases of Ubuntu OpenStack).
URLLIB3_LOGGERS = frozenset((
    'urllib3.connectionpool',
    'urllib3.util.retry',
    'requests.packages.urllib3.connectionpool',
    'requests.packages.urllib3.util.retry',
))


def _enable_fake_intel_iommu(root_dir, pt=False):
    """Fakes out netronome.iommu_check to believe that the IOMMU is on."""
    plug.makedirs(os.path.join(root_dir, 'sys/kernel/slab/iommu_devinfo'))

    if pt:
        proc = os.path.join(root_dir, 'proc')
        plug.makedirs(proc)
        with open(os.path.join(proc, 'cmdline'), 'w') as fh:
            print >>fh, 'iommu=pt'


def _make_fake_sysfs_fallback_map(n=1):
    lines = []
    used = set()

    while len(lines) < n:
        pf_addr = _random_pci_address()
        vf_addr = _random_pci_address()
        fake_fallback_devname = 'test{}'.format(random.randint(0, 10000))
        fake_vf_number = random.randint(0, 59)

        line = '{} {} {} {}'.format(
            pf_addr, vf_addr, fake_fallback_devname, fake_vf_number
        )
        if line in used:
            continue
        used.add(line)
        lines.append(line)

    return fallback.read_sysfs_fallback_map(_in='\n'.join(lines))


class _DisableGC(object):
    """
    With the mock server threads used for some of the tests, SQLite connections
    are occasionally getting returned to the SQLAlchemy connection pool (by
    garbage collection) on the wrong thread.

    This causes non-deterministic "INFO:ProgrammingError:SQLite objects created
    in a thread can only be used in that same thread" messages on the
    sqlalchemy.pool.NullPool logger, followed by "ERROR:Exception closing
    connection" and "ERROR:Exception during reset or similar."

    We could just mute the sqlalchemy.pool.NullPool logger but since it really
    is wrong to pass SQLite connections between threads, let's just disable GC
    during the affected tests (since the mock server threads are short-lived).
    """

    def __init__(self, *args, **kwds):
        super(_DisableGC, self).__init__(*args, **kwds)
        self.reenable = False

    def setUp(self):
        self.reenable = gc.isenabled()
        gc.disable()

    def tearDown(self):
        if self.reenable:
            gc.enable()
            gc.collect()


class FakeAgent(object):
    """
    A fake contrail-vrouter-agent. Runs on a background (daemon) thread.
    """

    def __init__(
        self,
        vhostuser_socket=None,
        port_post_handler=None,
        port_delete_handler=None,
        sysfs=None,
        run_timedelta=timedelta(seconds=10)
    ):
        self.loop = tornado.ioloop.IOLoop()
        self.good_requests = []
        self.ports = {}
        self.timeout = False

        self.port_post_handler = port_post_handler \
            or FakeAgent._PortPostHandler
        self.port_delete_handler = port_delete_handler \
            or FakeAgent._PortDeleteHandler

        self.sysfs = sysfs

        # The real contrail-vrouter-agent uses a backdoor to Neutron to find
        # out whether a port is supposed to be running in DPDK mode. We don't
        # have that luxury so, whoever is testing us must tell us if there is
        # supposed to be a vhostuser socket.
        self.vhostuser_socket = vhostuser_socket

        # Maximum amount of time that run() is allowed to execute.
        self.run_timedelta = run_timedelta

    class Config(object):
        """
        Communicates the agent's OS-assigned URL back to the client.
        """
        def __init__(self, **kwds):
            super(FakeAgent.Config, self).__init__(**kwds)
            self.base_url = None

    class Sysfs(object):
        """
        Manages sysfs files, in particular the virtual_vifs_plugged file.
        """
        def __init__(self, root_dir):
            self.virtual_vifs_plugged_fpath = os.path.join(
                root_dir, 'sys/module/nfp_vrouter/control/virtual_vifs_plugged'
            )
            self.virtual_vifs_plugged = None
            self.loop = None

        def configure(self, loop):
            self.loop = loop

            # Random initial contents for virtual_vifs_plugged.
            self.virtual_vifs_plugged = s = set()
            for i in xrange(7):
                if random.randint(0, 99) < 15:
                    s.add('junk{}'.format(i))

            plug.makedirs(os.path.dirname(self.virtual_vifs_plugged_fpath))
            self.write_virtual_vifs_plugged()

        def add_vif(self, vif):
            def _cb():
                self.virtual_vifs_plugged.add(vif)
                self.write_virtual_vifs_plugged()

            t = self.add_vif_timeout()
            if t is not None:
                self.loop.add_timeout(t, _cb)

        def add_vif_timeout(self):
            return timedelta(milliseconds=random.randint(250, 750))

        def remove_vif(self, vif):
            def _cb():
                self.virtual_vifs_plugged.discard(vif)
                self.write_virtual_vifs_plugged()

            t = self.remove_vif_timeout()
            if t is not None:
                self.loop.add_timeout(t, _cb)

        def remove_vif_timeout(self):
            return timedelta(milliseconds=random.randint(250, 750))

        def write_virtual_vifs_plugged(self):
            fpath = self.virtual_vifs_plugged_fpath + '.tmp'
            with open(fpath, 'w') as fh:
                for vif in self.virtual_vifs_plugged:
                    print >>fh, vif
            os.rename(fpath, self.virtual_vifs_plugged_fpath)

    class _PortPostHandler(vrouter_rest.JsonRequestHandler):
        def initialize(
            self, good_requests, ports, vhostuser_socket, sysfs, **kwds
        ):
            super(FakeAgent._PortPostHandler, self).initialize(**kwds)

            self.good_requests = good_requests
            self.ports = ports
            self.vhostuser_socket = vhostuser_socket
            self.sysfs = sysfs

        def post(self):
            try:
                vif = self.json_args['system-name']
                self.ports[uuid.UUID(self.json_args['id'])] = vif
            except (KeyError, ValueError):
                raise HTTPError(400)

            self.good_requests.append(('POST', self.json_args))
            self.create_dpdk_socket()

            if self.sysfs is not None:
                self.sysfs.add_vif(vif)

            self.finish_post_response()

        def create_dpdk_socket(self):
            if self.vhostuser_socket is None:
                return

            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.bind(self.vhostuser_socket)

        def finish_post_response(self):
            self.set_header('Content-Type', 'application/json')
            self.write({})

    class _PortDeleteHandler(tornado.web.RequestHandler):
        def initialize(self, good_requests, ports, sysfs, **kwds):
            super(FakeAgent._PortDeleteHandler, self).initialize(**kwds)

            self.good_requests = good_requests
            self.ports = ports
            self.sysfs = sysfs

        def delete(self, neutron_port):
            try:
                neutron_port = uuid.UUID(neutron_port)
            except ValueError:
                raise HTTPError(400)

            try:
                vif = self.ports.pop(neutron_port)
            except KeyError:
                raise HTTPError(404)

            self.good_requests.append(('DELETE', neutron_port))

            if self.sysfs is not None:
                self.sysfs.remove_vif(vif)

            self.finish_delete_response()

        def finish_delete_response(self):
            self.set_status(204)

    def run(self, config, config_ready):
        if self.sysfs is not None:
            self.sysfs.configure(loop=self.loop)

        endpoints = (
            (r'/port', self.port_post_handler, {
                'good_requests': self.good_requests,
                'ports': self.ports,
                'vhostuser_socket': self.vhostuser_socket,
                'sysfs': self.sysfs,
            }),

            (r'/port/([A-Fa-f0-9-]+)', self.port_delete_handler, {
                'good_requests': self.good_requests,
                'ports': self.ports,
                'sysfs': self.sysfs,
            }),
        )

        application = tornado.web.Application(endpoints)
        http_server = tornado.httpserver.HTTPServer(
            application, io_loop=self.loop
        )

        addr = '127.0.0.1'
        sockets = tornado.netutil.bind_sockets(0, address=addr)
        assert len(sockets) == 1
        http_server.add_sockets(sockets)
        config.base_url = 'http://{}:{}'.format(
            addr, sockets[0].getsockname()[1]
        )
        config_ready.set()

        def _on_timeout():
            self.timeout = True
            self.loop.stop()

        self.loop.add_timeout(self.run_timedelta, _on_timeout)

        # IOLoop.start() calls logging.basicConfig() for us unless we have
        # already set up a log handler. The question is, where do we have to
        # set it up?
        #
        # For Tornado 4.2.1 (SaltStack version), a log handler on any of the
        # root, 'tornado', or 'tornado.application' loggers will suppress the
        # automatic logging setup.
        #
        # For Tornado 3.1.1 (Ubuntu 14.04 version), Tornado only checks for a
        # handler on the root logger.
        with attachLogHandler(logging.root):
            self.loop.start()

        self.loop.close()

    def stop(self):
        self.sync()
        self.loop.add_callback(self.loop.stop)

    def sync(self):
        # Call from main thread to ensure we have made it back to the IOLoop.
        ioloop_idle = threading.Event()
        ioloop_idle.clear()

        def _callback():
            ioloop_idle.set()

        self.loop.add_callback(_callback)
        IDLE_TIMEOUT_SEC = 3
        ioloop_sync_ok = ioloop_idle.wait(IDLE_TIMEOUT_SEC)
        assert ioloop_sync_ok, 'ioloop did not become idle within {}s'.format(
            IDLE_TIMEOUT_SEC
        )

    @classmethod
    def boot(cls, assertTrue, **kwds):
        config = cls.Config()
        config_ready = threading.Event()
        config_ready.clear()

        agent = cls(**kwds)
        thr = threading.Thread(
            target=agent.run,
            kwargs={'config': config, 'config_ready': config_ready},
        )
        thr.daemon = True
        thr.start()

        READY_TIMEOUT_SEC = 5
        config_ready.wait(READY_TIMEOUT_SEC)
        assertTrue(
            config_ready.is_set(),
            'config not ready after {}s'.format(READY_TIMEOUT_SEC)
        )

        return config, agent, thr


class FakeVirtIOPortControlServer(object):
    def __init__(self, handle_request=None, **kwds):
        super(FakeVirtIOPortControlServer, self).__init__(**kwds)

        self.stop_event = threading.Event()
        self.handle_request = handle_request
        self.request_log = []
        self.request_types = {}

    def run(self, zmq_port_control_ep, ready_event):
        if zmq_port_control_ep is None:
            # don't listen; used to test connect() timeout
            ready_event.set()
            self.stop_event.wait()
            return

        context = zmq.Context()

        socket = context.socket(zmq.REP)
        socket.setsockopt(zmq.LINGER, 500)
        socket.setsockopt(zmq.SNDTIMEO, 500)
        socket.setsockopt(zmq.RCVTIMEO, 500)
        socket.bind(zmq_port_control_ep)

        ready_event.set()

        while not self.stop_event.is_set():
            try:
                request_data = socket.recv()
            except zmq.error.Again as e:
                # recv() timeout
                continue

            # TODO(wbrinzer): test the actual virtiorelayd with invalid request
            # data.
            request = self.decode_request(request_data)

            if self.handle_request is not None:
                response = self.handle_request(request)
                socket.send(response)
            else:
                # no response; used to test recv() timeout
                break

        self.stop_event.wait()

    def decode_request(self, request_data):
        req = relay.PortControlRequest()
        req.ParseFromString(request_data)
        self.request_log.append(plug._format_PortControlRequest(req))

        op_str = relay.PortControlRequest.Op.Name(req.op)
        self.request_types.setdefault(op_str, 0)
        self.request_types[op_str] += 1

        return req

    def stop(self):
        self.stop_event.set()

    @classmethod
    def boot(cls, assertTrue, server_zmq_port_control_ep, **kwds):
        ready_event = threading.Event()
        ready_event.clear()

        server = cls(**kwds)
        thr = threading.Thread(
            target=server.run,
            kwargs={
                'zmq_port_control_ep': server_zmq_port_control_ep,
                'ready_event': ready_event,
            },
        )
        thr.daemon = True
        thr.start()

        READY_TIMEOUT_SEC = 2
        ready_event.wait(READY_TIMEOUT_SEC)
        assertTrue(
            ready_event.is_set(),
            'server thread not ready after {}s'.format(READY_TIMEOUT_SEC)
        )

        return server, thr


class FakeVirtIOConfigServer(object):
    def __init__(self, handle_request=None, **kwds):
        super(FakeVirtIOConfigServer, self).__init__(**kwds)

        self.stop_event = threading.Event()
        self.handle_request = handle_request

    def run(self, zmq_config_ep, ready_event):
        if zmq_config_ep is None:
            # Don't listen. This can be used to test the connect() timeout.
            ready_event.set()
            self.stop_event.wait()
            return

        context = zmq.Context()

        socket = context.socket(zmq.REP)
        socket.setsockopt(zmq.LINGER, 500)
        socket.setsockopt(zmq.SNDTIMEO, 500)
        socket.setsockopt(zmq.RCVTIMEO, 500)
        socket.bind(zmq_config_ep)

        ready_event.set()

        while not self.stop_event.is_set():
            try:
                request_data = socket.recv()
            except zmq.error.Again as e:
                # recv() timeout
                continue

            request = self.decode_request(request_data)

            if self.handle_request is not None:
                response = self.handle_request(request)
                socket.send(response)
            else:
                # No response. This can be used used to test the recv()
                # timeout.
                break

        self.stop_event.wait()

    def decode_request(self, request_data):
        req = relay.ConfigRequest()
        req.ParseFromString(request_data)
        return req

    def stop(self):
        self.stop_event.set()

    @classmethod
    def boot(cls, assertTrue, server_zmq_config_ep, **kwds):
        ready_event = threading.Event()
        ready_event.clear()

        server = cls(**kwds)
        thr = threading.Thread(
            target=server.run,
            kwargs={
                'zmq_config_ep': server_zmq_config_ep,
                'ready_event': ready_event,
            },
        )
        thr.daemon = True
        thr.start()

        READY_TIMEOUT_SEC = 2
        ready_event.wait(READY_TIMEOUT_SEC)
        assertTrue(
            ready_event.is_set(),
            'server thread not ready after {}s'.format(READY_TIMEOUT_SEC)
        )

        return server, thr


if relay is None:
    FakeVirtIOPortControlServer = None
    FakeVirtIOConfigServer = None
