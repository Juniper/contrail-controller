#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import resource
import socket
import fixtures
import subprocess
import uuid
from util import retry
from mockredis import mockredis
from mockkafka import mockkafka
from mockzoo import mockzoo
import redis
import copy
import os
import json
import gevent
import datetime
from fcntl import fcntl, F_GETFL, F_SETFL
from operator import itemgetter
from opserver_introspect_utils import VerificationOpsSrv, \
     VerificationOpsSrvIntrospect
from collector_introspect_utils import VerificationCollector
from alarmgen_introspect_utils import VerificationAlarmGen
from generator_introspect_utils import VerificationGenerator
from opserver.sandesh.viz.constants import COLLECTOR_GLOBAL_TABLE, SOURCE, MODULE
from opserver.opserver_util import OpServerUtils
from opserver.sandesh.alarmgen_ctrl.ttypes import UVEAlarmState
from sandesh_common.vns.constants import NodeTypeNames, ModuleNames
from sandesh_common.vns.ttypes import NodeType, Module
from pysandesh.util import UTCTimestampUsec
from pysandesh.sandesh_base import SandeshConfig
from sets import Set

class Query(object):
    table = None
    start_time = None
    end_time = None
    select_fields = None
    where = None
    sort = None
    sort_fields = None
    limit = None
    filter = None

    def __init__(self, table, start_time, end_time, select_fields, where = None,
            sort_fields = None, sort = None, limit = None, filter = None):
        self.table = table
        self.start_time = start_time
        self.end_time = end_time
        self.select_fields = select_fields
        if where is not None:
            self.where = where
        if sort_fields is not None:
            self.sort_fields = sort_fields
        if sort is not None:
            self.sort = sort
        if limit is not None:
            self.limit = limit
        if filter is not None:
            self.filter = filter

class Collector(object):
    def __init__(self, analytics_fixture, redis_uve, 
                 logger, ipfix_port = False, sflow_port = False,
                 syslog_port = False, protobuf_port = True,
                 kafka = None, is_dup = False,
                 cassandra_user = None, cassandra_password = None,
                 zookeeper = None, cluster_id='', sandesh_config=None):
        self.analytics_fixture = analytics_fixture
        if kafka is None:
            self.kafka_port = None
        else:
            self.kafka_port = kafka.port
        # If these ports are needed, "start" should allocate them
        self.syslog_port = 0 if syslog_port else -1
        self.ipfix_port = 0 if ipfix_port else -1
        self.sflow_port = 0 if sflow_port else -1

        self.protobuf_port = protobuf_port
        self.http_port = 0
        self.listen_port = 0
        self.hostname = socket.gethostname()
        self._instance = None
        self._redis_uve = redis_uve
        self._logger = logger
        self._is_dup = is_dup
        self.redis_password = None
        if self._redis_uve.password:
           self.redis_password = str(self._redis_uve.password)
        if self._is_dup is True:
            self.hostname = self.hostname+'dup'
        self.cassandra_user = analytics_fixture.cassandra_user
        self.cassandra_password = analytics_fixture.cassandra_password
        self.zk_port = zookeeper.port
        self._generator_id = None
        self.cluster_id = cluster_id
        self.sandesh_config = sandesh_config
    # end __init__

    def set_sandesh_config(self, sandesh_config):
        self.sandesh_config = sandesh_config
    # end set_sandesh_config

    def get_addr(self):
        return '127.0.0.1:'+str(self.listen_port)
    # end get_addr

    def get_syslog_port(self):
        return self.syslog_port
    # end get_syslog_port

    def get_protobuf_port(self):
        return self.protobuf_port
    # end get_protobuf_port

    def get_ipfix_port(self):
        return self.ipfix_port
    # end get_ipfix_port

    def get_sflow_port(self):
        return self.sflow_port
    # end get_sflow_port

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def get_redis_uve(self):
        return self._redis_uve
    # end get_redis_uve

    def start(self):
        assert(self._instance == None)
        self._log_file = '/tmp/vizd.messages.%s.%d' % \
                (os.getenv('USER', 'None'), self._redis_uve.port)
        subprocess.call(['rm', '-rf', self._log_file])
        if (self.ipfix_port == 0):
            self.ipfix_port = AnalyticsFixture.get_free_udp_port()
        if self.sflow_port == 0:
            self.sflow_port = AnalyticsFixture.get_free_udp_port()
        if (self.syslog_port == 0):
            self.syslog_port = AnalyticsFixture.get_free_port()
        args = [self.analytics_fixture.builddir + '/analytics/vizd',
            '--DEFAULT.cassandra_server_list', '127.0.0.1:' +
            str(self.analytics_fixture.cassandra_port),
            '--REDIS.port', 
            str(self._redis_uve.port),
            '--COLLECTOR.port', str(self.listen_port),
            '--DEFAULT.hostip', '127.0.0.1',
            '--DEFAULT.http_server_port', str(self.http_port),
            '--DEFAULT.syslog_port', str(self.syslog_port),
            '--DEFAULT.ipfix_port', str(self.ipfix_port),
            '--DEFAULT.sflow_port', str(self.sflow_port),
            '--DEFAULT.log_level', 'SYS_DEBUG',
            '--DEFAULT.log_file', self._log_file,
            '--DATABASE.enable_message_keyword_writes']
        if self.redis_password:
            args.append('--REDIS.password')
            args.append(self.redis_password)
        if self._is_dup is True:
            args.append('--DEFAULT.dup')
        if (self.protobuf_port):
            self.protobuf_port = AnalyticsFixture.get_free_port()
            args.append('--COLLECTOR.protobuf_port')
            args.append(str(self.protobuf_port))
        else:
            self.protobuf_port = None
        if self.cassandra_user is not None and \
           self.cassandra_password is not None:
               args.append('--CASSANDRA.cassandra_user')
               args.append(self.cassandra_user)
               args.append('--CASSANDRA.cassandra_password')
               args.append(self.cassandra_password)
        if self.kafka_port:
            args.append('--DEFAULT.kafka_broker_list')
            args.append('127.0.0.1:%d' % self.kafka_port)        
            args.append('--DEFAULT.partitions')
            args.append(str(4))
        args.append('--DEFAULT.zookeeper_server_list')
        args.append('127.0.0.1:%d' % self.zk_port)
        if self.cluster_id:
            args.append('--DATABASE.cluster_id')
            args.append(self.cluster_id)
        if self.sandesh_config:
            if 'sandesh_ssl_enable' in self.sandesh_config:
                args.append('--SANDESH.sandesh_ssl_enable')
                args.append(self.sandesh_config['sandesh_ssl_enable'])
            if 'introspect_ssl_enable' in self.sandesh_config:
                args.append('--SANDESH.introspect_ssl_enable')
                args.append(self.sandesh_config['introspect_ssl_enable'])
            if 'sandesh_keyfile' in self.sandesh_config:
                args.append('--SANDESH.sandesh_keyfile')
                args.append(self.sandesh_config['sandesh_keyfile'])
            if 'sandesh_certfile' in self.sandesh_config:
                args.append('--SANDESH.sandesh_certfile')
                args.append(self.sandesh_config['sandesh_certfile'])
            if 'sandesh_ca_cert' in self.sandesh_config:
                args.append('--SANDESH.sandesh_ca_cert')
                args.append(self.sandesh_config['sandesh_ca_cert'])
        self._logger.info('Setting up Vizd: %s' % (' '.join(args))) 
        ports, self._instance = \
                         self.analytics_fixture.start_with_ephemeral_ports(
                         "contrail-collector", ["http","collector"],
                         args, AnalyticsFixture.enable_core)
        self._generator_id = self.hostname+':'+NodeTypeNames[NodeType.ANALYTICS]+\
                            ':'+ModuleNames[Module.COLLECTOR]+':'+str(self._instance.pid)
        self.http_port = ports["http"]
        self.listen_port = ports["collector"]
        return self.verify_setup()
    # end start

    def verify_setup(self):
        if not self.http_port:
            return False
        if not self.listen_port:
            return False
        return True

    def stop(self):
        if self._instance is not None:
            rcode = self.analytics_fixture.process_stop(
                "contrail-collector:%s" % str(self.listen_port),
                self._instance, self._log_file)
            #assert(rcode == 0)
            self._instance = None
    # end stop

# end class Collector

class AlarmGen(object):
    def __init__(self, collectors, kafka_port,
                 analytics_fixture, logger, zoo, is_dup=False,
                 sandesh_config=None):
        self.collectors = collectors
        self.analytics_fixture = analytics_fixture
        self.http_port = 0
        self.kafka_port = kafka_port
        self._zoo = zoo
        self.hostname = socket.gethostname()
        self.sandesh_config = sandesh_config
        self._instance = None
        self._logger = logger
        self._generator_id = self.hostname+':'+NodeTypeNames[NodeType.ANALYTICS]+\
                            ':'+ModuleNames[Module.ALARM_GENERATOR]+':0'
        self.redis_password = None
        if self.analytics_fixture.redis_uves[0].password:
           self.redis_password = str(self.analytics_fixture.redis_uves[0].password)
    # end __init__

    def set_sandesh_config(self, sandesh_config):
        self.sandesh_config = sandesh_config
    # end set_sandesh_config

    def get_introspect(self):
        if self.http_port != 0:
            return VerificationAlarmGen("127.0.0.1", self.http_port)
        else:
            return None

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def start(self):
        assert(self._instance == None)
        self._log_file = '/tmp/alarmgen.messages.%s.%d' % \
                (os.getenv('USER', 'None'), os.getpid())
        subprocess.call(['rm', '-rf', self._log_file])
        args = ['contrail-alarm-gen',
                '--http_server_port', str(self.http_port),
                '--log_file', self._log_file,
                '--log_level', 'SYS_DEBUG',
                '--redis_server_port',
                str(self.analytics_fixture.redis_uves[0].port)]
        if self.kafka_port:
            args.append('--kafka_broker_list')
            args.append('127.0.0.1:' + str(self.kafka_port))
        args.append('--redis_uve_list') 
        for redis_uve in self.analytics_fixture.redis_uves:
            args.append('127.0.0.1:'+str(redis_uve.port))
        args.append('--collectors')
        for collector in self.collectors:
            args.append(collector)
        if self.redis_password is not None:
            args.append('--redis_password')
            args.append(self.redis_password)
        part = "0"
        if self._zoo is not None:
            part = "4"
            args.append('--zk_list')
            args.append('127.0.0.1:'+str(self._zoo))
        args.append('--partitions')
        args.append(part)
        if self.sandesh_config:
            if 'sandesh_ssl_enable' in self.sandesh_config and \
                self.sandesh_config['sandesh_ssl_enable']:
                args.append('--sandesh_ssl_enable')
            if 'introspect_ssl_enable' in self.sandesh_config and \
                self.sandesh_config['introspect_ssl_enable']:
                args.append('--introspect_ssl_enable')
            if 'sandesh_keyfile' in self.sandesh_config:
                args.append('--sandesh_keyfile')
                args.append(self.sandesh_config['sandesh_keyfile'])
            if 'sandesh_certfile' in self.sandesh_config:
                args.append('--sandesh_certfile')
                args.append(self.sandesh_config['sandesh_certfile'])
            if 'sandesh_ca_cert' in self.sandesh_config:
                args.append('--sandesh_ca_cert')
                args.append(self.sandesh_config['sandesh_ca_cert'])

        self._logger.info('Setting up AlarmGen: %s' % ' '.join(args))
        ports, self._instance = \
                         self.analytics_fixture.start_with_ephemeral_ports(
                         "contrail-alarm-gen", ["http"],
                         args, None, False)
        self.http_port = ports["http"]
        return self.verify_setup()
    # end start

    def verify_setup(self):
        if not self.http_port:
            return False
        if self._zoo is not None:
            for part in range(0,4):
                if not self.analytics_fixture.verify_alarmgen_partition(\
                        part,'true'):
                    return False
        return True

    def stop(self):
        if self._instance is not None:
            rcode = self.analytics_fixture.process_stop(
                "contrail-alarm-gen:%s" % str(self.http_port),
                self._instance, self._log_file, is_py=False, del_log=False)
            self._instance = None
    # end stop

    # TODO : PartitionOwnershipReq, PartitionStatusReq

# end class AlarmGen

