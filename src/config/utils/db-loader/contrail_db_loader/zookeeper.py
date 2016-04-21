# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
import logging
import uuid
import gevent

from kazoo.client import KazooClient
from kazoo.exceptions import NodeExistsError
from kazoo.handlers.gevent import SequentialGeventHandler
from kazoo.retry import KazooRetry


logger = logging.getLogger(__name__)


class ZookeeperClient(object):
    def __init__(self, server_list):
        self._retry = KazooRetry(max_tries=None, max_delay=300,
                                 sleep_func=gevent.sleep)
        self._zk_client = KazooClient(
            hosts=','.join(server_list),
            timeout=400,
            handler=SequentialGeventHandler(),
            logger=logger,
            connection_retry=self._retry,
            command_retry=self._retry)

    def connect(self):
        self._zk_client.start()

    def disconnect(self):
        self._zk_client.stop()
        self._zk_client.close()

    def create_node(self, path, value=None):
        if value is None:
            value = uuid.uuid4()
        try:
            self._zk_client.create(path, str(value), makepath=True)
        except NodeExistsError:
            self._zk_client.set(path, str(value))
