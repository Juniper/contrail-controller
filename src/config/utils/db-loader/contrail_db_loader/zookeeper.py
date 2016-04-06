# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
from kazoo.client import KazooClient
from kazoo.handlers.gevent import SequentialGeventHandler
import logging
import uuid
import threading
threading._DummyThread._Thread__stop = lambda x: 42

logger = logging.getLogger(__name__)


class ZookeeperClient(object):
    def __init__(self, server_list):
        self._zk_client = KazooClient(
            hosts=','.join(server_list),
            handler=SequentialGeventHandler())

    def connect(self):
        self._zk_client.start()

    def disconnect(self):
        self._zk_client.stop()

    def create_node(self, path, value=None):
        if value is None:
            value = uuid.uuid4()
        if self._zk_client.exists(path):
            current_value = self._zk_client.get(path)
            if current_value == value:
                return
            else:
                self._zk_client.set(path, str(value))
        else:
            self._zk_client.create(path, str(value), makepath=True)
