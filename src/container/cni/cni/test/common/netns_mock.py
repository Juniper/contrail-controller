import json
import mock
import unittest
import os
import requests
import sys
import errno
from pyroute2 import NetlinkError
from mock import patch
from mock import Mock

HOST_NS_PID = 0
CONTAINER_NS_PID = 1
CURRENT_NS_PID = 2

class NetnsMock(object):
    next_index = 0
    netns = None
    ifaces = None
    expected_netns = None
    routes = None
    ns_ifaces = None

    @staticmethod
    def reset():
        NetnsMock.netns = HOST_NS_PID
        NetnsMock.expected_netns = CONTAINER_NS_PID
        NetnsMock.ifaces = {
            0: { "name": "eth0", "ns": HOST_NS_PID },
            1: { "name": "eth1", "ns": HOST_NS_PID },
            2: { "name": "veth0", "ns": HOST_NS_PID },
            3: { "name": "veth1", "ns": HOST_NS_PID },
            4: { "name": "eth0", "ns": CONTAINER_NS_PID },
            5: { "name": "eth1", "ns": CONTAINER_NS_PID },
            6: { "name": "eth2", "ns": CONTAINER_NS_PID },
            7: { "name": "eth3", "ns": CONTAINER_NS_PID },
        }
        NetnsMock.next_index = 8
        NetnsMock.ns_ifaces = {
            HOST_NS_PID: [0,1,2,3],
            CONTAINER_NS_PID: [4,5,6,7]
        }
        NetnsMock.routes = []

    def __init__(self, ns_path):
        pass

    def __enter__(self):
        NetnsMock.netns = CONTAINER_NS_PID

    def __exit__(self, type, value, tb):
        NetnsMock.netns = HOST_NS_PID

    @staticmethod
    def getpid():
        return CURRENT_NS_PID

    @staticmethod
    def get(ifname, ns):
        idx = NetnsMock.find(ifname, ns)
        if idx is None:
            return None
        return NetnsMock.ifaces[idx]

    @staticmethod
    def find(ifname, ns):
        for i, x in enumerate(NetnsMock.ns_ifaces[ns]):
            if NetnsMock.ifaces[x]["name"]==ifname:
                return x
        return None

    @staticmethod
    def add(ifname, ns, **kwargs):
        NetnsMock.ifaces[NetnsMock.next_index] = {
            "name": ifname,
            "ns": ns
        }
        for key in kwargs:
            NetnsMock.ifaces[NetnsMock.next_index][key] = kwargs[key]
        NetnsMock.ns_ifaces[ns].append(NetnsMock.next_index)
        NetnsMock.next_index += 1
        return NetnsMock.next_index - 1

class IPRouteMock(object):
    def __init__(self, ns=None):
        if ns is None:
            self.netns = NetnsMock.netns
        else:
            self.netns = ns

    def link_lookup(self, ifname):
        idx = NetnsMock.find(ifname, self.netns)
        if idx is None:
            return []
        return [idx]

    def link_create(self, ifname, peer, kind, address):
        NetnsMock.add(ifname, self.netns, mac=address)
        NetnsMock.add(peer, self.netns)

    def link(self, cmd, index=None, ifname=None, net_ns_pid=None, **kwargs):
        if (index is not None) and (index not in NetnsMock.ifaces):
            raise NetlinkError(code=1)

        if cmd == "get":
            return NetnsMock.ifaces[index]
        elif cmd == "del":
            NetnsMock.ns_ifaces[NetnsMock.ifaces[index]["ns"]].remove(index)
            NetnsMock.ifaces.pop(index)
            return None
        elif cmd == "set":
            if ifname is not None:
                kwargs["name"] = ifname
            for key in kwargs:
                NetnsMock.ifaces[index][key] = kwargs[key]
            #if net_ns_pid is None:
            #    net_ns_pid = self.netns
            if net_ns_pid is not None:
                if net_ns_pid == CURRENT_NS_PID:
                    net_ns_pid = NetnsMock.netns
                if NetnsMock.ifaces[index]["ns"] != net_ns_pid:
                    NetnsMock.ifaces[index]["ns"] = net_ns_pid
                    NetnsMock.ns_ifaces[int(not net_ns_pid)].remove(index)
                    NetnsMock.ns_ifaces[net_ns_pid].append(index)
        elif cmd == "add":
            NetnsMock.add(ifname, self.netns, **kwargs)
        else:
            raise RuntimeError("Unsupported command " + cmd + " for link.")

    def addr(self, cmd, index, address, prefixlen):
        if not index in NetnsMock.ifaces:
            raise NetlinkError(code=1)

        if cmd == "add":
            NetnsMock.ifaces[index]["addr"] = address
            NetnsMock.ifaces[index]["plen"] = prefixlen
        else:
            raise RuntimeError()

    def route(self, cmd, dst, gateway):
        if cmd == "add":
            NetnsMock.routes.append({
                'dst': dst,
                'gw': gateway
            })
        else:
            raise RuntimeError()

