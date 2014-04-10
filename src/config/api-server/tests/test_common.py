#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from flexmock import flexmock, Mock

import redis
from cfgm_common.test_utils import *
import cfgm_common.ifmap.client as ifmap_client
import cfgm_common.ifmap.response as ifmap_response
import discoveryclient.client as disc_client
from cfgm_common.zkclient import ZookeeperClient

import vnc_cfg_api_server


def launch_api_server(listen_ip, listen_port, http_server_port):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " % (listen_ip)
    args_str = args_str + "--listen_port %s " % (listen_port)
    args_str = args_str + "--http_server_port %s " % (http_server_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160"

    vnc_cfg_api_server.main(args_str)

#end launch_api_server


def setup_flexmock():
    flexmock(ifmap_client.client, __init__=FakeIfmapClient.initialize,
             call=FakeIfmapClient.call)
    flexmock(ifmap_response.Response, __init__=stub, element=stub)
    flexmock(ifmap_response.newSessionResult, get_session_id=stub)
    flexmock(ifmap_response.newSessionResult, get_publisher_id=stub)

    flexmock(pycassa.system_manager.Connection, __init__=stub)
    flexmock(pycassa.system_manager.SystemManager, create_keyspace=stub,
             create_column_family=stub)
    flexmock(pycassa.ConnectionPool, __init__=stub)
    flexmock(pycassa.ColumnFamily, __new__=FakeCF)
    flexmock(pycassa.util, convert_uuid_to_time=Fake_uuid_to_time)

    flexmock(disc_client.DiscoveryClient, __init__=stub)
    flexmock(disc_client.DiscoveryClient, publish_obj=stub)
    flexmock(ZookeeperClient, __new__=ZookeeperClientMock)

    flexmock(kombu.Connection, __new__=FakeKombu.Connection)
    flexmock(kombu.Exchange, __new__=FakeKombu.Exchange)
    flexmock(kombu.Queue, __new__=FakeKombu.Queue)
#end setup_flexmock
