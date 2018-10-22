#!/bin/env python2
from re import match, MULTILINE
from collections import namedtuple

from multicloud import Multicloud

import docker

BIRD_CONTAINER_NAMES = [r'bird_bird_1']
BirdStatus = namedtuple("BirdStatus", ["peer", "is_up"])


class Bird(Multicloud):
    def __init__(self, logger):
        Multicloud.__init__(self, logger)

        self.names = BIRD_CONTAINER_NAMES

    def get_tunnel_status(self):
        client = docker.from_env()
        exc = client.exec_create(self.names[0], ' '.join(["birdc", "show", "bfd", "sessions"]))
        stdout = client.exec_start(exc)

        stats = []

        for line in stdout.splitlines():
            exp = match(r'^([\w.]+)\s+([\w_-]+)\s+(\w+)\s+([:\w]+)\s+([.\w]+)\s+([.\w]+)$',
                        line, MULTILINE)

            if exp:
                stats.append(BirdStatus(peer=exp.group(1), is_up=exp.group(3)))

        return stats