class OpServer(object):
    def __init__(self, collectors, analytics_fixture, logger,
                 admin_user, admin_password, zoo=None, is_dup=False,
                 sandesh_config=None):
        self.collectors = collectors
        self.analytics_fixture = analytics_fixture
        self.http_port = 0
        self.hostname = socket.gethostname()
        self._zoo = zoo
        self._instance = None
        self._logger = logger
        self._is_dup = is_dup
        self.redis_password = None
        if self.analytics_fixture.redis_uves[0].password:
           self.redis_password = str(self.analytics_fixture.redis_uves[0].password)
        if self._is_dup is True:
            self.hostname = self.hostname+'dup'
        self._generator_id = self.hostname+':'+NodeTypeNames[NodeType.ANALYTICS]+\
                            ':'+ModuleNames[Module.OPSERVER]+':0'
        self.rest_api_port = AnalyticsFixture.get_free_port()
        self.admin_port = AnalyticsFixture.get_free_port()
        self.admin_user = admin_user
        self.admin_password = admin_password
        self.sandesh_config = sandesh_config
    # end __init__

    def set_sandesh_config(self, sandesh_config):
        self.sandesh_config = sandesh_config
    # end set_sandesh_config

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def start(self):
        assert(self._instance == None)
        self._log_file = '/tmp/opserver.messages.%s.%d' % \
                (os.getenv('USER', 'None'), self.admin_port)
        subprocess.call(['rm', '-rf', self._log_file])
        args = ['contrail-analytics-api',
                '--redis_query_port',
                str(self.analytics_fixture.redis_uves[0].port),
                '--cassandra_server_list', '127.0.0.1:' +
                str(self.analytics_fixture.cassandra_port),
                '--http_server_port', str(self.http_port),
                '--log_file', self._log_file,
                '--log_level', "SYS_INFO",
                '--rest_api_port', str(self.rest_api_port),
                '--admin_port', str(self.admin_port),
                '--admin_user', self.admin_user,
                '--admin_password', self.admin_password]
        part = "0"
        if self._zoo is not None:
            part = "4"
            args.append('--zk_list')
            args.append('127.0.0.1:'+str(self._zoo))
        args.append('--partitions')
        args.append(part)
        if self.analytics_fixture.redis_uves[0].password:
            args.append('--redis_password')
            args.append(self.analytics_fixture.redis_uves[0].password)
        args.append('--redis_uve_list') 
        for redis_uve in self.analytics_fixture.redis_uves:
            args.append('127.0.0.1:'+str(redis_uve.port))
        args.append('--collectors')
        for collector in self.collectors:
            args.append(collector)
        if self._is_dup:
            args.append('--dup')

        if self.analytics_fixture.cassandra_user is not None:
            args.append('--cassandra_user')
            args.append(self.analytics_fixture.cassandra_user)
        if self.analytics_fixture.cassandra_password is not None:
            args.append('--cassandra_password')
            args.append(self.analytics_fixture.cassandra_password)
        if self.analytics_fixture.cluster_id:
            args.append('--cluster_id')
            args.append(self.analytics_fixture.cluster_id)
        if self.sandesh_config:
            if 'sandesh_ssl_enable' in self.sandesh_config and \
                self.sandesh_config['sandesh_ssl_enable']:
                args.append('--sandesh_ssl_enable')
            if 'introspect_ssl_enable' in self.sandesh_config and \
                self.sandesh_config['introspect_ssl_enable']:
                args.append('--introspect_ssl_enable')
            if 'sandesh_keyfile' in self.sandesh_config:
                args.append('--sandesh_keyfile')
                args.append(self.sandesh_config['sandesh_keyfile'])
            if 'sandesh_certfile' in self.sandesh_config:
                args.append('--sandesh_certfile')
                args.append(self.sandesh_config['sandesh_certfile'])
            if 'sandesh_ca_cert' in self.sandesh_config:
                args.append('--sandesh_ca_cert')
                args.append(self.sandesh_config['sandesh_ca_cert'])
        self._logger.info('Setting up OpServer: %s' % ' '.join(args))
        ports, self._instance = \
                         self.analytics_fixture.start_with_ephemeral_ports(
                         "contrail-analytics-api", ["http"],
                         args, None, True)
        self.http_port = ports["http"]
        return self.verify_setup()
    # end start

    def verify_setup(self):
        if not self.http_port:
            return False
        return True

    def stop(self):
        if self._instance is not None:
            rcode = self.analytics_fixture.process_stop(
                "contrail-analytics-api:%s" % str(self.admin_port),
                self._instance, self._log_file, is_py=True)
            #assert(rcode == 0)
            self._instance = None
    # end stop

    def send_tracebuffer_request(self, src, mod, instance, tracebuf):
        vns = VerificationOpsSrv('127.0.0.1', self.admin_port,
            self.admin_user, self.admin_password)
        res = vns.send_tracebuffer_req(src, mod, instance, tracebuf)
        self._logger.info('send_tracebuffer_request: %s' % (str(res)))
        assert(res['status'] == 'pass')
    # end send_tracebuffer_request

# end class OpServer

class QueryEngine(object):
    def __init__(self, collectors, analytics_fixture, logger, cluster_id='',
                 sandesh_config=None):
        self.collectors = collectors
        self.analytics_fixture = analytics_fixture
        self.listen_port = AnalyticsFixture.get_free_port()
        self.http_port = 0
        self.hostname = socket.gethostname()
        self._instance = None
        self._logger = logger
        self.redis_password = None
        self.cassandra_user = self.analytics_fixture.cassandra_user
        self.cassandra_password = self.analytics_fixture.cassandra_password
        if self.analytics_fixture.redis_uves[0].password:
           self.redis_password = str(self.analytics_fixture.redis_uves[0].password) 
        self._generator_id = self.hostname+':'+NodeTypeNames[NodeType.ANALYTICS]+\
                            ':'+ModuleNames[Module.QUERY_ENGINE]+':0'
        self.cluster_id = cluster_id
        self.sandesh_config = sandesh_config
    # end __init__

    def set_sandesh_config(self, sandesh_config):
        self.sandesh_config = sandesh_config
    # end set_sandesh_config

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def start(self, analytics_start_time=None):
        assert(self._instance == None)
        self._log_file = '/tmp/qed.messages.%s.%d' % \
                (os.getenv('USER', 'None'), self.listen_port)
        subprocess.call(['rm', '-rf', self._log_file])
        args = [self.analytics_fixture.builddir + '/query_engine/qedt',
                '--REDIS.port', str(self.analytics_fixture.redis_uves[0].port),
                '--DEFAULT.cassandra_server_list', '127.0.0.1:' +
                str(self.analytics_fixture.cassandra_port),
                '--DEFAULT.http_server_port', str(self.listen_port),
                '--DEFAULT.log_local', '--DEFAULT.log_level', 'SYS_DEBUG',
                '--DEFAULT.log_file', self._log_file]
        for collector in self.collectors:
            args += ['--DEFAULT.collectors', collector]
        if self.redis_password:
            args.append('--REDIS.password')
            args.append(self.redis_password)
        if analytics_start_time is not None:
            args += ['--DEFAULT.start_time', str(analytics_start_time)]
        if self.cassandra_user is not None:
            args += ['--CASSANDRA.cassandra_user', self.cassandra_user]
        if self.cassandra_password is not None:
            args += ['--CASSANDRA.cassandra_password', self.cassandra_password]
        if self.cluster_id:
            args += ['--DATABASE.cluster_id', self.cluster_id]
        if self.sandesh_config:
            if 'sandesh_ssl_enable' in self.sandesh_config:
                args.append('--SANDESH.sandesh_ssl_enable')
                args.append(self.sandesh_config['sandesh_ssl_enable'])
            if 'introspect_ssl_enable' in self.sandesh_config:
                args.append('--SANDESH.introspect_ssl_enable')
                args.append(self.sandesh_config['introspect_ssl_enable'])
            if 'sandesh_keyfile' in self.sandesh_config:
                args.append('--SANDESH.sandesh_keyfile')
                args.append(self.sandesh_config['sandesh_keyfile'])
            if 'sandesh_certfile' in self.sandesh_config:
                args.append('--SANDESH.sandesh_certfile')
                args.append(self.sandesh_config['sandesh_certfile'])
            if 'sandesh_ca_cert' in self.sandesh_config:
                args.append('--SANDESH.sandesh_ca_cert')
                args.append(self.sandesh_config['sandesh_ca_cert'])
        self._logger.info('Setting up contrail-query-engine: %s' % ' '.join(args))
        ports, self._instance = \
                         self.analytics_fixture.start_with_ephemeral_ports(
                         "contrail-query-engine", ["http"],
                         args, AnalyticsFixture.enable_core)
        self.http_port = ports["http"]
        return self.verify_setup()
    # end start

    def verify_setup(self):
        if not self.http_port:
            return False
        return True

    def stop(self):
        if self._instance is not None:
            rcode = self.analytics_fixture.process_stop(
                "contrail-query-engine:%s" % str(self.listen_port),
                self._instance, self._log_file, False)
            #assert(rcode == 0)
            self._instance = None
    # end stop

# end class QueryEngine

class Redis(object):
    def __init__(self, builddir, password=None):
        self.builddir = builddir
        self.port = AnalyticsFixture.get_free_port()
        self.password = password
        self.running = False
    # end __init__

    def start(self):
        assert(self.running == False)
        self.running = True
        ret = mockredis.start_redis(self.port, self.password)
        return(ret)

    # end start
    def stop(self):
        if self.running:
            mockredis.stop_redis(self.port, self.password)
            self.running =  False
    #end stop

# end class Redis

class Kafka(object):
    def __init__(self, zk_port):
        self.port = None
        self.running = False
        self.zk_port = zk_port
    # end __init__

    def start(self):
        assert(self.running == False)
        self.running = True
        if not self.port:
            self.port = AnalyticsFixture.get_free_port()
        mockkafka.start_kafka(self.zk_port, self.port)

    # end start

    def stop(self):
        if self.running:
            mockkafka.stop_kafka(self.port)
            self.running =  False
    #end stop
# end class Kafka

class Zookeeper(object):
    def __init__(self, zk_port=None):
        self.running = False;
        self.port = zk_port
    # end __init__

    def start(self):
        assert(self.running == False)
        if not self.port:
            self.port = AnalyticsFixture.get_free_port()
        mockzoo.start_zoo(self.port)
        self.running = True
    # end start

    def stop(self):
        if self.running:
            mockzoo.stop_zoo(self.port)
        self.running = False
    # end stop
# end class Zookeeper

