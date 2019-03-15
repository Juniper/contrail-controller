# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Infra, Inc. All rights reserved.

import mock
import socket
import unittest

from device_manager.dm_server \
    import initialize_amqp_client as dm_initialize_amqp_client
from device_manager.dm_server_args import parse_args as dm_parse_args
from device_manager.logger import DeviceManagerLogger
from pysandesh.sandesh_base import Sandesh

ETCD_HOST = 'etcd-host-01'


class TestKombuClient(unittest.TestCase):

    def setUp(self):
        self.args = dm_parse_args('')
        self.args.etcd_server = ETCD_HOST
        if 'host_ip' in self.args:
            self.host_ip = self.args.host_ip
        else:
            self.host_ip = socket.gethostbyname(socket.getfqdn())
            self.args.host_ip = self.host_ip
        self.logger = mock.Mock(
            name='DeviceManagerLogger', spec=DeviceManagerLogger)
        self.logger._sandesh = mock.Mock(name='Sandesh', spec=Sandesh)

    def test_kombu_rabbit_initialization(self):
        self.args.notification_driver = 'rabbit'
        self.args.db_driver = 'cassandra'
        amqp_client = dm_initialize_amqp_client(self.logger, self.args)
        self.assertIsNotNone(amqp_client)

    def test_kombu_etcd_initialization(self):
        self.args.notification_driver = 'etcd'
        self.args.db_driver = 'etcd'
        amqp_client = dm_initialize_amqp_client(self.logger, self.args)
        self.assertIsNotNone(amqp_client)


if __name__ == "__main__":
    unittest.main()
