#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import resource
import socket
import fixtures
import subprocess
import uuid
from util import retry, get_free_port
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
from opserver.sandesh.viz.constants import MESSAGE_TABLE, SOURCE, MODULE
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
            '--DEFAULT.log_file', self._log_file]
        if self.analytics_fixture.cassandra_port == 0:
            args.append('--DATABASE.disable_all_writes')
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
            if 'disable_object_logs' in self.sandesh_config and \
                self.sandesh_config['disable_object_logs']:
                args.append('--SANDESH.disable_object_logs')
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
            if 'disable_object_logs' in self.sandesh_config and \
                self.sandesh_config['disable_object_logs']:
                args.append('--disable_object_logs')
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
                '--http_server_port', str(self.http_port),
                '--log_file', self._log_file,
                '--log_level', "SYS_DEBUG",
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
            if 'disable_object_logs' in self.sandesh_config and \
                self.sandesh_config['disable_object_logs']:
                args.append('--disable_object_logs')
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
            if 'disable_object_logs' in self.sandesh_config and \
                self.sandesh_config['disable_object_logs']:
                args.append('--SANDESH.disable_object_logs')
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
                keyfile = sandesh_config.get('sandesh_keyfile'),
                certfile = sandesh_config.get('sandesh_certfile'),
                ca_cert = sandesh_config.get('sandesh_ca_cert'),
                sandesh_ssl_enable = \
                    sandesh_config.get('sandesh_ssl_enable', False),
                introspect_ssl_enable = \
                    sandesh_config.get('introspect_ssl_enable', False),
                disable_object_logs = \
                    sandesh_config.get('disable_object_logs', False))
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
        return self.opserver.rest_api_port
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
            self.admin_password, headers={'X-Auth-Token':'user:admin'})
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
        self.logger.info("res %s" % str(res))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            return True

    @retry(delay=1, tries=30)
    def verify_generator_list(self, collectors, exp_genlist):
        actual_genlist = []
        for collector in collectors:
            actual_genlist.extend(self.get_generator_list(collector))
        self.logger.info('generator list: ' + str(set(actual_genlist)))
        self.logger.info('exp generator list: ' + str(set(exp_genlist)))
        return set(actual_genlist) == set(exp_genlist)

    @retry(delay=2, tries=30)
    def verify_generator_uve_list(self, exp_gen_list):
        self.logger.info('verify_generator_uve_list')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # get generator list
        gen_list = vns.uve_query('generators',
            {'cfilt':'ModuleClientState:client_info'})
        try:
            actual_gen_list = [gen['name'] for gen in gen_list]
            self.logger.info('exp generators list: %s' % str(exp_gen_list))
            self.logger.info('actual generators list: %s' % str(actual_gen_list))
            for gen in exp_gen_list:
                if gen not in actual_gen_list:
                    return False
        except Exception as e:
            self.logger.error('Exception: %s' % e)
            return False
        return True
    # end verify_generator_uve_list

    @retry(delay=1, tries=5)
    def verify_generator_connected_times(self, generator, times):
        self.logger.info('verify_generator_connected_times')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        try:
            ModuleClientState  = vns.uve_query('generator/%s' % generator,
                                {'cfilt':'ModuleClientState:client_info'})
            clientinfo = ModuleClientState['ModuleClientState']['client_info']
            self.logger.info('exp successful connections: %s' % times)
            self.logger.info('actual successful connections: %s' %
                                      clientinfo['successful_connections'])
            if clientinfo['successful_connections'] != times:
                return False
        except Exception as e:
            self.logger.error('Exception: %s' % e)
            return False
        return True
    # end verify_generator_connected_times

    @retry(delay=1, tries=6)
    def verify_message_table_messagetype(self):
        self.logger.info("verify_message_table_messagetype")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # query for CollectorInfo logs
        res = vns.post_query(MESSAGE_TABLE,
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
        res = vns.post_query(MESSAGE_TABLE,
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
        res_qe = vns.post_query(MESSAGE_TABLE,
                                start_time='-10m', end_time='now',
                                select_fields=["Type", "Messagetype"],
                                where_clause="ModuleId = contrail-query-engine")
        self.logger.info("res_qe %s" % str(res_qe))
        # query for Collector logs
        res_c = vns.post_query(MESSAGE_TABLE,
                               start_time='-10m', end_time='now',
                               select_fields=["Type", "Messagetype"],
                               where_clause="ModuleId = contrail-collector")
        self.logger.info("res_c %s" % str(res_c))
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
            MESSAGE_TABLE,
            start_time='-10m', end_time='now',
            select_fields=["ModuleId"],
            where_clause=str(where_clause1 + " OR  " + where_clause2))
        self.logger.info("res %s" % str(res))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info("moduleids %s" % str(moduleids))
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
            MESSAGE_TABLE,
            start_time='-10m', end_time='now',
            select_fields=["ModuleId"],
            where_clause=str(where_clause1 + " AND  " + where_clause2))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info("moduleids %s" % str(moduleids))
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
            res = vns.post_query(MESSAGE_TABLE, start_time='-10m',
                    end_time='now', select_fields=[key],
                    where_clause='%s = %s*' % (key, value))
            if not len(res):
                return False
            self.logger.info("res %s" % str(res))
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
        res = vns.post_query(MESSAGE_TABLE,
                             start_time='-10m', end_time='now',
                             select_fields=["ModuleId"],
                             where_clause=str(
                                 where_clause1 + " OR  " + where_clause2),
                             filter="ModuleId = contrail-query-engine")
        self.logger.info("res %s" % str(res))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info("moduleids %s" % str(moduleids))
            if len(moduleids) != 1:  # 1 moduleid: contrail-collector
                return False

        res = vns.post_query(MESSAGE_TABLE,
                              start_time='-10m', end_time='now',
                              select_fields=["ModuleId"],
                              where_clause=str(
                                  where_clause1 + " AND  " + where_clause2),
                              filter="ModuleId = contrail-collector")
        self.logger.info("res %s" % str(res))
        if res != []:
            return False
        return True

    @retry(delay=1, tries=6)
    def verify_message_table_filter2(self):
        self.logger.info("verify_message_table_filter2")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        a_query = Query(table=MESSAGE_TABLE,
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

        a_query = Query(table=MESSAGE_TABLE,
                start_time='now-10m',
                end_time='now',
                select_fields=["ModuleId"],
                filter=[[{"name": "ModuleId", "value": "contrail-collector", "op": 1}], [{"name": "ModuleId", "value": "contrail-analytics-api", "op": 1}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info("res %s" % str(res))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            # 1 moduleid: contrail-collector || contrail-analytics-api
            assert(len(moduleids) == 2 and\
                   "contrail-collector" in moduleids and\
                   "contrail-analytics-api" in moduleids)  
                
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
        res = vns.post_query(MESSAGE_TABLE,
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
            self.logger.info("moduleids %s" % str(moduleids))
            for module in exp_moduleids:
                if module not in moduleids:
                    return False
            expected_res = sorted(res, key=itemgetter('ModuleId'))
            if res != expected_res:
                return False

        # Descending sort
        self.logger.info("verify_message_table_sort:Descending Sort")
        res = vns.post_query(MESSAGE_TABLE,
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
            self.logger.info("moduleids %s" % str(moduleids))
            for module in exp_moduleids:
                if module not in moduleids:
                    return False
            expected_res = sorted(
                res, key=itemgetter('ModuleId'), reverse=True)
            if res != expected_res:
                return False

        # sort + limit
        res = vns.post_query(MESSAGE_TABLE,
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
            self.logger.info("moduleids %s" % str(moduleids))
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
        res = vns.post_query(MESSAGE_TABLE,
                             start_time='-10m', end_time='now',
                             select_fields=['ModuleId', 'Messagetype'],
                             where_clause='', limit=1)
        self.logger.info("res %s" % str(res))
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
        self.logger.info("res %s" % str(res))
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
        self.logger.info("res %s" % str(res))
        if len(res) == gen_obj.vn_sum_rows['rows']:
            return True
        return False 

    @retry(delay=1, tries=10)
 
    def verify_where_query_prefix(self,generator_obj):
        
        self.logger.info('verify where query in FlowSeriesTable')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        vrouter = generator_obj._hostname
        a_query = Query(table="FlowSeriesTable",
                start_time=(generator_obj.session_start_time),
                end_time=(generator_obj.session_end_time),
                select_fields=["sourcevn","sourceip","vrouter"],
                where=[[{"name":"sourcevn","value":"domain1:admin:vn1","op":1},
                        {"name":"destvn","value":"domain1:admin:vn2","op":1}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        assert(len(res)>0)
        a_query = Query(table="FlowSeriesTable",
                start_time=(generator_obj.session_start_time),
                end_time=(generator_obj.session_end_time),
                select_fields=["sourcevn","sourceip","vrouter"],
                where=[[{"name":"protocol","value":1,"op":1}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        assert(len(res)>0)
        a_query = Query(table="FlowSeriesTable",
                start_time=(generator_obj.session_start_time),
                end_time=(generator_obj.session_end_time),
                select_fields=["destvn","SUM(bytes)"],
                where=[[{"name":"sourcevn","value":"","op":7}]])
        json_qstr = json.dumps(a_query.__dict__)
        res = vns.post_query_json(json_qstr)
        assert(len(res)==2)
        return True

    def verify_flow_table(self, generator_obj):
        vrouter = generator_obj._hostname
        # query flow records
        self.logger.info('verify_flow_table')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=[
                                 'UuidKey', 'agg-packets', 'agg-bytes'])
        self.logger.info("FlowRecordTable result:%s" % str(res))
        assert(len(res) == 2*(generator_obj.flow_cnt**2))

        # query based on various WHERE parameters

        # sourcevn and sourceip
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['UuidKey', 'sourcevn', 'sourceip'],
            where_clause='sourceip=10.10.10.1 AND sourcevn=domain1:admin:vn1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt*generator_obj.flow_cnt)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['sourcevn', 'sourceip'],
            where_clause='sourceip=10.10.10.1 AND sourcevn=domain1:admin:vn1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        # give non-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'sourcevn', 'sourceip'],
                             where_clause='sourceip=20.1.1.10 AND vrouter=%s'% vrouter)
        self.logger.info("res %s" % str(res))
        assert(len(res) == 0)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['sourcevn', 'sourceip'],
            where_clause='sourceip=20.1.1.10 AND sourcevn=domain1:admin:vn1 AND vrouter=%s'% vrouter)
        self.logger.info("res %s" % str(res))
        assert(len(res) == 0)

        # destvn and destip
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['UuidKey', 'destvn', 'destip'],
            where_clause='destip=2001:db8::1:2 AND destvn=domain1:admin:vn2'+
                         ' AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt*generator_obj.flow_cnt)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['destvn', 'destip'],
            where_clause='destip=2001:db8::1:2 AND destvn=domain1:admin:vn2'+
                         ' AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        # give non-existent values in the where clause
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['UuidKey', 'destvn', 'destip'],
            where_clause='destip=10.10.10.2 AND ' +
            'destvn=default-domain:default-project:default-virtual-network AND' +
            'vrouter=%s'% vrouter)
        self.logger.info("res %s" % str(res))
        assert(len(res) == 0)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['destvn', 'destip'],
            where_clause='destip=20.1.1.10 AND destvn=domain1:admin:vn2&> AND vrouter=%s'% vrouter)
        self.logger.info("res %s" % str(res))
        assert(len(res) == 0)

        # sourcevn + sourceip AND destvn + destip [ipv4/ipv6]
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['UuidKey', 'sourcevn', 'sourceip',
                'destvn', 'destip'],
            where_clause='sourceip=2001:db8::1:2 AND '+
                'sourcevn=domain1:admin:vn2 AND '+
                'destip=10.10.10.1 AND destvn=domain1:admin:vn1 AND '+
                'vrouter=%s'% vrouter,
            dir=0)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt*generator_obj.flow_cnt)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['sourcevn', 'sourceip', 'destvn', 'destip'],
            where_clause='sourceip=2001:db8::1:2 AND '+
                'sourcevn=domain1:admin:vn2 AND '+
                'destip=10.10.10.1 AND destvn=domain1:admin:vn1 AND '+
                'vrouter=%s'% vrouter,
            dir=0)
        self.logger.info(str(res))
        assert(len(res) == 1)

        # sport and protocol
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'sport', 'protocol'],
                             where_clause='sport=32777 AND protocol=0 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        # give no-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'sport', 'protocol'],
                             where_clause='sport=20 AND protocol=17 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)

        # dport and protocol
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'dport', 'protocol'],
                             where_clause='dport=102 AND protocol=1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)

        # give no-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'dport', 'protocol'],
                             where_clause='dport=10 AND protocol=17 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)

        # sort and limit
        res = vns.post_query(
            'FlowRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['UuidKey', 'protocol'], where_clause='vrouter=%s'% vrouter,
            sort_fields=['protocol'], sort=1)
        self.logger.info(str(res))
        assert(len(res) == 2 * (generator_obj.flow_cnt ** 2))
        assert(res[0]['protocol'] == 0)

        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['protocol'], where_clause='vrouter=%s'% vrouter,
                             sort_fields=['protocol'], sort=1, limit=1)
        self.logger.info(str(res))
        assert(len(res) == 1)
        assert(res[0]['protocol'] == 0)

        # limit without sort
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['protocol'], where_clause='vrouter=%s'% vrouter,
                             limit=5)
        self.logger.info(str(res))
        assert(len(res) == 5)

        # Filter by action
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'action', 'drop_reason'],
                             where_clause='vrouter=%s'% vrouter,
                             filter='action=drop')
        self.logger.info(str(res))
        assert(len(res) == 2 * (generator_obj.flow_cnt**2))

        # verify vmi field
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'vmi'],
                             where_clause='vrouter=%s AND sourceip=10.10.10.1'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt**2)
        for r in res:
            assert(r['vmi'] == generator_obj.client_vmi)
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'vmi'],
                             where_clause='vrouter=%s AND sourceip=2001:db8::1:2'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt**2)
        for r in res:
            assert(r['vmi'] == generator_obj.server_vmi)

        # verify vmi with filter
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'vmi'],
                             where_clause='vrouter=%s'% vrouter,
                             filter='vmi=%s'% generator_obj.client_vmi)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt**2)
        for r in res:
            assert(r['vmi'] == generator_obj.client_vmi)

        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['UuidKey', 'vmi'],
                             where_clause='vrouter=%s'% vrouter,
                             filter='vmi=%s'% str(uuid.uuid1()))
        self.logger.info("res %s" % str(res))
        assert(len(res) == 0)

        # Range query on sport
        res = vns.post_query('FlowRecordTable',
                  start_time=str(generator_obj.session_start_time),
                  end_time=str(generator_obj.session_end_time),
                  select_fields=['UuidKey', 'sport', 'protocol'],
                  where_clause='(sport=32747<32817 AND protocol=0 AND \
                      vrouter=%s)' % vrouter)
        self.logger.info(str(res))
        assert(len(res) == 6)

        return True
    # end verify_flow_table

    @retry(delay=1, tries=10)
    def verify_session_samples(self, generator_obj):
        self.logger.info("verify_session_samples")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        vrouter = generator_obj._hostname
        res = vns.post_query('SessionSeriesTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['T'], session_type="client")
        self.logger.info(str(res))
        if len(res) != generator_obj.client_session_cnt:
            self.logger.error("Session samples client: Actual %d expected %d" %
                (len(res), generator_obj.client_session_cnt))
            return False

        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        vrouter = generator_obj._hostname
        result = vns.post_query('SessionSeriesTable',
                             start_time=str(generator_obj.session_start_time),
                             end_time=str(generator_obj.session_end_time),
                             select_fields=['T'], session_type="server")
        self.logger.info(str(result))
        if len(result) != generator_obj.server_session_cnt:
            self.logger.error("Session samples server: Actual %d expected %d" %
                (len(res), generator_obj.server_session_cnt))
            return False

        return True
    #end verify_session_samples

    def verify_session_table(self, generator_obj):

        self.logger.info('verify_session_table')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # query session records
        res = vns.post_query('SessionRecordTable',
                        start_time=str(generator_obj.session_start_time),
                        end_time=str(generator_obj.session_end_time),
                        select_fields=[
                            'forward_flow_uuid',
                            'reverse_flow_uuid',
                            'client_port',
                            'server_port',
                            'forward_teardown_bytes',
                            'reverse_teardown_bytes'],
                        session_type="client")
        self.logger.info("SessionRecordTable result:%s" % str(res))
        assert(len(res) == generator_obj.flow_cnt*generator_obj.flow_cnt)

        # query based on various WHERE parameters and filters

        # vn and remote_vn
        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['forward_flow_uuid',
                           'reverse_flow_uuid', 'vn', 'remote_vn'],
            where_clause='vn=domain1:admin:vn1 AND remote_vn=domain1:admin:vn2',
            session_type="client", is_service_instance = 0)
        self.logger.info(str(res))
        self.logger.info("exp_len:" + str(generator_obj.flow_cnt) + " res_len:" + str(len(res)))
        assert(len(res) == generator_obj.flow_cnt*generator_obj.flow_cnt)

        # give non-existent values in the where clause
        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['forward_flow_uuid',
                           'reverse_flow_uuid', 'vn', 'remote_vn'],
            where_clause='vn=domain1:admin:vn10 AND remote_vn=domain1:admin:vn2',
            session_type="client")
        self.logger.info(str(res))
        assert(len(res) == 0)

        # local_ip and server_port AND protocol
        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['forward_flow_uuid',
                           'reverse_flow_uuid', 'vn', 'remote_vn'],
            where_clause='local_ip=10.10.10.1 AND server_port=102 AND protocol=1',
            session_type="client")
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        # non-existent values in the where clause
        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['forward_flow_uuid',
                           'reverse_flow_uuid', 'server_port', 'protocol'],
            where_clause='server_port=123 AND protocol=22',
            session_type="client")
        self.logger.info(str(res))
        assert(len(res) == 0)

        # do not provide uuid in select, should still include it in the results
        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['vn', 'remote_vn'],
            where_clause='local_ip=10.10.10.1 AND server_port=102 AND protocol=1',
            session_type="client")
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        for record in res:
            assert('forward_flow_uuid' in record)
            assert('reverse_flow_uuid' in record)

        # filter by remote_ip and client_port
        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['forward_flow_uuid', 'reverse_flow_uuid',
                           'remote_ip', 'client_port'],
            filter='client_port=32787',
            session_type="client")
        self.logger.info(str(res))
        assert(len(res) == 1)

        # sort and limit
        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['forward_flow_uuid', 'reverse_flow_uuid',
                           'protocol', 'server_port'],
            session_type="client",
            sort_fields=['protocol'], sort=1)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt*generator_obj.flow_cnt)
        assert(res[0]['protocol'] == 0)

        res = vns.post_query(
            'SessionRecordTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['forward_flow_uuid', 'reverse_flow_uuid',
                           'protocol', 'server_port'],
            session_type="client",
            limit=5)
        self.logger.info(str(res))
        assert(len(res) == 5)

        # vrouter and vrouter_ip
        res = vns.post_query(
               'SessionRecordTable',
                start_time=str(generator_obj.session_start_time),
                end_time=str(generator_obj.session_end_time),
                select_fields=['forward_flow_uuid', 'reverse_flow_uuid',
                                'vrouter', 'vrouter_ip'],
                session_type="client")
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt*generator_obj.flow_cnt)
        for r in res:
            assert(r['vrouter'] == generator_obj._hostname)
            assert(r['vrouter_ip'] == '10.0.0.1')
        #give non-existent values as filter
        res = vns.post_query(
               'SessionRecordTable',
                start_time=str(generator_obj.session_start_time),
                end_time=str(generator_obj.session_end_time),
                select_fields=['forward_flow_uuid', 'reverse_flow_uuid',
                                'vrouter', 'vrouter_ip'],
                filter='vrouter=a6s45',
                session_type="client")
        assert(len(res) == 0)
        res = vns.post_query(
               'SessionRecordTable',
                start_time=str(generator_obj.session_start_time),
                end_time=str(generator_obj.session_end_time),
                select_fields=['forward_flow_uuid', 'reverse_flow_uuid',
                                'vrouter', 'vrouter_ip'],
                filter='vrouter_ip=10.0.2.2',
                session_type="client")
        assert(len(res) == 0)

        return True
    # end verify_session_table

    def verify_session_series_aggregation_binning(self, generator_obj):
        vrouter = generator_obj._hostname
        self.logger.info('verify_session_series_aggregation_binning')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)

        #Helper function for stats aggregation
        def _aggregate_stats(session, ip=None, port=None, protocol=None):
            stats = {'sum_fwd_bytes':0, 'sum_rev_pkts':0}
            for key, agg_info in session.session_data[0].sess_agg_info.iteritems():
                if (ip is None or ip == key.ip) and \
                    (port is None or port == key.port) and \
                    (protocol is None or protocol == key.protocol):
                    stats['sum_fwd_bytes'] += agg_info.sampled_forward_bytes
                    stats['sum_rev_pkts'] += agg_info.sampled_reverse_pkts
            return stats

        def _aggregate_session_stats(sessions, start_time, end_time, ip=None,
                port=None, protocol=None):
            stats = {'sum_fwd_bytes':0, 'sum_rev_pkts':0}
            for session in sessions:
                if session._timestamp < start_time:
                        continue
                elif session._timestamp > end_time:
                        break
                s = _aggregate_stats(session, ip, port, protocol)
                stats['sum_fwd_bytes'] += s['sum_fwd_bytes']
                stats['sum_rev_pkts'] += s['sum_rev_pkts']
            return stats

        # 1. stats
        self.logger.info('SessionSeries: [SUM(forward_sampled_bytes), SUM(reverse_sampled_pkts), sample_count]')
        res = vns.post_query(
            'SessionSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['SUM(forward_sampled_bytes)', 'SUM(reverse_sampled_pkts)', 'sample_count', 'vrouter'],
            filter='vrouter=%s'% vrouter, session_type="client")
        self.logger.info(str(res))
        assert(len(res) == 1)
        exp_sum_fwd_bytes = exp_sum_rev_pkts = 0
        for s in generator_obj.client_sessions:
            for agg_info in s.session_data[0].sess_agg_info.values():
                exp_sum_fwd_bytes += agg_info.sampled_forward_bytes
                exp_sum_rev_pkts += agg_info.sampled_reverse_pkts
        assert(res[0]['SUM(forward_sampled_bytes)'] == exp_sum_fwd_bytes)
        assert(res[0]['SUM(reverse_sampled_pkts)'] == exp_sum_rev_pkts)
        assert(res[0]['sample_count'] ==
            generator_obj.client_session_cnt*generator_obj.client_session_cnt)

        # 2. session tuple + stats
        self.logger.info(
            'SessionSeries: [server_port, local_ip, \
                SUM(forward_sampled_bytes), SUM(reverse_sampled_pkts), sample_count]')
        # Each session msg has unique (deployment, application, stie, tier).
        # Therefore, the following query
        # should return # records equal to the # client sessions.
        res = vns.post_query(
            'SessionSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['deployment', 'tier', 'application', 'site', 
                           'SUM(forward_sampled_bytes)',
                           'SUM(reverse_sampled_pkts)', 'sample_count'],
            session_type="client")
        self.logger.info(str(res))
        assert(len(res) == generator_obj.client_session_cnt)
        for r in res:
            sum_fwd_bytes = 0
            sum_rev_pkts = 0
            for s in generator_obj.client_sessions:
                if r['deployment'] == s.session_data[0].deployment:
                    for key,agg_info in s.session_data[0].sess_agg_info.iteritems():
                        sum_fwd_bytes += agg_info.sampled_forward_bytes
                        sum_rev_pkts += agg_info.sampled_reverse_pkts
                    assert(r['SUM(forward_sampled_bytes)'] == sum_fwd_bytes)
                    assert(r['SUM(reverse_sampled_pkts)'] == sum_rev_pkts)
                    assert(r['sample_count'] == generator_obj.client_session_cnt)
                    break

        # all session msgs have same vn-remote_vn hence following query should
        # return 1 record
        res = vns.post_query(
            'SessionSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['vn', 'remote_vn',
                           'SUM(forward_sampled_bytes)',
                           'SUM(reverse_sampled_pkts)', 'sample_count'],
            session_type="server")
        self.logger.info(str(res))
        assert(len(res) == 1)
        sum_fwd_bytes = 0
        sum_rev_pkts = 0
        exp_sample_cnt = 0
        for s in generator_obj.server_sessions:
            for key,agg_info in s.session_data[0].sess_agg_info.iteritems():
                sum_fwd_bytes += agg_info.sampled_forward_bytes
                sum_rev_pkts += agg_info.sampled_reverse_pkts
                exp_sample_cnt += 1
        assert(res[0]['SUM(forward_sampled_bytes)'] == sum_fwd_bytes)
        assert(res[0]['SUM(reverse_sampled_pkts)'] == sum_rev_pkts)
        assert(res[0]['sample_count'] == exp_sample_cnt)

        # sort results by aggregate column
        res = vns.post_query('SessionSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['protocol', 'server_port',
                           'SUM(forward_sampled_bytes)',
                           'SUM(reverse_sampled_pkts)'],
            session_type="client",
            sort_fields=['SUM(forward_sampled_bytes)'], sort=1, limit=3)
        self.logger.info(str(res))
        assert(len(res) == 3)
        server_port = 100
        sum_fwd_bytes = 180
        sum_rev_pkts = 9
        for r in res:
            assert(r['server_port'] == server_port)
            assert(r['SUM(forward_sampled_bytes)'] == sum_fwd_bytes)
            assert(r['SUM(reverse_sampled_pkts)'] == sum_rev_pkts)
            server_port += 1
            sum_fwd_bytes += 180
            sum_rev_pkts += 9

        # 3. T=<granularity> + stats
        self.logger.info('SessionSeries: [T=<x>, SUM(forward_sampled_bytes), SUM(reverse_sampled_pkts)]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (30 * 1000 * 1000))
        granularity = 10
        gms = granularity * 1000 * 1000 # in micro seconds
        res = vns.post_query(
            'SessionSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'SUM(forward_sampled_bytes)',
                           'SUM(reverse_sampled_pkts)', 'vrouter'],
            session_type = "client",
            filter='vrouter=%s' % vrouter)
        diff_t = int(et) - int(st)
        num_records = (diff_t/gms) + bool(diff_t%gms)
        assert(len(res) == num_records)
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts = [res_start_time + (x * gms) for x in range(num_records)]
        exp_result = {}
        for t in ts:
            end_time = t + gms
            if end_time > int(et):
                end_time = int(et)
            exp_result[t] = _aggregate_session_stats(generator_obj.client_sessions,
                                                t, end_time)
        self.logger.info('exp_result: %s' % str(exp_result))
        self.logger.info('res: %s' % str(res))
        assert(len(exp_result) == num_records)
        for r in res:
            try:
                stats = exp_result[r['T=']]
            except KeyError:
                assert(False)
            assert(r['SUM(forward_sampled_bytes)'] == stats['sum_fwd_bytes'])
            assert(r['SUM(reverse_sampled_pkts)'] == stats['sum_rev_pkts'])

        # 4. T=<granularity + tuple + stats
        self.logger.info('SessionSeries: [T=<x>, protocol, \
            SUM(forward_sampled_bytes), SUM(reverse_sampled_pkts)]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (30 * 1000 * 1000))
        granularity = 10
        gms = granularity * 1000 * 1000 # in micro seconds
        res = vns.post_query(
            'SessionSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'protocol',
                            'SUM(forward_sampled_bytes)',
                            'SUM(reverse_sampled_pkts)', 'vrouter'],
            filter='vrouter=%s' % vrouter,
            session_type = "client")
        diff_t = int(et) - int(st)
        num_ts = (diff_t/gms) + bool(diff_t%gms)
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts_list = [res_start_time + (x * gms) \
              for x in range(num_ts)]
        protos = [0, 1]
        exp_result = {}
        for i in protos:
            ts_stats = {}
            for ts in ts_list:
                end_time = ts + gms
                if end_time > int(et):
                    end_time = int(et)
                ts_stats[ts] = _aggregate_session_stats(generator_obj.client_sessions, 
                        ts, end_time, protocol=i)
            exp_result[i] = ts_stats
        self.logger.info('exp_result: %s' % str(exp_result))
        self.logger.info('res: %s' % str(res))
        assert(len(res) == 6)
        for r in res:
            try:
                stats = exp_result[r['protocol']][r['T=']]
            except KeyError:
                assert(False)
            assert(r['SUM(forward_sampled_bytes)'] == stats['sum_fwd_bytes'])
            assert(r['SUM(reverse_sampled_pkts)'] == stats['sum_rev_pkts'])

        # 5. T=<granularity> + tuples
        self.logger.info(
            'SessionSeriesTable: [T=<x>, protocol, vn, remote_vn]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + 30 * 1000 * 1000)
        granularity = 10
        gms = 10 * 1000 * 1000
        res = vns.post_query(
            'SessionSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'protocol', 'vn', 'remote_vn'],
            filter="vrouter=%s" % (vrouter),
            session_type="client")
        diff_t = int(et) - int(st)
        num_ts = (diff_t/gms) + bool(diff_t%gms)
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts = [res_start_time + (x * gms) \
                for x in range(num_ts)]
        exp_result = []

        exp_result_cnt = 0
        proto = [0, 1]
        vn = generator_obj.client_sessions[0].session_data[0].vn
        remote_vn = generator_obj.client_sessions[0].session_data[0].remote_vn
        for t in ts:
            for i in proto:
                exp_result.append({'T=':t, 'vn':vn,
                                              'remote_vn':remote_vn,
                                              'protocol':i})
                exp_result_cnt += 1
        self.logger.info("exp_result:" + str(exp_result))
        self.logger.info("res" + str(res))
        assert(exp_result_cnt == len(res))
        for r in res:
            found = 0
            for exp_res in exp_result:
                if ((r['T='] == exp_res['T=']) and \
                    (r['vn'] == exp_res['vn']) and \
                    (r['remote_vn'] == exp_res['remote_vn']) and \
                    (r['protocol'] == exp_res['protocol'])):
                    found = 1
                    break
            assert(found)

        # 6. Timestamp + stats
        self.logger.info('Sessionseries: [T, forward_sampled_bytes, \
                                           reverse_sampled_pkts]')
        res  = vns.post_query(
                'SessionSeriesTable',
                start_time = str(generator_obj.session_start_time),
                end_time = str(generator_obj.session_end_time),
                select_fields=['T', 'forward_sampled_bytes', 'reverse_sampled_pkts'],
                where_clause='server_port=102 AND protocol=1',
                session_type="client")
        self.logger.info(str(res))

        server_port = 102
        assert(len(res) == generator_obj.client_session_cnt)
        for r in res:
            found = 0
            for session_obj in generator_obj.client_sessions:
                session = session_obj.session_data[0]
                if (r['T'] == session_obj._timestamp):
                    for key, agg_info in session.sess_agg_info.iteritems():
                        if(key.service_port == server_port):
                            assert(r['forward_sampled_bytes'] ==
                                agg_info.sampled_forward_bytes)
                            assert(r['reverse_sampled_pkts'] ==
                                agg_info.sampled_reverse_pkts)
                            found = 1
            assert(found)

        # 7. Timestamp + tuple + stats
        self.logger.info('Sessionseries: [T, deployment, protocol, \
                                            forward_sampled_bytes, \
                                            reverse_sampled_pkts]')
        res  = vns.post_query(
                'SessionSeriesTable',
                start_time = str(generator_obj.session_start_time),
                end_time = str(generator_obj.session_end_time),
                select_fields=['T', 'deployment', 'protocol',
                               'forward_sampled_bytes', 'reverse_sampled_pkts'],
                where_clause='server_port=101 AND protocol=0',
                session_type="client")
        self.logger.info(str(res))

        server_port = 101
        proto = 0
        assert(len(res) == generator_obj.client_session_cnt)
        for r in res:
            found = 0
            for session_obj in generator_obj.client_sessions:
                session = session_obj.session_data[0]
                if (r['T'] == session_obj._timestamp):
                    for key, agg_info in session.sess_agg_info.iteritems():
                        if (key.service_port == server_port and key.protocol == proto):
                            assert(r['forward_sampled_bytes'] ==
                                agg_info.sampled_forward_bytes)
                            assert(r['reverse_sampled_pkts'] ==
                                agg_info.sampled_reverse_pkts)
                            assert(r['deployment'] == session.deployment)
                            found = 1
            assert(found)

        # 8. Timestamp + tuple
        self.logger.info('SessionSeriesTable: [T, deployment, protocol, server_port]')
        res = vns.post_query(
                'SessionSeriesTable',
                start_time = str(generator_obj.session_start_time),
                end_time = str(generator_obj.session_end_time),
                select_fields=['T', 'deployment', 'protocol', 'server_port'],
                session_type="client")
        self.logger.info(str(res))
        assert(len(res) == generator_obj.client_session_cnt * generator_obj.flow_cnt)
        for r in res:
            found = 0
            for session_obj in generator_obj.client_sessions:
                session = session_obj.session_data[0]
                if (r['T'] == session_obj._timestamp):
                    for key, agg_info in session.sess_agg_info.iteritems():
                        if (key.service_port == r['server_port']):
                            assert(r['protocol'] == key.protocol)
                            assert(r['deployment'] == session.deployment)
                            found = 1
            assert(found)

        # 9. T= + tuple + stats (with unrolling)
        self.logger.info('SessionSeriesTable: [T=, deployment, \
                                                server_port, remote_ip, \
                                                SUM(forward_sampled_bytes), \
                                                SUM(reverse_sampled_pkts)]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (30 * 1000 * 1000))
        granularity = 5
        gms = 5 * 1000 * 1000
        res = vns.post_query(
                'SessionSeriesTable',
                start_time = st, end_time = et,
                select_fields=['T=%s'%str(granularity), 'deployment',
                                'protocol', 'remote_ip',
                                'SUM(forward_sampled_bytes)',
                                'SUM(reverse_sampled_pkts)'],
                session_type="client")
        self.logger.info(str(res))
        assert(len(res) == 6)

        diff_t = int(et) - int(st)
        num_ts = (diff_t/gms) + bool(diff_t%gms)
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts_list = [res_start_time + (x * gms) \
              for x in range(num_ts)]
        protos = [0, 1]
        exp_result = {}
        for i in protos:
            ts_stats = {}
            for ts in ts_list:
                end_time = ts + gms
                if end_time > int(et): end_time = int(et)
                ts_stats[ts] = _aggregate_session_stats(generator_obj.client_sessions,
                        ts, end_time, protocol=i)
            exp_result[i] = ts_stats
        self.logger.info("expected_results: %s" % str(exp_result))

        for r in res:
            try:
                stats = exp_result[r['protocol']][r['T=']]
            except KeyError:
                assert(False)
            assert(r['SUM(forward_sampled_bytes)'] == stats['sum_fwd_bytes'])
            assert(r['SUM(reverse_sampled_pkts)'] == stats['sum_rev_pkts'])

        # 10. tuple + stats (filter by action)
        self.logger.info('SessionSeriesTable: [server_port, forward_action, \
                                                SUM(forward_sampled_bytes), \
                                                SUM(reverse_sampled_pkts)]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (30 * 1000 * 1000))
        action = 'drop'
        res = vns.post_query(
                'SessionSeriesTable',
                start_time = st, end_time = et,
                select_fields=['server_port', 'forward_action',
                                'SUM(forward_sampled_bytes)',
                                'SUM(reverse_sampled_pkts)'],
                session_type="client",
                filter='forward_action=%s'%action)
        self.logger.info("results: %s" % str(res))
        assert(len(res) == 3)

        return True
    # end verify_session_series_aggregation_binning

    def verify_flow_series_aggregation_binning(self, generator_object):
        generator_obj = generator_object[0]
        vrouter = generator_obj._hostname
        self.logger.info('verify_flow_series_aggregation_binning')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)

        # 1. stats
        self.logger.info('Flowseries: [sum(bytes), sum(packets)]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['SUM(bytes)', 'SUM(packets)'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info("res %s" % str(res))
        assert(len(res) == 1)
        exp_sum_pkts = exp_sum_bytes = 0

        for flow in generator_obj.forward_flows:
            exp_sum_pkts += flow.sampled_pkts
            exp_sum_bytes += flow.sampled_bytes
        for flow in generator_obj.reverse_flows:
            exp_sum_pkts += flow.sampled_pkts
            exp_sum_bytes += flow.sampled_bytes

        assert(res[0]['SUM(packets)'] == 3*exp_sum_pkts)
        assert(res[0]['SUM(bytes)'] == 3*exp_sum_bytes)

        # 2. flow tuple + stats
        self.logger.info(
            'Flowseries: [sport, dport, sum(bytes), sum(packets)]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['sport', 'dport', 'SUM(bytes)',
                           'SUM(packets)'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 2 * (generator_obj.flow_cnt ** 2))
        exp_sum_bytes = {}
        exp_sum_pkts = {}
        for i in range(generator_obj.flow_cnt*generator_obj.flow_cnt):
            sport = 32747 + i*10
            dport = 100 + i/3
            if sport not in exp_sum_bytes:
                exp_sum_bytes[sport] = {}
            exp_sum_bytes[sport][dport] = generator_obj.forward_flows[i].sampled_bytes
            if sport not in exp_sum_pkts:
                exp_sum_pkts[sport] = {}
            exp_sum_pkts[sport][dport] = generator_obj.forward_flows[i].sampled_pkts
        for i in range(generator_obj.flow_cnt*generator_obj.flow_cnt):
            sport = 100 + i/3
            dport = 32747 + i*10
            if sport not in exp_sum_bytes:
                exp_sum_bytes[sport] = {}
            exp_sum_bytes[sport][dport] = generator_obj.reverse_flows[i].sampled_bytes
            if sport not in exp_sum_pkts:
                exp_sum_pkts[sport] = {}
            exp_sum_pkts[sport][dport] = generator_obj.reverse_flows[i].sampled_pkts
        self.logger.info(str(exp_sum_bytes))
        self.logger.info(str(exp_sum_pkts))
        for r in res:
            assert(r['sport'] in exp_sum_bytes)
            assert(r['dport'] in exp_sum_bytes[r['sport']])
            assert(r['SUM(bytes)'] == 3*exp_sum_bytes[r['sport']][r['dport']])
            assert(r['SUM(packets)'] == 3*exp_sum_pkts[r['sport']][r['dport']])

        self.logger.info('Flowseries: [sourcevn, destvn, sum(bytes), sum(pkts)]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['sourcevn', 'destvn', 'SUM(bytes)', 'SUM(packets)'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 2)
        exp_sum_pkts  = [0, 0]
        exp_sum_bytes = [0, 0]
        for i in range(generator_obj.flow_cnt * generator_obj.flow_cnt):
            exp_sum_pkts[0] += generator_obj.forward_flows[i].sampled_pkts
            exp_sum_bytes[0] += generator_obj.forward_flows[i].sampled_bytes
        for i in range(generator_obj.flow_cnt * generator_obj.flow_cnt):
            exp_sum_pkts[1] += generator_obj.reverse_flows[i].sampled_pkts
            exp_sum_bytes[1] += generator_obj.reverse_flows[i].sampled_bytes
        self.logger.info(str(exp_sum_bytes))
        self.logger.info(str(exp_sum_pkts))
        for r in res:
            if r['sourcevn'] == 'domain1:admin:vn1':
                assert(r['SUM(bytes)'] == 3*exp_sum_bytes[0])
                assert(r['SUM(packets)'] == 3*exp_sum_pkts[0])
            elif r['sourcevn'] == 'domain1:admin:vn2':
                assert(r['SUM(bytes)'] == 3*exp_sum_bytes[1])
                assert(r['SUM(packets)'] == 3*exp_sum_pkts[1])
            else:
                assert(False)

        # 3. T=<granularity> + stats
        self.logger.info('Flowseries: [T=<x>, sum(bytes), sum(packets)]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (30 * 1000 * 1000))
        granularity = 10
        gms = granularity * 1000 * 1000 # in micro seconds
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'SUM(bytes)',
                           'SUM(packets)'],
            where_clause='sourcevn=domain1:admin:vn2 ' +
            'AND destvn=domain1:admin:vn1 AND vrouter=%s'% vrouter)
        diff_t = int(et) - int(st)
        num_records = (diff_t/(10*1000*1000)) + bool(diff_t%(10*1000*1000))
        #assert(len(res) == num_records)
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts = [res_start_time + (x * gms) for x in range(num_records)]
        exp_result = {}
        for t in ts:
            end_time = t + gms
            if end_time > int(et):
                end_time = int(et)
            exp_result[t] = {'SUM(bytes)':exp_sum_bytes[1],
                             'SUM(packets)':exp_sum_pkts[1]}
        self.logger.info('exp_result: %s' % str(exp_result))
        self.logger.info('res: %s' % str(res))
        assert(len(exp_result) == num_records)
        for r in res:
            try:
                stats = exp_result[r['T=']]
            except KeyError:
                assert(False)
            assert(r['SUM(bytes)'] == stats['SUM(bytes)'])
            assert(r['SUM(packets)'] == stats['SUM(packets)'])

        # 4. T=<granularity> + tuples + stats
        self.logger.info(
            'Flowseries: [T=<x>, protocol, sport, sum(bytes), sum(packets)]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (20 * 1000 * 1000))
        granularity = 5
        gms = 5 * 1000 * 1000
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'sport', 'protocol', 'SUM(bytes)',
                           'SUM(packets)'],
            where_clause='vrouter=%s'% vrouter)
        diff_t = int(et) - int(st)
        num_ts = (diff_t/(10*1000*1000)) + bool(diff_t%(10*1000*1000))
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts = [res_start_time + (x * 10*1000*1000) for x in range(num_records)]

        exp_result = {}
        client_session = generator_obj.client_sessions[0].session_data[0]
        for key, value in client_session.sess_agg_info.iteritems():
            for key2, value2 in value.sessionMap.iteritems():
                if not key2.port in exp_result:
                    exp_result[key2.port] = {'protocol':key.protocol,
                                             'SUM(bytes)':value2.forward_flow_info.sampled_bytes,
                                             'SUM(packets)':value2.forward_flow_info.sampled_pkts}
                else:
                    exp_result[key2.port]['SUM(bytes)'] += value2.forward_flow_info.sampled_bytes
                    exp_result[key2.port]['SUM(packets)'] += value2.forward_flow_info.sampled_pkts
        server_session = generator_obj.server_sessions[0].session_data[0]
        for key, value in server_session.sess_agg_info.iteritems():
            for key2, value2 in value.sessionMap.iteritems():
                if not key.service_port in exp_result:
                    exp_result[key.service_port] = {'protocol':key.protocol,
                                             'SUM(bytes)':value2.reverse_flow_info.sampled_bytes,
                                             'SUM(packets)':value2.reverse_flow_info.sampled_pkts}
                else:
                    exp_result[key.service_port]['SUM(bytes)'] += value2.reverse_flow_info.sampled_bytes
                    exp_result[key.service_port]['SUM(packets)'] += value2.reverse_flow_info.sampled_pkts

        self.logger.info('exp_result: %s' % str(exp_result))
        self.logger.info('res: %s' % str(res))
        assert(len(res) == len(ts)*len(exp_result))
        for r in res:
            try:
                stats = exp_result[r['sport']]
            except KeyError:
                assert(False)
            assert(r['protocol'] == stats['protocol'])
            assert(r['SUM(bytes)'] == stats['SUM(bytes)'])
            assert(r['SUM(packets)'] == stats['SUM(packets)'])
        
        # 6. direction_ing + stats
        self.logger.info('Flowseries: [direction_ing, SUM(bytes), SUM(packets)')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['direction_ing', 'SUM(bytes)', 'SUM(packets)'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        exp_sum_pkts = exp_sum_bytes = 0
        for flow in generator_obj.forward_flows:
            exp_sum_pkts += flow.sampled_pkts
            exp_sum_bytes += flow.sampled_bytes
        for flow in generator_obj.reverse_flows:
            exp_sum_pkts += flow.sampled_pkts
            exp_sum_bytes += flow.sampled_bytes
        self.logger.info("exp_sum_bytes:" + str(exp_sum_bytes))
        self.logger.info("exp_sum_pkts:" + str(exp_sum_pkts))
        assert(res[0]['SUM(packets)'] == 3*exp_sum_pkts)
        assert(res[0]['SUM(bytes)'] == 3*exp_sum_bytes)
        assert(res[0]['direction_ing'] == 1)
        
        self.logger.info('Flowseries: [direction_ing, SUM(bytes), SUM(packets)]')
        result = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['direction_ing', 'SUM(bytes)', 'SUM(packets)'],
            where_clause='vrouter=%s'% vrouter, dir=0)
        self.logger.info("result %s" % str(result))
        assert(len(result) == 1)
        assert(result[0]['SUM(packets)'] == 3*exp_sum_pkts)
        assert(result[0]['SUM(bytes)'] == 3*exp_sum_bytes)
        assert(result[0]['direction_ing'] == 0)
        
        # 7. T=<granularity> + tuples
        self.logger.info(
            'Flowseries: [T=<x>, sourcevn, destvn, sport, dport, protocol]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (20 * 1000 * 1000))
        granularity = 10
        gms = 10 * 1000 * 1000
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity), 'protocol', 'sourcevn', 'destvn',
                           'sport', 'dport'],
            where_clause='vrouter=%s'% vrouter)
        diff_t = int(et) - int(st)
        num_ts = (diff_t/gms) + bool(diff_t%gms)
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts = [res_start_time + (x * gms) for x in range(num_records)]
        exp_result = []

        client_session = generator_obj.client_sessions[0].session_data[0]
        server_session = generator_obj.server_sessions[0].session_data[0]
        for t in ts:
            for key, value in client_session.sess_agg_info.iteritems():
                for key2, value2 in value.sessionMap.iteritems():
                    exp_result.append({'protocol':key.protocol,
                                       'sourcevn':client_session.vn,
                                       'destvn':client_session.remote_vn,
                                       'sport':key2.port,
                                       'dport':key.service_port,
                                       'T=':t,
                                       'vrouter':vrouter})
            for key, value in server_session.sess_agg_info.iteritems():
                for key2, value2 in value.sessionMap.iteritems():
                    exp_result.append({'protocol':key.protocol,
                                       'sourcevn':server_session.vn,
                                       'destvn':server_session.remote_vn,
                                       'sport':key.service_port,
                                       'dport':key2.port,
                                       'T=':t,
                                       'vrouter':vrouter})

        assert(len(exp_result) == len(res))
        res = sorted(res, key=lambda r: (r['sport'], r['dport']))
        exp_result = sorted(exp_result, key=lambda r: (r['sport'], r['dport']))
        self.logger.info("res:" + str(res))
        self.logger.info("exp_res:" + str(exp_result))
        assert(res == exp_result)

        # 11. T=<granularity>
        self.logger.info('Flowseries: [T=<x>]')
        st = str(generator_obj.session_start_time)
        et = str(generator_obj.session_start_time + (15 * 1000 * 1000))
        granularity = 10
        res = vns.post_query(
            'FlowSeriesTable', start_time=st, end_time=et,
            select_fields=['T=%s' % (granularity)],
            where_clause='sourcevn=domain1:admin:vn1' +
            'AND destvn=domain1:admin:vn2')
        diff_t = int(et) - int(st)
        num_ts = (diff_t/(10*1000*1000)) + bool(diff_t%(10*1000*1000))
        ts = []
        res_start_time = generator_obj.session_start_time - \
                            (generator_obj.session_start_time % gms)
        ts = [{'T=':res_start_time + (x * 10*1000*1000)} for x in range(num_ts)]
        self.logger.info(str(res))
        self.logger.info(str(ts))
        assert(res == ts)

        # 12. Flow tuple
        self.logger.info('Flowseries: [protocol, sport, dport]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['protocol', 'sport', 'dport'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))

        exp_result = []
        session = generator_obj.client_sessions[0].session_data[0]
        for key, value in session.sess_agg_info.iteritems():
            for key2, value2 in value.sessionMap.iteritems():
                dict = {'protocol':key.protocol, 'sport':key2.port,
                        'dport':key.service_port,
                        'bytes':value2.forward_flow_info.sampled_bytes,
                        'packets':value2.forward_flow_info.sampled_pkts}
                exp_result.append(dict)
        session = generator_obj.server_sessions[0].session_data[0]
        for key, value in session.sess_agg_info.iteritems():
            for key2, value2 in value.sessionMap.iteritems():
                dict = {'protocol':key.protocol, 'sport':key.service_port,
                        'dport':key2.port,
                        'bytes':value2.reverse_flow_info.sampled_bytes,
                        'packets':value2.reverse_flow_info.sampled_pkts}
                exp_result.append(dict)
        self.logger.info(exp_result)
        assert(len(res) == len(exp_result))
        for exp_r in exp_result:
            found = 0
            for r in res:
                if exp_r['sport'] == r['sport'] and exp_r['dport'] == r['dport'] and \
                    exp_r['protocol'] == r['protocol']:
                    found = 1
            assert(found)

        # T + flow tuple
        self.logger.info('Flowseries: [T, protocol, sport, dport]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['T', 'protocol', 'sport', 'dport'],
            where_clause='sourcevn=domain1:admin:vn1' +
                'AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 27)

        # 14. T + flow tuple + stats
        self.logger.info('Flowseries: [T, protocol, sport, dport, bytes, packets]')
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.session_start_time),
            end_time=str(generator_obj.session_end_time),
            select_fields=['T', 'protocol', 'sport', 'dport', 'bytes', 'packets'],
            where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 3*len(exp_result))
        for exp_r in exp_result:
            found = 0
            for r in res:
                if r['sport'] == exp_r['sport'] and r['protocol'] == exp_r['protocol'] and \
                        r['dport'] == exp_r['dport']:
                    assert(r['bytes'] == exp_r['bytes'])
                    assert(r['packets'] == exp_r['packets'])
                    found = 1
                    break
            assert(found)

        return True
    # end verify_flow_series_aggregation_binning

    @retry(delay=2, tries=5)
    def verify_fieldname_messagetype(self):
        self.logger.info('Verify stats table for stats name field');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        query = Query(table="StatTable.FieldNames.fields",
		            start_time="now-10m",
                            end_time="now",
                            select_fields=["fields.value"],
                            where=[[{"name": "name", "value": "MessageTable:Messagetype",
                                "op": OpServerUtils.MatchOp.EQUAL}]])
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info("res %s" % str(res))
        if not (len(res)>=1):
            return False
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
        vops = VerificationOpsSrvIntrospect('127.0.0.1', opserver.http_port)
        try:
            redis_uve = vops.get_redis_uve_info()['RedisUveInfo']
            if redis_uve['status'] == 'Connected':
                return connected
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return not connected
    # end verify_opserver_redis_uve_connection

    def get_opserver_vns(self):
        self.logger.info('get_opserver_vns')
        vops = VerificationOpsSrv('127.0.0.1', self.opserver.rest_api_port)
        try:
            return vops.get_ops_vns()
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return []
    #end get_opserver_vns

    def get_opserver_vns_response(self):
        self.logger.info('get_opserver_vns_response')
        vops = VerificationOpsSrv('127.0.0.1', self.opserver.rest_api_port)
        return vops.get_ops_vns_response()
    #end get_opserver_vns_response
 
    def get_opserver_alarms(self):
        self.logger.info('get_opserver_alarms')
        vops = VerificationOpsSrv('127.0.0.1', self.opserver.rest_api_port)
        return vops.get_alarms(filters=None)
    #end get_opserver_alarms

    @retry(delay=2, tries=5)
    def set_opserver_db_info(self, opserver,
                             disk_usage_percentage_in = None,
                             pending_compaction_tasks_in = None,
                             disk_usage_percentage_out = None,
                             pending_compaction_tasks_out = None):
        self.logger.info('set_opserver_db_info')
        vops = VerificationOpsSrvIntrospect('127.0.0.1', opserver.http_port)
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
        where_clause = ' AND '.join(where_clause)
        res = vns.post_query(MESSAGE_TABLE, start_time='-3m', end_time='now',
                             select_fields=['MessageTS', 'Type'],
                             where_clause=where_clause, filter='Type=4')
        if not res:
            return False
        self.logger.info("res %s" % str(res))
        return True
    # end verify_tracebuffer_in_analytics_db

    @retry(delay=1, tries=5)
    def verify_table_source_module_list(self, exp_src_list, exp_mod_list):
        self.logger.info('verify source/module list')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        try:
            src_list = vns.get_table_column_values(MESSAGE_TABLE,
                                                   SOURCE)
            self.logger.info('src_list: %s' % str(src_list))
            if len(set(src_list).intersection(exp_src_list)) != \
                    len(exp_src_list):
                return False
            mod_list = vns.get_table_column_values(MESSAGE_TABLE,
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

    @retry(delay=1, tries=5)
    def verify_object_table_sandesh_types(self, table, object_id,
                                          exp_msg_types):
        self.logger.info('verify_object_table_sandesh_types')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        res = vns.post_query(table, start_time='-1m', end_time='now',
                select_fields=['MessageTS','Messagetype', 'ObjectLog', 'SystemLog'],
                where_clause='ObjectId=%s' % object_id)
        if not res:
            return False
        self.logger.info('ObjectTable query response: %s' % str(res))
        actual_msg_types = list(set([r['Messagetype'] for r in res]))
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

        query = Query(table=MESSAGE_TABLE,
                            start_time="now-1h",
                            end_time="now",
                            select_fields=["Xmlmessage","Level"],
                            where=map(lambda x:[{"name": "Keyword", "value": x,
                                "op": 1}], keywords))
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info("res %s" % str(res))
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

    def delete_generator_from_ngenerator(self, redis_uve, generator):
        self.logger.info('delete %s from NGENERATOR' % generator)
        try:
            r = redis.StrictRedis(db=1, port=redis_uve.port, password=redis_uve.password)
            if r.sismember("NGENERATORS", generator):
                r.srem("NGENERATORS", generator)
            else:
              return False
        except Exception as e:
            self.logger.error('Failed to delete generator from redis - %s' % e)
            return False
        else:
           return True

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
        self.logger.info('Remove Timestamps from actual')
        for item in actual_uve_value:
            for tk in item['value']:
                if '__T' in item['value'][tk]:
                    del item['value'][tk]['__T']  
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

    @retry(delay=1, tries=8)
    def verify_uve_timestamp(self, table, typ, expected_t_count):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port,
            self.admin_user, self.admin_password)
        # first step: querry without flat
        table_query = table
        raw_uves = vns.uve_query(table_query, None)
        #second step: querry with flat
        table_query = table + '?flat'
        agg_uves = vns.uve_query(table_query, None)

        if typ not in raw_uves:
            return False
        assert('__T' in raw_uves[typ])
        ts_list = raw_uves[typ]['__T']
        if len(ts_list) != expected_t_count:
            return False
        ts_latest = 0
        for ts in ts_list:
            ts_cur = int(ts[0]['#text'])
            ts_latest = max(ts_latest, ts_cur)
        if typ not in agg_uves:
            return False
        assert('__T' in agg_uves[typ])
        return (ts_latest == agg_uves[typ]['__T'])

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
        return get_free_port()

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
            self.logger.info('%s' % ((35+len(name))*'*'))
            self.logger.info('Shutting down python : %s(obj.stop)' % name)
            instance._main_obj.stop()
            instance._main_obj = None
            gevent.sleep(0)
            self.logger.info('Shutting down python : %s(obj.kill)' % name)
            gevent.kill(instance)
            rcode = 0
            self.logger.info('Shutting down python : %s(done)' % name)
            self.logger.info('%s' % ((35+len(name))*'*'))
        else:
            self.logger.info('%s' % ((35+len(name))*'*'))
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
                    self.logger.info('%s' % ((35+len(name))*'*'))
                    self.logger.info('Log for %s' % (name))
                    self.logger.info('%s' % ((35+len(name))*'*'))
                    self.logger.info(fin.read())
                    self.logger.info('%s' % ((35+len(name))*'*'))
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
            tries = 50
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
                    gevent.sleep(2)
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

    @retry(delay=1, tries=5)
    def verify_analytics_api_info_uve(self, hostname, analytics_table,
            rest_api_ip, host_ip):

        '''
        Read the rest_api_ip and host_ip configured in OpServer.
        Upon starting OpServer these two values are set and the
        the corresponding UVE is written
        '''

        self.logger.info('verify_analytics_api_info_uve: %s:%s:%s:%s' \
                % (hostname, analytics_table, rest_api_ip, host_ip))
        verify_ops = VerificationOpsSrv('127.0.0.1', self.opserver_port,
                self.admin_user, self.admin_password)
        yfilts = {}
        yfilts['cfilt'] = ["AnalyticsApiInfo"]
        filters = self._get_filters_url_param(yfilts)
        table_query = analytics_table + '/' + hostname
        self.logger.info('verify_analytics_api_info_uve: UVE query: %s:%s' \
                % (table_query, str(filters)))
        try:
            res = verify_ops.uve_query(table_query, filters)
        except Exception as err:
            self.logger.error('Failed to get response for %s:%s [%s]' % \
                (table_query, str(filters), str(err)))
            assert(False)
        self.logger.info('RESULT uve_query: %s' % str(res))
        if res == {}:
            return False
        else:
            assert(len(res) > 0)
            analytics_node_ip = dict(res['AnalyticsApiInfo']['analytics_node_ip'])
            self.logger.info('[AnalyticsApiInfo][analytics_node_ip]: %s' % \
                    str(analytics_node_ip))
            assert len(analytics_node_ip) == 2
            assert rest_api_ip == analytics_node_ip['rest_api_ip']
            assert host_ip == analytics_node_ip['host_ip']
            return True
    # end verify_analytics_api_info_uve