class AnalyticsFixture(fixtures.Fixture):
    ADMIN_USER = 'test'
    ADMIN_PASSWORD = 'password'

    def set_sandesh_config(self, sandesh_config):
        self.sandesh_config = sandesh_config
        self.sandesh_config_struct = None
        if sandesh_config:
            self.sandesh_config_struct = SandeshConfig(
                sandesh_config.get('sandesh_keyfile'),
                sandesh_config.get('sandesh_certfile'),
                sandesh_config.get('sandesh_ca_cert'),
                sandesh_config.get('sandesh_ssl_enable', False),
                sandesh_config.get('introspect_ssl_enable', False))
    # end set_sandesh_config

    def __init__(self, logger, builddir, cassandra_port,
                 ipfix_port = False, sflow_port = False, syslog_port = False,
                 protobuf_port = False, noqed=False, collector_ha_test=False,
                 redis_password=None, start_kafka=False,
                 cassandra_user=None, cassandra_password=None, cluster_id="",
                 sandesh_config=None):

        self.builddir = builddir
        self.cassandra_port = cassandra_port
        self.ipfix_port = ipfix_port
        self.sflow_port = sflow_port
        self.syslog_port = syslog_port
        self.protobuf_port = protobuf_port
        self.logger = logger
        self.noqed = noqed
        self.collector_ha_test = collector_ha_test
        self.redis_password = redis_password
        self.start_kafka = start_kafka
        self.kafka = None
        self.opserver = None
        self.query_engine = None
        self.alarmgen = None
        self.cassandra_user = cassandra_user
        self.cassandra_password = cassandra_password
        self.zookeeper = None
        self.admin_user = AnalyticsFixture.ADMIN_USER
        self.admin_password = AnalyticsFixture.ADMIN_PASSWORD
        self.cluster_id = cluster_id
        self.set_sandesh_config(sandesh_config)

    def setUp(self):
        super(AnalyticsFixture, self).setUp()

        self.redis_uves = [Redis(self.builddir,
                                 self.redis_password)]
        self.redis_uves[0].start()

        self.zookeeper = Zookeeper()
        self.zookeeper.start()

        zkport = None
        if self.start_kafka:
            zkport = self.zookeeper.port
            self.kafka = Kafka(zkport)
            self.kafka.start()

        self.collectors = [Collector(self, self.redis_uves[0], self.logger,
                           ipfix_port = self.ipfix_port,
                           sflow_port = self.sflow_port,
                           syslog_port = self.syslog_port,
                           protobuf_port = self.protobuf_port,
                           kafka = self.kafka,
                           zookeeper = self.zookeeper,
                           cluster_id=self.cluster_id,
                           sandesh_config=self.sandesh_config)]
        if not self.collectors[0].start():
            self.logger.error("Collector did NOT start")
            return 

        if not self.verify_collector_gen(self.collectors[0]):
            self.logger.error("Collector UVE not in Redis")
            return

        if self.collector_ha_test:
            self.redis_uves.append(Redis(self.builddir,
                                         self.redis_password))
            self.redis_uves[1].start()
            self.collectors.append(Collector(self, self.redis_uves[1],
                                             self.logger,
                                             kafka = self.kafka,
                                             is_dup = True,
                                             zookeeper = self.zookeeper,
                                             cluster_id=self.cluster_id,
                                             sandesh_config=self.sandesh_config))
            if not self.collectors[1].start():
                self.logger.error("Second Collector did NOT start")

        self.opserver = OpServer(self.get_collectors(),
                                 self, self.logger, self.admin_user,
                                 self.admin_password, zkport,
                                 sandesh_config=self.sandesh_config)
        if not self.opserver.start():
            self.logger.error("OpServer did NOT start")
        self.opserver_port = self.get_opserver_port()
        
        if self.kafka is not None: 
            self.alarmgen = AlarmGen(self.get_collectors(), self.kafka.port,
                                     self, self.logger, zkport,
                                     sandesh_config=self.sandesh_config)
            if not self.alarmgen.start():
                self.logger.error("AlarmGen did NOT start")

        if not self.noqed:
            self.query_engine = QueryEngine(self.get_collectors(),
                                            self, self.logger,
                                            cluster_id=self.cluster_id,
                                            sandesh_config=self.sandesh_config)
            if not self.query_engine.start():
                self.logger.error("QE did NOT start")
    # end setUp

    def get_collector(self):
        return '127.0.0.1:'+str(self.collectors[0].listen_port)
    # end get_collector

    def get_collectors(self):
        collector_ips = []
        for collector in self.collectors:
            collector_ips.append('127.0.0.1:'+str(collector.listen_port))
        return collector_ips
    # end get_collectors

    def get_opserver_port(self):
        return self.opserver.admin_port
    # end get_opserver_port

    def get_generator_list(self, collector):
        generator_list = []
        vcl = VerificationCollector('127.0.0.1', collector.http_port, \
                self.sandesh_config_struct)
        try:
           genlist = vcl.get_generators()['generators']
           self.logger.info('Generator list from collector %s -> %s' %
               (collector.hostname, str(genlist)))
           for gen in genlist:
               if gen['state'] != 'Established':
                   continue
               generator_list.append('%s:%s:%s:%s' % (gen['source'],
                   gen['node_type'], gen['module_id'], gen['instance_id']))
        except Exception as err:
            self.logger.error('Failed to get generator list: %s' % err)
        return generator_list
    # end get_generator_list

    def verify_on_setup(self):
        result = True
        if self.opserver is None:
            result = result and False
            self.logger.error("AnalyticsAPI not functional without OpServer")
            return result
        if not self.noqed:
            if not self.query_engine:
                result = result and False
                self.logger.error("AnalyticsAPI not functional without QE")
        if self.kafka is not None:
            if self.alarmgen is None:
                result = result and False
                self.logger.error("Analytics did not start AlarmGen")
            else:
                if not self.alarmgen.verify_setup():
                    result = result and False
                    self.logger.error("Analytics AlarmGen startup failed")
        if not self.verify_opserver_api():
            result = result and False
            self.logger.error("AnalyticsAPI not responding")
        self.verify_is_run = True
        return result

    @retry(delay=2, tries=20)
    def verify_collector_gen(self, collector):
        '''
        See if the SandeshClient within vizd has been able to register
        with the collector within vizd
        '''
        vcl = VerificationCollector('127.0.0.1', collector.http_port, \
                self.sandesh_config_struct)
        self.logger.info("verify_collector_gen port %s : %s" % \
            (collector.http_port, str(vcl)))
        try:
            genlist = vcl.get_generators()['generators']
            src = genlist[0]['source']
        except:
            return False

        self.logger.info("Src Name is %s" % src)
        if src == socket.gethostname():
            return True
        else:
            return False

    @retry(delay=1, tries=10)
    def verify_opserver_api(self):
        '''
        Verify that the opserver is accepting client requests
        '''
        data = {}
        url = 'http://127.0.0.1:' + str(self.opserver_port) + '/'
        data = OpServerUtils.get_url_http(url, self.admin_user,
            self.admin_password)
        self.logger.info("Checking OpServer %s" % str(data))
        if data == {}:
            return False
        else:
            return True

    def set_alarmgen_partition(self, part, own):
        vag = self.alarmgen.get_introspect()
        ret = vag.get_PartitionOwnership(part,own)
        self.logger.info("set_alarmgen_partition %d %d : %s" % \
                         (part,own,str(ret)))
        if ret == []:
            return False
        return ret['status']

    @retry(delay=2, tries=5)
    def verify_alarmgen_partition(self, part, own, uves = None):
        vag = self.alarmgen.get_introspect()
        ret = vag.get_PartitionStatus(part)
        self.logger.info("verify_alarmgen_partition %d : %s" % \
                         (part, str(ret)))
        if ret == []:
            return False
        if ret['enabled'] == own:
            return True
        else:
            return False
      
    @retry(delay=2, tries=5)
    def verify_uvetable_alarm(self, table, name, type, is_set=True, rules=None):
        vag = self.alarmgen.get_introspect()
        ret = vag.get_UVETableAlarm(table)
        self.logger.info("verify_uvetable_alarm %s, %s, %s [%s]: %s" % \
                         (table, str(name), str(type), str(is_set), str(ret)))
        if not len(ret):
            return False
        if not ret.has_key('uves'):
            return False
        uves = ret['uves']
        if not name:
            if not uves:
                return True
            if not len(uves):
                return True
        if not uves:
            uves = []
        alarms = {}
	for uve in uves:
	    elem = uve['uai']['UVEAlarms']
            if name and elem['name'] != name:
                continue
	    #alarms in Idle state should not be counted
	    alarm_state = int(uve['uas']['UVEAlarmOperState']['state'])
	    if (alarm_state == UVEAlarmState.Idle):
		continue
            for alm in elem['alarms']:
                if len(alm['alarm_rules']):
                    alarms[alm['type']] = alm['alarm_rules']['AlarmRules']['or_list']
        if type in alarms:
            if is_set:
                if rules:
                    if len(rules) != len(alarms[type]):
                        return False
                    agg1 = 0
                    agg2 = 0
                    for idx in range(0,len(rules)):
                        agg1 += len(rules[idx]['and_list'])
                        agg2 += len(alarms[type][idx]['and_list'])
                    if agg1 != agg2:
                        return False
            return is_set
        else:
	    if not name:
		return True
            return not is_set

    @retry(delay=2, tries=10)
    def verify_collector_obj_count(self):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query('ObjectCollectorInfo',
                             start_time='-10m', end_time='now',
                             select_fields=["ObjectLog"],
                             where_clause=str(
                                 'ObjectId=' + socket.gethostname()),
                             sync=False)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            self.logger.info(str(res))
            return True

    @retry(delay=1, tries=30)
    def verify_generator_list(self, collectors, exp_genlist):
        actual_genlist = []
        for collector in collectors:
            actual_genlist.extend(self.get_generator_list(collector))
        self.logger.info('generator list: ' + str(set(actual_genlist)))
        self.logger.info('exp generator list: ' + str(set(exp_genlist)))
        return set(actual_genlist) == set(exp_genlist)

    @retry(delay=1, tries=10)
    def verify_generator_uve_list(self, exp_gen_list):
        self.logger.info('verify_generator_uve_list')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # get generator list
        gen_list = vns.uve_query('generators',
            {'cfilt':'ModuleClientState:client_info'})
        try:
            actual_gen_list = [gen['name'] for gen in gen_list]
            self.logger.info('generators: %s' % str(actual_gen_list))
            for gen in exp_gen_list:
                if gen not in actual_gen_list:
                    return False
        except Exception as e:
            self.logger.error('Exception: %s' % e)
            return False
        return True
    # end verify_generator_uve_list

    @retry(delay=1, tries=6)
    def verify_message_table_messagetype(self):
        self.logger.info("verify_message_table_messagetype")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # query for CollectorInfo logs
        res = vns.post_query('MessageTable',
                             start_time='-10m', end_time='now',
                             select_fields=["ModuleId"],
                             where_clause="Messagetype = CollectorInfo")
        if (res == []):
            return False
        assert(len(res) > 0)

        # verify if the result returned is ok
        moduleids = list(set(x['ModuleId'] for x in res))
        self.logger.info("Modules: %s " % str(moduleids))
        # only one moduleid: Collector
        if (not((len(moduleids) == 1))):
            return False
        if (not ("contrail-collector" in moduleids)):
            return False
        return True

    @retry(delay=1, tries=6)
    def verify_message_table_select_uint_type(self):
        self.logger.info("verify_message_table_select_uint_type")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # query for CollectorInfo logs
        res = vns.post_query('MessageTable',
                             start_time='-10m', end_time='now',
                             select_fields=["Level", "Type", "MessageTS", "SequenceNum"],
                             where_clause='')
	if (res == []):
            return False
	else:
	    for x in res:
	        assert('Level' in x)
		assert('Type' in x)
		assert('MessageTS' in x)
		assert('SequenceNum' in x)
	    	assert(type(x['Level']) is int)
	    	assert(type(x['Type']) is int)
	    	assert(type(x['MessageTS']) is int)
	    	assert(type(x['SequenceNum']) is int)
	    return True
    
    @retry(delay=1, tries=6)
    def verify_message_table_moduleid(self):
        self.logger.info("verify_message_table_moduleid")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # query for contrail-query-engine logs
        res_qe = vns.post_query('MessageTable',
                                start_time='-10m', end_time='now',
                                select_fields=["Type", "Messagetype"],
                                where_clause="ModuleId = contrail-query-engine")
        # query for Collector logs
        res_c = vns.post_query('MessageTable',
                               start_time='-10m', end_time='now',
                               select_fields=["Type", "Messagetype"],
                               where_clause="ModuleId = contrail-collector")
        if (res_qe == []) or (res_c == []):
            return False
        assert(len(res_qe) > 0)
        assert(len(res_c) > 0)
        return True

    @retry(delay=1, tries=6)
    def verify_message_table_where_or(self):
        self.logger.info("verify_message_table_where_or")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        where_clause1 = "ModuleId = contrail-query-engine"
        where_clause2 = str("Source =" + socket.gethostname())
        res = vns.post_query(
            'MessageTable',
            start_time='-10m', end_time='now',
            select_fields=["ModuleId"],
            where_clause=str(where_clause1 + " OR  " + where_clause2))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            if ('contrail-collector' in moduleids) and ('contrail-query-engine' in moduleids):
                return True
            else:
                return False

    @retry(delay=1, tries=6)
    def verify_message_table_where_and(self):
        self.logger.info("verify_message_table_where_and")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        where_clause1 = "ModuleId = contrail-query-engine"
        where_clause2 = str("Source =" + socket.gethostname())
        res = vns.post_query(
            'MessageTable',
            start_time='-10m', end_time='now',
            select_fields=["ModuleId"],
            where_clause=str(where_clause1 + " AND  " + where_clause2))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            if len(moduleids) == 1:  # 1 moduleid: contrail-query-engine
                return True
            else:
                return False

    @retry(delay=1, tries=6)
    def verify_message_table_where_prefix(self):
        self.logger.info('verify_message_table_where_prefix')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        prefix_key_value_map = {'Source': socket.gethostname()[:-1],
            'ModuleId': 'contrail-', 'Messagetype': 'Collector'}
        for key, value in prefix_key_value_map.iteritems():
            self.logger.info('verify where_prefix: %s = %s*' % (key, value))
            res = vns.post_query('MessageTable', start_time='-10m',
                    end_time='now', select_fields=[key],
                    where_clause='%s = %s*' % (key, value))
            if not len(res):
                return False
            self.logger.info(str(res))
            for r in res:
                assert(r[key].startswith(value))
        return True
    # end verify_message_table_where_prefix

    @retry(delay=1, tries=6)
    def verify_message_table_filter(self):
        self.logger.info("verify_message_table_where_filter")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        where_clause1 = "ModuleId = contrail-query-engine"
        where_clause2 = str("Source =" + socket.gethostname())
        res = vns.post_query('MessageTable',
                             start_time='-10m', end_time='now',
                             select_fields=["ModuleId"],
                             where_clause=str(
                                 where_clause1 + " OR  " + where_clause2),
                             filter="ModuleId = contrail-query-engine")
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            if len(moduleids) != 1:  # 1 moduleid: contrail-collector
                return False

        res1 = vns.post_query('MessageTable',
                              start_time='-10m', end_time='now',
                              select_fields=["ModuleId"],
                              where_clause=str(
                                  where_clause1 + " AND  " + where_clause2),
                              filter="ModuleId = contrail-collector")
        self.logger.info(str(res1))
        if res1 != []:
            return False
        return True

    @retry(delay=1, tries=6)
    def verify_message_table_filter2(self):
        self.logger.info("verify_message_table_filter2")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        a_query = Query(table="MessageTable",
                start_time='now-10m',
                end_time='now',
                select_fields=["ModuleId"],
                filter=[[{"name": "ModuleId", "value": "contrail-collector", "op": 1}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            assert(len(moduleids) == 1 and "contrail-collector" in moduleids)

        a_query = Query(table="MessageTable",
                start_time='now-10m',
                end_time='now',
                select_fields=["ModuleId"],
                filter=[[{"name": "ModuleId", "value": "contrail-collector", "op": 1}], [{"name": "ModuleId", "value": "contrail-analytics-api", "op": 1}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            assert(len(moduleids) == 2 and "contrail-collector" in moduleids and "contrail-analytics-api" in moduleids)  # 1 moduleid: contrail-collector || contrail-analytics-api
                
        return True

    @retry(delay=1, tries=1)
    def verify_message_table_sort(self):
        self.logger.info("verify_message_table_sort:Ascending Sort")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        where_clause1 = "ModuleId = contrail-query-engine"
        where_clause2 = str("Source =" + socket.gethostname())

        exp_moduleids = ['contrail-analytics-api',
                         'contrail-collector', 'contrail-query-engine']

        # Ascending sort
        res = vns.post_query('MessageTable',
                             start_time='-10m', end_time='now',
                             select_fields=["ModuleId"],
                             where_clause=str(
                                 where_clause1 + " OR  " + where_clause2),
                             sort_fields=["ModuleId"], sort=1)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = []
            for x in res:
                if x['ModuleId'] not in moduleids:
                    moduleids.append(x['ModuleId'])
            self.logger.info(str(moduleids))
            for module in exp_moduleids:
                if module not in moduleids:
                    return False
            expected_res = sorted(res, key=itemgetter('ModuleId'))
            if res != expected_res:
                return False

        # Descending sort
        self.logger.info("verify_message_table_sort:Descending Sort")
        res = vns.post_query('MessageTable',
                             start_time='-10m', end_time='now',
                             select_fields=["ModuleId"],
                             where_clause=str(
                                 where_clause1 + " OR  " + where_clause2),
                             sort_fields=["ModuleId"], sort=2)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = []
            for x in res:
                if x['ModuleId'] not in moduleids:
                    moduleids.append(x['ModuleId'])
            self.logger.info(str(moduleids))
            for module in exp_moduleids:
                if module not in moduleids:
                    return False
            expected_res = sorted(
                res, key=itemgetter('ModuleId'), reverse=True)
            if res != expected_res:
                return False

        # sort + limit
        res = vns.post_query('MessageTable',
                             start_time='-10m', end_time='now',
                             select_fields=["ModuleId"],
                             where_clause=str(
                                 where_clause1 + " OR  " + where_clause2),
                             sort_fields=["ModuleId"], sort=1, limit=1)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = []
            for x in res:
                if x['ModuleId'] not in moduleids:
                    moduleids.append(x['ModuleId'])
            self.logger.info(str(moduleids))
            if len(moduleids) == 1: 
                if moduleids[0] != 'contrail-analytics-api':
                    return False
                return True
            else:
                return False

    def verify_message_table_limit(self):
        self.logger.info("verify_message_table_limit")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query('MessageTable',
                             start_time='-10m', end_time='now',
                             select_fields=['ModuleId', 'Messagetype'],
                             where_clause='', limit=1)
        self.logger.info(str(res))
        assert(len(res) == 1)
        return True
    # end verify_message_table_limit

    @retry(delay=1, tries=8)
    def verify_intervn_all(self, gen_obj):
        self.logger.info("verify_intervn_all")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query('StatTable.UveVirtualNetworkAgent.vn_stats',
                             start_time='-10m',
                             end_time='now',
                             select_fields=['T', 'name', 'UUID','vn_stats.other_vn', 'vn_stats.vrouter', 'vn_stats.in_tpkts'],
                             where_clause=gen_obj.vn_all_rows['whereclause'])
        self.logger.info(str(res))
        if len(res) == gen_obj.vn_all_rows['rows']:
            return True
        return False      

    @retry(delay=1, tries=8)
    def verify_intervn_sum(self, gen_obj):
        self.logger.info("verify_intervn_sum")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query('StatTable.UveVirtualNetworkAgent.vn_stats',
                             start_time='-10m',
                             end_time='now',
                             select_fields=gen_obj.vn_sum_rows['select'],
                             where_clause=gen_obj.vn_sum_rows['whereclause'])
        self.logger.info(str(res))
        if len(res) == gen_obj.vn_sum_rows['rows']:
            return True
        return False 

    @retry(delay=1, tries=10)
    def verify_flow_samples(self, generator_obj):
        self.logger.info("verify_flow_samples")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        vrouter = generator_obj._hostname
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['T'], dir=1, where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        if len(res) != generator_obj.num_flow_samples:
            return False
        
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        result = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.egress_flow_start_time),
                             end_time=str(generator_obj.egress_flow_end_time),
                             select_fields=['T'], dir=0, where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(result))
        if len(result) != generator_obj.egress_num_flow_samples:
            return False

        return True
    # end verify_flow_samples
 
    def verify_where_query_prefix(self,generator_obj):
        
        self.logger.info('verify where query in FlowSeriesTable')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        vrouter = generator_obj._hostname
        a_query = Query(table="FlowSeriesTable",
                start_time=(generator_obj.flow_start_time),
                end_time=(generator_obj.flow_end_time),
                select_fields=["sourcevn","sourceip","vrouter"],
                where=[[{"name":"sourcevn","value":"domain1:admin","op":7},
                        {"name":"destvn","value":"domain1:admin","op":7},
                        {"name":"vrouter","value":"%s"%vrouter,"op":1}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        assert(len(res)>0)
        a_query = Query(table="FlowSeriesTable",
                start_time=(generator_obj.flow_start_time),
                end_time=(generator_obj.flow_end_time),
                select_fields=["sourcevn","sourceip","vrouter"],
                where=[[{"name":"protocol","value":1,"op":1}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        assert(len(res)>0)
        return True

    def verify_flow_table(self, generator_obj):
        vrouter = generator_obj._hostname
        # query flow records
        self.logger.info('verify_flow_table')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=[
                                 'UuidKey', 'agg-packets', 'agg-bytes'],
                             where_clause='vrouter=%s'% vrouter)
        self.logger.info("FlowRecordTable result:%s" % str(res))
        assert(len(res) == generator_obj.flow_cnt)

        # query based on various WHERE parameters

        # sourcevn and sourceip
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['UuidKey', 'sourcevn', 'sourceip'],
            where_clause='sourceip=10.10.10.1 AND sourcevn=domain1:admin:vn1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['sourcevn', 'sourceip'],
            where_clause='sourceip=10.10.10.1 AND sourcevn=domain1:admin:vn1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        # give non-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'sourcevn', 'sourceip'],
                             where_clause='sourceip=20.1.1.10 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['sourcevn', 'sourceip'],
            where_clause='sourceip=20.1.1.10 AND sourcevn=domain1:admin:vn1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)

        # destvn and destip
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['UuidKey', 'destvn', 'destip'],
            where_clause='destip=2001:db8::2:1 AND destvn=domain1:admin:vn2&>'+
                         ' AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['destvn', 'destip'],
            where_clause='destip=2001:db8::2:1 AND destvn=domain1:admin:vn2&>'+
                         ' AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        # give non-existent values in the where clause
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['UuidKey', 'destvn', 'destip'],
            where_clause='destip=10.10.10.2 AND ' +
            'destvn=default-domain:default-project:default-virtual-network AND' +
            'vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['destvn', 'destip'],
            where_clause='destip=20.1.1.10 AND destvn=domain1:admin:vn2&> AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)

        # sourcevn + sourceip AND destvn + destip [ipv4/ipv6]
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.egress_flow_start_time),
            end_time=str(generator_obj.egress_flow_end_time),
            select_fields=['UuidKey', 'sourcevn', 'sourceip',
                'destvn', 'destip'],
            where_clause='sourceip=2001:db8::1:2 AND '+
                'sourcevn=domain1:admin:vn2 AND '+
                'destip=10.10.10.1 AND destvn=domain1:admin:vn1 AND '+
                'vrouter=%s'% vrouter,
            dir=0)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.egress_flow_start_time),
            end_time=str(generator_obj.egress_flow_end_time),
            select_fields=['sourcevn', 'sourceip', 'destvn', 'destip'],
            where_clause='sourceip=2001:db8::1:2 AND '+
                'sourcevn=domain1:admin:vn2 AND '+
                'destip=10.10.10.1 AND destvn=domain1:admin:vn1 AND '+
                'vrouter=%s'% vrouter,
            dir=0)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.egress_num_flow_samples)

        # sport and protocol
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'sport', 'protocol'],
                             where_clause='sport=32777 AND protocol=1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['sport', 'protocol'],
                             where_clause='sport=32777 AND protocol=1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 5)
        # give no-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'sport', 'protocol'],
                             where_clause='sport=20 AND protocol=17 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['sport', 'protocol'],
                             where_clause='sport=20 AND protocol=1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)

        # dport and protocol
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'dport', 'protocol'],
                             where_clause='dport=104 AND protocol=2 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['dport', 'protocol'],
                             where_clause='dport=104 AND protocol=2 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 5)
        # give no-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'dport', 'protocol'],
                             where_clause='dport=10 AND protocol=17 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['dport', 'protocol'],
                             where_clause='dport=10 AND protocol=17 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)

        # sort and limit
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['UuidKey', 'protocol'], where_clause='vrouter=%s'% vrouter,
            sort_fields=['protocol'], sort=1)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        assert(res[0]['protocol'] == 0)

        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['protocol'], where_clause='vrouter=%s'% vrouter,
                             sort_fields=['protocol'], sort=2, limit=1)
        self.logger.info(str(res))
        assert(len(res) == 1)
        assert(res[0]['protocol'] == 2)

        # limit without sort
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['protocol'], where_clause='vrouter=%s'% vrouter,
                             limit=1)
        self.logger.info(str(res))
        assert(len(res) == 1)

        # Filter by action
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'action', 'drop_reason'],
                             where_clause='vrouter=%s'% vrouter,
                             filter='action=pass')
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)

        # verify vmi_uuid field
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'vmi_uuid'],
                             where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        for r in res:
            assert(r['vmi_uuid'] == generator_obj.flow_vmi_uuid)

        # verify vmi_uuid with filter
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'vmi_uuid'],
                             where_clause='vrouter=%s'% vrouter,
                             filter='vmi_uuid=%s'% generator_obj.flow_vmi_uuid)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        for r in res:
            assert(r['vmi_uuid'] == generator_obj.flow_vmi_uuid)

        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'vmi_uuid'],
                             where_clause='vrouter=%s'% vrouter,
                             filter='vmi_uuid=%s'% str(uuid.uuid1()))
        self.logger.info(str(res))
        assert(len(res) == 0)

        # Filter by drop_reason
        res = vns.post_query('FlowRecordTable',
                  start_time=str(generator_obj.egress_flow_start_time),
                  end_time=str(generator_obj.egress_flow_end_time),
                  select_fields=['UuidKey', 'drop_reason'],
                  where_clause='vrouter=%s'% vrouter,
                  filter='drop_reason=Reason1', dir=0)
        self.logger.info(str(res))
        assert(len(res) == 1)

        # Range query on sport
        res = vns.post_query('FlowRecordTable',
                  start_time=str(generator_obj.flow_start_time),
                  end_time=str(generator_obj.flow_end_time),
                  select_fields=['UuidKey', 'sport', 'protocol'],
                  where_clause='(sport=32747<32787 AND protocol=0 AND \
                      vrouter=%s)' % vrouter)
        self.logger.info(str(res))
        assert(len(res) == 2)

        return True
    # end verify_flow_table

    def verify_flow_series_aggregation_binning(self, generator_object):
        generator_obj = generator_object[0]
        vrouter = generator_obj._hostname
        self.logger.info('verify_flow_series_aggregation_binning')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)

        # Helper function for stats aggregation 
        def _aggregate_stats(flow, start_time, end_time):
            stats = {'sum_bytes':0, 'sum_pkts':0}
            for f in flow.samples:
                if f._timestamp < start_time:
                    continue
                elif f._timestamp > end_time:
                    break
                stats['sum_bytes'] += f.flowdata[0].diff_bytes
                stats['sum_pkts'] += f.flowdata[0].diff_packets
            return stats 
        
        def _aggregate_flows_stats(flows, start_time, end_time):
            stats = {'sum_bytes':0, 'sum_pkts':0}
            for f in flows:
                s = _aggregate_stats(f, start_time, end_time)
                stats['sum_bytes'] += s['sum_bytes']
                stats['sum_pkts'] += s['sum_pkts']
            return stats

        # 1. stats
        self.logger.info('Flowseries: [sum(bytes), sum(packets), flow_count]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['sum(bytes)', 'sum(packets)', 'flow_count'], 
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        exp_sum_pkts = exp_sum_bytes = 0
        for f in generator_obj.flows:
            exp_sum_pkts += f.packets
            exp_sum_bytes += f.bytes
        assert(res[0]['sum(packets)'] == exp_sum_pkts)
        assert(res[0]['sum(bytes)'] == exp_sum_bytes)
        assert(res[0]['flow_count'] == generator_obj.flow_cnt)

        # 2. flow tuple + stats
        self.logger.info(
            'Flowseries: [sport, dport, sum(bytes), sum(packets), flow_count]')
        # Each flow has unique (sport, dport). Therefore, the following query
        # should return # records equal to the # flows.
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['sport', 'dport', 'sum(bytes)', 
                           'sum(packets)', 'flow_count'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        for r in res:
            cnt = 0
            for f in generator_obj.flows:
                if r['sport'] == f.sport and r['dport'] == f.dport:
                    assert(r['sum(packets)'] == f.packets)
                    assert(r['sum(bytes)'] == f.bytes)
                    assert(r['flow_count'] == 1)
                    break
                cnt += 1
            assert(cnt < generator_obj.flow_cnt)

        # All flows has the same (sourcevn, destvn). Therefore, the following
        # query should return one record.
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['sourcevn', 'destvn', 'sum(bytes)', 
                           'sum(packets)', 'flow_count'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        exp_sum_pkts = exp_sum_bytes = 0
        for f in generator_obj.flows:
            exp_sum_pkts += f.packets
            exp_sum_bytes += f.bytes
        assert(res[0]['sum(packets)'] == exp_sum_pkts)
        assert(res[0]['sum(bytes)'] == exp_sum_bytes)
        assert(res[0]['flow_count'] == generator_obj.flow_cnt)

        # top 3 flows
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['sport', 'dport', 'sum(bytes)'],
                             where_clause='vrouter=%s'% vrouter,
                             sort_fields=['sum(bytes)'], sort=2, limit=3)
        self.logger.info(str(res))
        assert(len(res) == 3)
        exp_res = sorted(
            generator_obj.flows, key=lambda flow: flow.bytes, reverse=True)
        cnt = 0
        for r in res:
            assert(r['sport'] == exp_res[cnt].sport)
            assert(r['dport'] == exp_res[cnt].dport)
            assert(r['sum(bytes)'] == exp_res[cnt].bytes)
            cnt += 1

        # 3. T=<granularity> + stats
        self.logger.info('Flowseries: [T=<x>, sum(bytes), sum(packets)]')
        st = str(generator_obj.flow_start_time)
        et = str(generator_obj.flow_start_time + (30 * 1000 * 1000))
        granularity = 10
        gms = granularity * 1000 * 1000 # in micro seconds
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'sum(bytes)',
                           'sum(packets)'],
            where_clause='sourcevn=domain1:admin:vn1 ' +
            'AND destvn=domain1:admin:vn2&> AND vrouter=%s'% vrouter)
        diff_t = int(et) - int(st)
        num_records = (diff_t/gms) + bool(diff_t%gms)
        assert(len(res) == num_records)
        ts = [generator_obj.flow_start_time + (x * gms) \
              for x in range(num_records)]
        exp_result = {}
        for t in ts:
            end_time = t + gms
            if end_time > int(et):
                end_time = int(et)
            ts_stats = _aggregate_flows_stats(generator_obj.flows, 
                                              t, end_time)
            exp_result[t] = {'sum(bytes)':ts_stats['sum_bytes'],
                             'sum(packets)':ts_stats['sum_pkts']}
        self.logger.info('exp_result: %s' % str(exp_result))
        self.logger.info('res: %s' % str(res))
        assert(len(exp_result) == num_records)
        for r in res:
            try:
                stats = exp_result[r['T']]
            except KeyError:
                assert(False)
            assert(r['sum(bytes)'] == stats['sum(bytes)'])
            assert(r['sum(packets)'] == stats['sum(packets)'])

        # 4. T=<granularity> + tuples + stats
        self.logger.info(
            'Flowseries: [T=<x>, protocol, sum(bytes), sum(packets)]')
        st = str(generator_obj.flow_start_time)
        et = str(generator_obj.flow_start_time + (10 * 1000 * 1000))
        granularity = 5
        gms = 5 * 1000 * 1000
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'protocol', 'sum(bytes)',
                           'sum(packets)'],
            where_clause='sourcevn=domain1:admin:vn1 ' +
            'AND destvn=domain1:admin:vn2&> AND vrouter=%s'% vrouter)
        diff_t = int(et) - int(st)
        num_ts = (diff_t/gms) + bool(diff_t%gms)
        ts = [generator_obj.flow_start_time + (x * gms) \
              for x in range(num_ts)]
        proto_flows = [
                        [generator_obj.flows[0], generator_obj.flows[1]],
                        [generator_obj.flows[2], generator_obj.flows[3]],
                        [generator_obj.flows[4]]
                      ]
        proto_ts = [ts, ts, [ts[0]]]
        exp_result = {}
        for i in range(0, len(proto_flows)):
            ts_stats = {}
            for ts in proto_ts[i]:
                end_time = ts + gms
                if end_time > int(et): end_time = int(et)
                stats = _aggregate_flows_stats(proto_flows[i], ts, end_time)
                ts_stats[ts] = {'sum(bytes)':stats['sum_bytes'],
                                'sum(packets)':stats['sum_pkts']}
            exp_result[i] = ts_stats
        self.logger.info('exp_result: %s' % str(exp_result))
        self.logger.info('res: %s' % str(res))
        assert(len(res) == 5)
        for r in res:
            try:
                stats = exp_result[r['protocol']][r['T']]
            except KeyError:
                assert(False)
            assert(r['sum(bytes)'] == stats['sum(bytes)'])
            assert(r['sum(packets)'] == stats['sum(packets)'])

        # 5. T=<granularity> + stats, granularity > (end_time - start_time)
        self.logger.info('Flowseries: [T=<x>, sum(bytes), sum(packets)], '
                         'x > (end_time - start_time)')
        st = str(generator_obj.flow_start_time)
        et = str(generator_obj.flow_end_time)
        granularity = 70
        gms = granularity * 1000 * 1000 # in micro seconds
        assert(gms > (int(et) - int(st)))
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'sum(bytes)',
                           'sum(packets)'],
            where_clause='vrouter=%s'% vrouter)
        ts_stats = _aggregate_flows_stats(generator_obj.flows, 
                                          int(st), int(et))
        exp_result = {int(st):{'sum(bytes)':ts_stats['sum_bytes'],
                               'sum(packets)':ts_stats['sum_pkts']}}
        self.logger.info('exp_result: %s' % str(exp_result))
        self.logger.info('res: %s' % str(res))
        assert(len(res) == 1)
        for r in res:
            try:
                stats = exp_result[r['T']]
            except KeyError:
                assert(False)
            assert(r['sum(bytes)'] == stats['sum(bytes)'])
            assert(r['sum(packets)'] == stats['sum(packets)'])
        
        # 6. direction_ing + stats
        self.logger.info('Flowseries: [direction_ing, sum(bytes), sum(packets), flow_count]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['direction_ing', 'sum(bytes)', 'sum(packets)', 'flow_count'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        exp_sum_pkts = exp_sum_bytes = 0
        for f in generator_obj.flows:
            exp_sum_pkts += f.packets
            exp_sum_bytes += f.bytes
        direction_ing = generator_obj.flows[0].direction_ing
        assert(res[0]['sum(packets)'] == exp_sum_pkts)
        assert(res[0]['sum(bytes)'] == exp_sum_bytes)
        assert(res[0]['flow_count'] == generator_obj.flow_cnt)
        assert(res[0]['direction_ing'] == direction_ing)
        
        self.logger.info('Flowseries: [direction_ing, sum(bytes), sum(packets), flow_count]')
        result = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.egress_flow_start_time),
            end_time=str(generator_obj.egress_flow_end_time),
            select_fields=['direction_ing', 'sum(bytes)', 'sum(packets)', 'flow_count'],
            where_clause='vrouter=%s'% vrouter, dir=0)
        self.logger.info(str(result))
        assert(len(result) == 1)
        exp_sum_pkts = exp_sum_bytes = 0
        for f in generator_obj.egress_flows:
            exp_sum_pkts += f.packets
            exp_sum_bytes += f.bytes
        direction_ing = generator_obj.egress_flows[0].direction_ing
        assert(result[0]['sum(packets)'] == exp_sum_pkts)
        assert(result[0]['sum(bytes)'] == exp_sum_bytes)
        assert(result[0]['flow_count'] == generator_obj.flow_cnt)
        assert(result[0]['direction_ing'] == direction_ing)
        
        # 7. T=<granularity> + tuples
        self.logger.info(
            'Flowseries: [T=<x>, sourcevn, destvn, sport, dport, protocol]')
        st = str(generator_obj.flow_start_time)
        et = str(generator_obj.flow_start_time + (10 * 1000 * 1000))
        granularity = 5
        gms = 5 * 1000 * 1000
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'protocol', 'sourcevn', 'destvn',
                           'sport', 'dport'],
            where_clause='sourcevn=domain1:admin:vn1' +
            'AND destvn=domain1:admin:vn2&> AND vrouter=%s'% vrouter)
        diff_t = int(et) - int(st)
        num_ts = (diff_t/gms) + bool(diff_t%gms)
        ts = [generator_obj.flow_start_time + (x * gms) \
              for x in range(num_ts)]
        exp_result = {}
        
        exp_result_cnt=0
        for i in generator_obj.flows:
            exp_result[exp_result_cnt] = {'T':ts[0], 'sourcevn':i.sourcevn, 
                                  'destvn':i.destvn, 'sport':i.sport,  
                                  'dport':i.dport, 'protocol':i.protocol,}
            exp_result_cnt +=1

        records = generator_obj.flow_cnt-1
        for i in range(0,records):
            exp_result[exp_result_cnt] = {'T':ts[1], 'sourcevn':generator_obj.flows[i].sourcevn,
                                  'destvn':generator_obj.flows[i].destvn,
                                  'sport':generator_obj.flows[i].sport,
                                  'dport':generator_obj.flows[i].dport,
                                  'protocol':generator_obj.flows[i].protocol,}
            exp_result_cnt +=1

        assert(exp_result_cnt == len(res))
        count = 0
        for r in res:
             assert(r['T'] == exp_result[count]['T'])
             assert(r['sourcevn'] == exp_result[count]['sourcevn'])
             assert(r['destvn'] == exp_result[count]['destvn'])
             assert(r['sport'] == exp_result[count]['sport'])
             assert(r['dport'] == exp_result[count]['dport'])
             assert(r['protocol'] == exp_result[count]['protocol'])
             count +=1

        # 8. Timestamp + stats
        self.logger.info('Flowseries: [T, bytes, packets]')
        # testing for flows at index 1 in generator_obj.flows
        flow = generator_obj.flows[1]
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['T', 'bytes', 'packets'],
            where_clause='sourcevn=%s' %(flow.sourcevn) +
            'AND destvn=%s AND sport= %d' %(flow.destvn, flow.sport) +
            'AND dport=%d AND protocol=%d' %(flow.dport, flow.protocol) +
            'AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        
        assert(len(res) == len(flow.samples))
        for f in flow.samples:
            found = 0
            for r in res:
                if r['T'] == f._timestamp:
                      assert(r['packets'] == f.flowdata[0].diff_packets)
                      assert(r['bytes'] == f.flowdata[0].diff_bytes)
                      found = 1
                      break
            assert(found)

        # limit
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['T', 'bytes', 'packets'],
            where_clause='sourcevn=%s' %(flow.sourcevn) +
            'AND destvn=%s AND sport= %d' %(flow.destvn, flow.sport) +
            'AND dport=%d AND protocol=%d' %(flow.dport, flow.protocol) +
            'AND vrouter=%s'% vrouter, limit=len(flow.samples)/2)
        self.logger.info(str(res))
        assert(len(res) == len(flow.samples)/2)

        # 9. Raw bytes and packets
        self.logger.info('Flowseries: [bytes, packets]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['bytes', 'packets'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        sorted_res = sorted(res, key=itemgetter('packets', 'bytes'))
        flow = []
        for f in generator_obj.flows:
            for s in f.samples:
                flow.append({'packets':s.flowdata[0].diff_packets,
                            'bytes':s.flowdata[0].diff_bytes})
        sorted_flow = sorted(flow, key=itemgetter('packets', 'bytes'))
        assert(sorted_res == sorted_flow)

        # limit
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['bytes', 'packets'],
            where_clause='vrouter=%s'% vrouter, limit=5)
        self.logger.info(str(res))
        assert(len(res) == 5)

        # 10. Timestamp
        self.logger.info('Flowseries: [T]')
        # testing for flows at index 1 in generator_obj.flows
        flow = generator_obj.flows[1]
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['T'],
            where_clause='sourcevn=%s' %(flow.sourcevn) +
            'AND destvn=%s AND sport= %d' %(flow.destvn, flow.sport) +
            'AND dport=%d AND protocol=%d' %(flow.dport, flow.protocol) +
            'AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == len(flow.samples))
        sorted_res = sorted(res, key=itemgetter('T'))
        
        cnt = 0
        for f in flow.samples:
            assert(sorted_res[cnt]['T'] == f._timestamp)
            cnt+= 1

        # 11. T=<granularity>
        self.logger.info('Flowseries: [T=<x>]')
        st = str(generator_obj.flow_start_time)
        et = str(generator_obj.flow_start_time + (10 * 1000 * 1000))
        granularity = 5
        gms = 5 * 1000 * 1000
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity)],
            where_clause='sourcevn=domain1:admin:vn1' +
            'AND destvn=domain1:admin:vn2&> AND vrouter=%s'% vrouter)
        diff_t = int(et) - int(st)
        num_ts = (diff_t/gms) + bool(diff_t%gms)
        ts = []
        for x in range(num_ts):
            ts.append({'T':generator_obj.flow_start_time + (x * gms)})
        self.logger.info(str(res))
        assert(res == ts)

        # 12. Flow tuple
        self.logger.info('Flowseries: [protocol, sport, dport]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['protocol', 'sport', 'dport'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        for flow in generator_obj.flows:
            found = 0
            for r in res:
                if flow.sport == r['sport']:
                    assert(r['dport'] == flow.dport)
                    assert(r['protocol'] == flow.protocol)
                    found = 1
            assert(found)

        # 13. T + flow tuple
        self.logger.info('Flowseries: [T, protocol, sport, dport]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['T', 'protocol', 'sport', 'dport'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        for flow in generator_obj.flows:
            sport = flow.sport
            for sample in flow.samples:
                found = 0
                for r in res:
                    if r['T'] == sample._timestamp and r['sport'] == sport:
                        assert(r['protocol'] == flow.protocol)
                        assert(r['dport'] == flow.dport)
                        found = 1
                        break
                assert(found) 

        # 14. T + flow tuple + stats
        self.logger.info('Flowseries: [T, protocol, sport, dport, bytes, packets]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['T', 'protocol', 'sport', 'dport', 'bytes', 'packets'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        for flow in generator_obj.flows:
            sport = flow.sport
            for sample in flow.samples:
                found = 0
                for r in res:
                    if r['T'] == sample._timestamp and r['sport'] == sport:
                        assert(r['protocol'] == flow.protocol)
                        assert(r['dport'] == flow.dport)
                        assert(r['bytes'] == sample.flowdata[0].diff_bytes)
                        assert(r['packets'] == sample.flowdata[0].diff_packets)
                        found = 1
                        break
                assert(found)

        # limit
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['T', 'protocol', 'sport', 'dport', 'bytes', 'packets'],
            where_clause='vrouter=%s'% vrouter,
            limit=generator_obj.num_flow_samples+10)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)

        # 15 vrouter
        self.logger.info("Flowseries: [sourcevn, destvn, vrouter]")
        generator_obj1 = generator_object[1]
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj1.flow_start_time),
                             end_time=str(generator_obj1.flow_end_time),
                             select_fields=['sourcevn', 'destvn', 'vrouter'], dir=1, where_clause='')
        self.logger.info(str(res))
        assert(len(res) == (generator_obj1.num_flow_samples + generator_obj.num_flow_samples))

        sorted_res = sorted(res, key=itemgetter('vrouter'))
        exp_result = []
        for flow in generator_obj1.flows:
            for f in flow.samples:
                dict = {'vrouter':f._source, 'destvn':f.flowdata[0].destvn,
                        'sourcevn':f.flowdata[0].sourcevn}
                exp_result.append(dict)
        for flow in generator_obj.flows:
            for f in flow.samples:
                dict = {'vrouter':f._source, 'destvn':f.flowdata[0].destvn,
                        'sourcevn':f.flowdata[0].sourcevn}
                exp_result.append(dict)
        sorted_exp_result = sorted(exp_result, key=itemgetter('vrouter'))
        assert(sorted_res == sorted_exp_result)
        return True
    # end verify_flow_series_aggregation_binning

    def verify_fieldname_messagetype(self):
        self.logger.info('Verify stats table for stats name field');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        query = Query(table="StatTable.FieldNames.fields",
		            start_time="now-10m",
                            end_time="now",
                            select_fields=["fields.value"],
                            where=[[{"name": "name", "value": "Message", "op": 7}]])
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info(str(res))
        assert(len(res)>1)
        return True

    @retry(delay=2, tries=5)
    def verify_generator_collector_connection(self, gen_http_port):
        self.logger.info('verify_generator_collector_connection')
        vgen = VerificationGenerator('127.0.0.1', gen_http_port)
        try:
            conn_status = vgen.get_collector_connection_status()
        except Exception as err:
            self.logger.error('Failed to get collector connection: %s' % (err))
            return False
        else:
            return conn_status['status'] == 'Established'
    # end verify_generator_collector_connection

    @retry(delay=2, tries=5)
    def verify_collector_redis_uve_connection(self, collector, connected=True):
        self.logger.info('verify_collector_redis_uve_connection')
        vcl = VerificationCollector('127.0.0.1', collector.http_port,
                self.sandesh_config_struct)
        try:
            redis_uve = vcl.get_redis_uve_info()['RedisUveInfo']
            if redis_uve['status'] == 'Connected':
                return connected
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return not connected
    # end verify_collector_redis_uve_connection 

    @retry(delay=2, tries=5)
    def verify_collector_db_info(self, collector,
                                 disk_usage_percentage_out = None,
                                 pending_compaction_tasks_out = None,
                                 disk_usage_percentage_level_out = None,
                                 pending_compaction_tasks_level_out = None):

        self.logger.info('verify_collector_db_info')
        vcl = VerificationCollector('127.0.0.1', collector.http_port,
                self.sandesh_config_struct)
        try:
            db_info_dict = vcl.get_db_info()
            db_info = (db_info_dict['db_info'])['DbInfo']
            disk_usage_percentage = int(db_info['disk_usage_percentage'])
            pending_compaction_tasks = int(db_info['pending_compaction_tasks'])
            disk_usage_percentage_level = \
                int(db_info['disk_usage_percentage_level'])
            pending_compaction_tasks_level = \
                int(db_info['pending_compaction_tasks_level'])
            self.logger.info('collector exp  disk_usage_percentage:%u'
                             ' pending_compaction_tasks:%u'
                             ' disk_usage_percentage_level:%u'
                             ' pending_compaction_tasks_level:%u' %
                             (disk_usage_percentage_out,
                              pending_compaction_tasks_out,
                              disk_usage_percentage_level_out,
                              pending_compaction_tasks_level_out))
            self.logger.info('collector read disk_usage_percentage:%u'
                             ' pending_compaction_tasks:%u'
                             ' disk_usage_percentage_level:%u'
                             ' pending_compaction_tasks_level:%u' %
                             (disk_usage_percentage, pending_compaction_tasks,
                              disk_usage_percentage_level,
                              pending_compaction_tasks_level))
            if ((disk_usage_percentage == disk_usage_percentage_out) and
                (pending_compaction_tasks == pending_compaction_tasks_out) and
                (disk_usage_percentage_level ==
                 disk_usage_percentage_level_out) and
                (pending_compaction_tasks_level ==
                 pending_compaction_tasks_level_out)):
                return True
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return False
    # end verify_collector_db_info

    @retry(delay=2, tries=5)
    def verify_opserver_redis_uve_connection(self, opserver, connected=True):
        self.logger.info('verify_opserver_redis_uve_connection')
        vops = VerificationOpsSrv('127.0.0.1', opserver.http_port,
            self.admin_user, self.admin_password)
        try:
            redis_uve = vops.get_redis_uve_info()['RedisUveInfo']
            if redis_uve['status'] == 'Connected':
                return connected
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return not connected
    # end verify_opserver_redis_uve_connection

    @retry(delay=2, tries=5)
    def set_opserver_db_info(self, opserver,
                             disk_usage_percentage_in = None,
                             pending_compaction_tasks_in = None,
                             disk_usage_percentage_out = None,
                             pending_compaction_tasks_out = None):
        self.logger.info('set_opserver_db_info')
        vops = VerificationOpsSrvIntrospect('127.0.0.1', opserver.http_port,
            self.admin_user, self.admin_password)
        try:
            vops.db_info_set_request(disk_usage_percentage_in,
                                     pending_compaction_tasks_in)
            db_info_dict = vops.db_info_get_request()
            db_info = db_info_dict['DbInfo']
            disk_usage_percentage = int(db_info['disk_usage_percentage'])
            pending_compaction_tasks = int(db_info['pending_compaction_tasks'])

            self.logger.info('opserver exp  disk_usage_percentage:%u'
                             ' pending_compaction_tasks:%u' %
                             (disk_usage_percentage_out,
                              pending_compaction_tasks_out))
            self.logger.info('opserver read disk_usage_percentage:%u'
                             ' pending_compaction_tasks:%u' %
                             (disk_usage_percentage, pending_compaction_tasks))
            if ((disk_usage_percentage == disk_usage_percentage_out) and
                (pending_compaction_tasks == pending_compaction_tasks_out)):
                return True
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return False
    # end set_opserver_db_info

    @retry(delay=2, tries=5)
    def verify_tracebuffer_in_analytics_db(self, src, mod, tracebuf):
        self.logger.info('verify trace buffer data in analytics db')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        where_clause = []
        where_clause.append('Source = ' + src)
        where_clause.append('ModuleId = ' + mod)
        where_clause.append('Category = ' + tracebuf)
        where_clause = ' AND '.join(where_clause)
        res = vns.post_query('MessageTable', start_time='-3m', end_time='now',
                             select_fields=['MessageTS', 'Type'],
                             where_clause=where_clause, filter='Type=4')
        if not res:
            return False
        self.logger.info(str(res))
        return True
    # end verify_tracebuffer_in_analytics_db

    @retry(delay=1, tries=5)
    def verify_table_source_module_list(self, exp_src_list, exp_mod_list):
        self.logger.info('verify source/module list')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        try:
            src_list = vns.get_table_column_values(COLLECTOR_GLOBAL_TABLE, 
                                                   SOURCE)
            self.logger.info('src_list: %s' % str(src_list))
            if len(set(src_list).intersection(exp_src_list)) != \
                    len(exp_src_list):
                return False
            mod_list = vns.get_table_column_values(COLLECTOR_GLOBAL_TABLE,
                                                   MODULE)
            self.logger.info('mod_list: %s' % str(mod_list))
            if len(set(mod_list).intersection(exp_mod_list)) != \
                    len(exp_mod_list):
                return False
        except Exception as e:
            self.logger.error('Exception: %s in getting source/module list' % e)
        else:
            return True
    # end verify_table_source_module_list

    @retry(delay=1, tries=5)
    def verify_where_query(self):
        self.logger.info('Verify where query with int type works');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        query = Query(table="StatTable.QueryPerfInfo.query_stats",
                            start_time="now-1h",
                            end_time="now",
                            select_fields=["query_stats.rows","table","query_stats.time"],
                            where=[[{"name":"query_stats.rows","value":0,"op":1}]])
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        assert(len(res)>0)
        return True
    # end verify_where_query

    def verify_collector_object_log(self, start_time, end_time):
        self.logger.info('verify_collector_object_log')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        query = Query(table='ObjectCollectorInfo',
                             start_time=start_time, end_time=end_time,
                             select_fields=['ObjectLog'])
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info("collector object log: %s" % res)
        return res
    # end verify_collector_object_log

    @retry(delay=2, tries=5)
    def verify_collector_object_log_before_purge(self, start_time, end_time):
        self.logger.info('verify_collector_object_log_before_purge')
        res = self.verify_collector_object_log(start_time, end_time)
        self.logger.info("collector object log before purging: %s" % res)
        if not res:
            return False
        return True
    # end verify_collector_object_log_before_purge

    def verify_database_purge_query(self, json_qstr):
        self.logger.info('verify database purge query');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_purge_query_json(json_qstr)
        try:
            assert(res['status'] == 'started')
            purge_id = res['purge_id']
        except KeyError:
            assert(False)
        else:
            return purge_id
    # end verify_database_purge_query

    @retry(delay=2, tries=5)
    def verify_collector_object_log_after_purge(self, start_time, end_time):
        self.logger.info('verify_collector_object_log_after_purge')
        res = self.verify_collector_object_log(start_time, end_time)
        self.logger.info("collector object log after purging: %s" % res)
        if res != []:
            return False
        return True
    # end verify_collector_object_log_after_purge

    @retry(delay=5, tries=5)
    def verify_database_purge_status(self, purge_id):
        self.logger.info('verify database purge status: purge_id [%s]' %
                         (purge_id))
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        query = Query(table='StatTable.DatabasePurgeInfo.stats',
                      start_time='now-1m', end_time='now',
                      select_fields=['stats.purge_id', 'stats.purge_status',
                                     'stats.purge_status_details'],
                      where=[[{'name':'stats.purge_id', 'value':purge_id,
                               'op':1}]])
        self.logger.info(json.dumps(query.__dict__))
        res = vns.post_query_json(json.dumps(query.__dict__))
        self.logger.info('database purge status: %s' % (str(res)))
        if not res:
            return False
        assert(res[0]['stats.purge_status'] == 'success' and \
               res[0]['stats.purge_status_details'] == '')
        return True
    # end verify_database_purge_status

    def verify_database_purge_with_percentage_input(self):
        self.logger.info('verify database purge with percentage input')
        end_time = UTCTimestampUsec()
        start_time = end_time - 10*60*pow(10,6)
        assert(self.verify_collector_object_log_before_purge(start_time, end_time))
        json_qstr = json.dumps({'purge_input': 100})
        purge_id = self.verify_database_purge_query(json_qstr)
        assert(self.verify_database_purge_status(purge_id))
        assert(self.verify_collector_object_log_after_purge(start_time, end_time))
        return True
    # end verify_database_purge_with_percentage_input

    def verify_database_purge_support_utc_time_format(self):
        self.logger.info('verify database purge support utc time format')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        json_qstr = json.dumps({'purge_input': 'now'})
        end_time = OpServerUtils.convert_to_utc_timestamp_usec('now')
        start_time = end_time - 20*60*pow(10,6)
        assert(self.verify_collector_object_log_before_purge(start_time, end_time))
        purge_id = self.verify_database_purge_query(json_qstr)
        assert(self.verify_database_purge_status(purge_id))
        assert(self.verify_collector_object_log_after_purge(start_time, end_time))
        return True
    # end verify_database_purge_support_utc_time_format

    def verify_database_purge_support_datetime_format(self):
        self.logger.info('verify database purge support datetime format')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        dt = datetime.datetime.now().strftime("%Y %b %d %H:%M:%S.%f")
        json_qstr = json.dumps({'purge_input': dt})
        end_time = OpServerUtils.convert_to_utc_timestamp_usec(dt)
        start_time = end_time - 30*60*pow(10,6)
        assert(self.verify_collector_object_log_before_purge(start_time, end_time))
        purge_id = self.verify_database_purge_query(json_qstr)
        assert(self.verify_database_purge_status(purge_id))
        assert(self.verify_collector_object_log_after_purge(start_time, end_time))
        return True
    # end verify_database_purge_support_datetime_format

    def verify_database_purge_support_deltatime_format(self):
        self.logger.info('verify database purge support deltatime format')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        json_qstr = json.dumps({'purge_input': '-1s'})
        end_time = OpServerUtils.convert_to_utc_timestamp_usec('-1s')
        start_time = end_time - 10*60*pow(10,6)
        assert(self.verify_collector_object_log_before_purge(start_time, end_time))
        purge_id = self.verify_database_purge_query(json_qstr)
        assert(self.verify_database_purge_status(purge_id))
        assert(self.verify_collector_object_log_after_purge(start_time, end_time))
        return True
    # end verify_database_purge_support_deltatime_format

    def verify_database_purge_request_limit(self):
        self.logger.info('verify database purge request limit')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        json_qstr = json.dumps({'purge_input': 50})
        res = vns.post_purge_query_json(json_qstr)
        self.logger.info(str(res))
        try:
            assert(res['status'] == 'started')
            purge_id = res['purge_id']
            res1 = vns.post_purge_query_json(json_qstr)
            assert(res1['status'] == 'running')
            assert(res1['purge_id'] == purge_id)
        except KeyError:
            assert(False)
        return True
    # end verify_database_purge_request_limit

    @retry(delay=1, tries=5)
    def verify_object_table_sandesh_types(self, table, object_id,
                                          exp_msg_types):
        self.logger.info('verify_object_table_sandesh_types')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query(table, start_time='-1m', end_time='now',
                select_fields=['Messagetype', 'ObjectLog', 'SystemLog'],
                where_clause='ObjectId=%s' % object_id)
        if not res:
            return False
        self.logger.info('ObjectTable query response: %s' % str(res))
        actual_msg_types = [r['Messagetype'] for r in res]
        actual_msg_types.sort()
        exp_msg_types.sort()
        self.logger.info('Expected message types: %s' % str(exp_msg_types))
        self.logger.info('Actual message types: %s' % str(actual_msg_types))
        return actual_msg_types == exp_msg_types
    # end verify_object_table_sandesh_types

    @retry(delay=1, tries=5)
    def verify_object_table_objectid_values(self, table, exp_object_id ):
        self.logger.info('verify_object_table_objectid_values')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query(table, start_time='-1m', end_time='now',
                select_fields=['Messagetype', 'ObjectLog', 'SystemLog',
                'ObjectId'], where_clause='')
        if not res:
           return False
        self.logger.info('ObjectTable query response: %s' % str(res))
        actual_object_id = [r['ObjectId'] for r in res]
        actual_object_id.sort()
        actual_object_id_s = Set(actual_object_id)
        exp_object_id.sort()
        exp_object_id_s = Set(exp_object_id)
        self.logger.info('Expected ObjectIds: %s' % str(exp_object_id_s))
        self.logger.info('Actual ObjectIds: %s' % str(actual_object_id_s))
        return (actual_object_id_s-exp_object_id_s) == Set()
    # end verify_object_table_objectid_values

    @retry(delay=1, tries=10)
    def verify_object_value_table_query(self, table, exp_object_values):
        self.logger.info('verify_object_value_table_query')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query(table, start_time='-10m', end_time='now',
                             select_fields=['ObjectId'],
                             where_clause='')
        if not res:
            return False
        self.logger.info(res)
        actual_object_values = [r['ObjectId'] for r in res]
        for object_id in exp_object_values:
            self.logger.info('object_id: %s' % object_id)
            if object_id not in actual_object_values:
                return False
        return True
    # end verify_object_value_table_query

    @retry(delay=1, tries=5)
    def verify_keyword_query(self, line, keywords=[]):
        self.logger.info('Verify where query with keywords');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)

        query = Query(table="MessageTable",
                            start_time="now-1h",
                            end_time="now",
                            select_fields=["Xmlmessage","Level"],
                            where=map(lambda x:[{"name": "Keyword", "value": x,
                                "op": 1}], keywords))
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info(str(res))
        return len(res)>0
    # end verify_keyword_query

    @retry(delay=1, tries=5)
    def verify_generator_list_in_redis(self, redis_uve, exp_gen_list):
        self.logger.info('Verify generator list in redis')
        try:
            r = redis.StrictRedis(db=1, port=redis_uve.port, password=redis_uve.password)
            gen_list = r.smembers('NGENERATORS')
        except Exception as e:
            self.logger.error('Failed to get generator list from redis - %s' % e)
            return False
        else:
            self.logger.info('Expected generator list: %s' % str(exp_gen_list))
            self.logger.info('Actual generator list: %s' % str(gen_list))
            return gen_list == set(exp_gen_list)
    # end verify_generator_list_in_redis

    @retry(delay=1, tries=8)
    def verify_fieldname_table(self):
        '''
        This function is called after entres are populated
        in Fieldnames table near simultaneously'. Check is made
        to ensure that the 2 entries are present in the table
        '''
        self.logger.info("verify_fieldname_table")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        self.logger.info("VerificationOpsSrv")
        res = vns.post_query('StatTable.FieldNames.fields',
                             start_time='-1m',
                             end_time='now',
                             select_fields=['fields.value'],
                             where_clause = 'name=ObjectVNTable:ObjectId')
        self.logger.info(str(res))
	#Verify that 2 different n/w are present vn0 and vn1
	assert(len(res)==2)
        return True
    # end verify_fieldname_table

    def _get_filters_url_param(self, filters):
        if filters is None:
            return None
        filt = {k:v for k, v in filters.iteritems() if v is not None}
        if filt.has_key('kfilt'):
            filt['kfilt'] = ','.join(filt['kfilt'])
        if filt.has_key('cfilt'):
            filt['cfilt'] = ','.join(filt['cfilt'])
        if filt.has_key('ackfilt'):
            filt['ackfilt'] = 'true' if filt['ackfilt'] else 'false'
        return filt
    # end _get_filters_url_param

    def _get_filters_json(self, filters):
        if filters is None:
            filters = {}
        filt = {k:v for k, v in filters.iteritems() if v is not None}
        return json.dumps(filt)
    # end _get_filters_json

    @retry(delay=1, tries=4)
    def verify_uve_list(self, table, filts=None, exp_uve_list=[]):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        filters = self._get_filters_url_param(filts)
        table_query = table+'s'
        self.logger.info('verify_uve_list: %s:%s' %
            (table_query, str(filters)))
        try:
            uve_list = vns.uve_query(table_query, filters)
        except Exception as err:
            self.logger.error('Failed to get response for %s:%s [%s]' % \
                (table_query, str(filters), str(err)))
            assert(False)
        actual_uve_list = [uve['name'] for uve in uve_list]
        exp_uve_list.sort()
        actual_uve_list.sort()
        self.logger.info('Expected UVE list: %s' % (str(exp_uve_list)))
        self.logger.info('Actual UVE list: %s' % (str(actual_uve_list)))
        return actual_uve_list == exp_uve_list
    # end verify_uve_list

    def _remove_alarm_token(self, data):
        for item in data:
            if 'UVEAlarms' in item['value'] and \
                'alarms' in item['value']['UVEAlarms']:
                for alarm in item['value']['UVEAlarms']['alarms']:
                    if 'token' in alarm:
                        del alarm['token']
            if '__SOURCE__' in item['value']:
                del item['value']['__SOURCE__']
    # end _remove_alarm_token

    def _verify_uves(self, exp_uves, actual_uves):
        self.logger.info('Expected UVEs: %s' % (str(exp_uves)))
        self.logger.info('Actual UVEs: %s' % (str(actual_uves)))
        if actual_uves is None:
            return False
        etk = exp_uves.keys()
        atk = actual_uves.keys() 
        if len(etk):
            exp_uve_value = exp_uves[etk[0]]
        else:
            exp_uve_value = []
        if len(atk):
            actual_uve_value = actual_uves[atk[0]]
        else:
            actual_uve_value = []
        self.logger.info('Remove token from alarms')
        self._remove_alarm_token(exp_uve_value)
        self._remove_alarm_token(actual_uve_value)
        exp_uve_value.sort()
        actual_uve_value.sort()
        self.logger.info('Expected UVE value: %s' % (str(exp_uve_value)))
        self.logger.info('Actual UVE value: %s' % (str(actual_uve_value)))
        return actual_uve_value == exp_uve_value
    # end _verify_uves

    @retry(delay=1, tries=4)
    def verify_get_alarms(self, table, filts=None, exp_uves=None):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        filters = self._get_filters_url_param(filts)
        self.logger.info('verify_get_alarms: %s' % str(filters))
        try:
            actual_uves = vns.get_alarms(filters)
        except Exception as err:
            self.logger.error('Failed to get response for %s: %s' % \
                (str(filters), str(err)))
            assert(False)
        return self._verify_uves(exp_uves, actual_uves)
    # end verify_get_alarms

    @retry(delay=1, tries=4)
    def verify_multi_uve_get(self, table, filts=None, exp_uves=None):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        filters = self._get_filters_url_param(filts)
        table_query = table+'/*'
        if not filters:
            table_query += '?flat'
        self.logger.info('verify_multi_uve_get: %s:%s' %
            (table_query, str(filters)))
        try:
            actual_uves = vns.uve_query(table_query, filters)
        except Exception as err:
            self.logger.error('Failed to get response for %s:%s [%s]' % \
                (table_query, str(filters), str(err)))
            assert(False)
        return self._verify_uves(exp_uves, actual_uves)
    # end verify_multi_uve_get

    @retry(delay=1, tries=4)
    def verify_uve_post(self, table, filts=None, exp_uves=None):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        filter_json = self._get_filters_json(filts)
        self.logger.info('verify_uve_post: %s: %s' % (table, filter_json))
        try:
            actual_uves = vns.post_uve_request(table, filter_json)
        except Exception as err:
            self.logger.error('Failed to get response for UVE POST request'
                              '%s: %s' % (table, str(err)))
            assert(False)
        return self._verify_uves(exp_uves, actual_uves)
    # end verify_uve_post

    @retry(delay=1, tries=5)
    def verify_alarm_list_include(self, table, filts=None, expected_alarms=[]):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        yfilts = filts or {}
        yfilts['cfilt'] = ["UVEAlarms"] 
        filters = self._get_filters_url_param(yfilts)
        table_query = table+'s'
        self.logger.info('verify_alarm_list: %s:%s' %
            (table_query, str(filters)))
        try:
            alarms = vns.uve_query(table_query, filters)
        except Exception as err:
            self.logger.error('Failed to get response for %s:%s [%s]' % \
                (table_query, str(filters), str(err)))
            assert(False)
        actual_alarms = [alarm['name'] for alarm in alarms \
            if alarm['name'] in set(expected_alarms)]
        expected_alarms.sort()
        actual_alarms.sort()
        self.logger.info('Expected Alarms: %s' % (str(expected_alarms)))
        self.logger.info('Actual Alarms: %s' % (str(actual_alarms)))
        return actual_alarms == expected_alarms
    # end verify_alarm_list_include

    @retry(delay=1, tries=5)
    def verify_alarm_list_exclude(self, table, filts=None, unexpected_alms=[]):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        yfilts = filts or {}
        yfilts['cfilt'] = ["UVEAlarms"]
        filters = self._get_filters_url_param(yfilts)
        table_query = table+'s'
        self.logger.info('verify_alarm_list: %s:%s' %
            (table_query, str(filters)))
        try:
            alarms = vns.uve_query(table_query, filters)
        except Exception as err:
            self.logger.error('Failed to get response for %s:%s [%s]' % \
                (table_query, str(filters), str(err)))
            assert(False)
        actual_alarms = [alarm['name'] for alarm in alarms \
            if alarm['name'] in set(unexpected_alms)]
        unexpected_alms.sort()
        actual_alarms.sort()
        self.logger.info('UnExpected Alarms: %s' % (str(unexpected_alms)))
        self.logger.info('Actual Alarms: %s' % (str(actual_alarms)))
        return len(actual_alarms) == 0
    # end verify_alarm_list_exclude

    def _verify_alarms(self, exp_alarms, actual_alarms):
        self.logger.info('Expected Alarms: %s' % (str(exp_alarms)))
        self.logger.info('Actual Alarms: %s' % (str(actual_alarms)))
        if actual_alarms is None:
            return False
        exp_alarm_value = exp_alarms['value']
        actual_alarm_value = actual_alarms['value']
        self.logger.info('Remove token from alarms')
        self._remove_alarm_token(exp_alarm_value)
        self._remove_alarm_token(actual_alarm_value)
        exp_alarm_value.sort()
        actual_alarm_value.sort()
        self.logger.info('Expected Alarm value: %s' % (str(exp_alarm_value)))
        self.logger.info('Actual Alarm value: %s' % (str(actual_alarm_value)))
        return actual_alarm_value == exp_alarm_value
    # end _verify_alarms

    @retry(delay=1, tries=3)
    def verify_alarm(self, table, key, expected_alarm):
        self.logger.info('verify_alarm: %s:%s' % (table, key))
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        table_query = table+'/'+key
        filters = {'cfilt':'UVEAlarms'}
        try:
            alarm = vns.uve_query(table_query, filters)
        except Exception as err:
            self.logger.error('Failed to get alarm %s:%s [%s]' % \
                (table_query, str(filters), str(err)))
            assert(False)
        return self.verify_alarm_data(expected_alarm, alarm)
    # end verify_alarm

    def verify_alarm_data(self, expected_alarm_data, actual_alarm_data):
        self.logger.info('Expected Alarm: %s' % (str(expected_alarm_data)))
        self.logger.info('Actual Alarm: %s' % (str(actual_alarm_data)))
        if expected_alarm_data == {}:
            return actual_alarm_data == {}
        expected_alarms = expected_alarm_data['alarms']
        try:
            actual_alarms = actual_alarm_data['UVEAlarms']['alarms']
        except (TypeError,KeyError):
            return False
        return actual_alarms == expected_alarms
    # end verify_alarm_data

    def get_db_read_stats_from_qe(self, qe, table_name, is_stats_table=False, field_name='reads'):
        qe_introspect = VerificationGenerator('127.0.0.1', qe.http_port)
        try:
            stats_info = qe_introspect.get_db_read_stats()
            table_stat_info=''
            if is_stats_table == False:
                # parse through stats of physical tables
                table_stat_info = stats_info['table_info']
            else:
                # parse through stats of logical tables
                table_stat_info = stats_info['statistics_table_info']
            for table in table_stat_info:
                if (str(table['table_name']).strip() == str(table_name).strip()):
                    return table[field_name]
        except Exception as err:
            self.logger.error('Exception: %s' % err)
    # end get_db_read_stats_from_qe

    def cleanUp(self):
        self.logger.info('cleanUp started')

        try:
            self.opserver.stop()
        except:
            pass
        try:
            self.alarmgen.stop()
        except:
            pass
        if self.query_engine:
            self.query_engine.stop()
        for collector in self.collectors:
            collector.stop()
        for redis_uve in self.redis_uves:
            redis_uve.stop()
        if self.kafka is not None:
            self.kafka.stop()
        self.zookeeper.stop()
        super(AnalyticsFixture, self).cleanUp()
        self.logger.info('cleanUp complete')

    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("", 0))
        cport = cs.getsockname()[1]
        cs.close()
        return cport

    @staticmethod
    def get_free_udp_port():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("", 0))
        u_port = sock.getsockname()[1]
        sock.close()
        return u_port

    def run_py_daemon(self, args):
            if args[0] == 'contrail-analytics-api':
                from opserver.opserver import main
                cmd = main
            elif args[0] == 'contrail-alarm-gen':
                from opserver.alarmgen import main
                cmd = main
            else:
                cmd = None
            if callable(cmd):
                self.logger.info('Starting python %s' % args[1])
                instance = gevent.spawn(cmd, ' '.join(args[1:]))
                instance.poll = lambda: None
                return instance
            raise NotImplementedError("%s is not mapped" % cmd[0])

    def process_stop(self, name, instance, log_file,
                     del_log=True, is_py=False):
        if is_py:
            self.logger.info('Shutting down python : %s(obj.stop)' % name)
            instance._main_obj.stop()
            instance._main_obj = None
            gevent.sleep(0)
            self.logger.info('Shutting down python : %s(obj.kill)' % name)
            gevent.kill(instance)
            rcode = 0
            self.logger.info('Shutting down python : %s(done)' % name)
        else:
            self.logger.info('Shutting down %s' % name)
            bad_term = False
            if instance.poll() == None:
                instance.terminate()
                cnt = 1
                while cnt < 10:
                    if instance.poll() != None:
                        break
                    cnt += 1
                    gevent.sleep(1)
            else:
                bad_term = True
            if instance.poll() == None:
                self.logger.info('%s FAILED to terminate; will be killed' % name)
                instance.kill()
                bad_term = True
            (p_out, p_err) = instance.communicate()
            rcode = instance.returncode
            if rcode != 0 or bad_term or not del_log:
                self.logger.info('%s returned %d' % (name,rcode))
                self.logger.info('%s terminated stdout: %s' % (name, p_out))
                self.logger.info('%s terminated stderr: %s' % (name, p_err))
                with open(log_file, 'r') as fin:
                    self.logger.info(fin.read())
        subprocess.call(['rm', '-rf', log_file])
        return rcode

    def start_with_ephemeral_ports(self, modname, pnames, args, preexec,
            is_py=False):

        pipes = {}
        for pname in pnames: 
            if is_py:
                pid = os.getppid()
            else:
                pid = os.getpid()
            pipe_name = '/tmp/%s.%d.%s_port' % (modname, pid, pname)
            self.logger.info("Read %s Port from %s" % (pname, pipe_name))
            #import pdb; pdb.set_trace()
            try:
                os.unlink(pipe_name)
            except:
                pass
            os.mkfifo(pipe_name)
            pipein = open(pipe_name, 'r+')
            flags = fcntl(pipein, F_GETFL)
            fcntl(pipein, F_SETFL, flags | os.O_NONBLOCK)
            pipes[pname] = pipein , pipe_name
            
        if is_py:
            instance = self.run_py_daemon(args)
        else:
            instance = subprocess.Popen(args, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE,
                             preexec_fn = preexec)
      
        pmap = {} 
        for k,v in pipes.iteritems(): 
            tries = 60
            port = None
            pipein , pipe_name = v
            while tries >= 0 and instance.poll() == None:
                try:
                    line = pipein.readline()[:-1]
                    port = int(line)
                    self.logger.info("Found %s_port %d for %s" % (k, port, modname))
                    tries = -1
                except Exception as e:
                    self.logger.info("No %s_port found for %s" % (k, modname))
                    gevent.sleep(1)
                    tries = tries - 1
            pipein.close()
            os.unlink(pipe_name)
            pmap[k] = port
            if not instance.poll() is None:
                (p_out, p_err) = instance.communicate()
                rcode = instance.returncode
                self.logger.info('%s returned %d at startup!' % (modname,rcode))
                self.logger.info('%s terminated stdout: %s' % (modname, p_out))
                self.logger.info('%s terminated stderr: %s' % (modname, p_err))

        return pmap, instance

    @staticmethod
    def enable_core():
        try:
	    resource.setrlimit(resource.RLIMIT_CORE, (-1, -1))
        except:
            pass
