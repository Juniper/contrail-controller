#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import resource
import socket
import fixtures
import subprocess
from util import retry
from mockredis import mockredis
import redis
import urllib2
import copy
import os
import json
from operator import itemgetter
from opserver_introspect_utils import VerificationOpsSrv
from collector_introspect_utils import VerificationCollector
from opserver.sandesh.viz.constants import COLLECTOR_GLOBAL_TABLE, SOURCE, MODULE
from sandesh_common.vns.constants import NodeTypeNames, ModuleNames
from sandesh_common.vns.ttypes import NodeType, Module

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
                 logger, is_dup=False):
        self.analytics_fixture = analytics_fixture
        self.listen_port = AnalyticsFixture.get_free_port()
        self.http_port = AnalyticsFixture.get_free_port()
        self.syslog_port = AnalyticsFixture.get_free_port()
        self.protobuf_port = AnalyticsFixture.get_free_port()
        self.hostname = socket.gethostname()
        self._instance = None
        self._redis_uve = redis_uve
        self._logger = logger
        self._is_dup = is_dup
        if self._is_dup is True:
            self.hostname = self.hostname+'dup'
        self._generator_id = self.hostname+':'+NodeTypeNames[NodeType.ANALYTICS]+\
                            ':'+ModuleNames[Module.COLLECTOR]+':0'
    # end __init__

    def get_addr(self):
        return '127.0.0.1:'+str(self.listen_port)
    # end get_addr

    def get_syslog_port(self):
        return self.syslog_port
    # end get_syslog_port

    def get_protobuf_port(self):
        return self.protobuf_port
    # end get_protobuf_port

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def get_redis_uve(self):
        return self._redis_uve
    # end get_redis_uve

    def start(self):
        assert(self._instance == None)
        self._log_file = '/tmp/vizd.messages.' + str(self.listen_port)
        subprocess.call(['rm', '-rf', self._log_file])
        args = [self.analytics_fixture.builddir + '/analytics/vizd',
            '--DEFAULT.cassandra_server_list', '127.0.0.1:' +
            str(self.analytics_fixture.cassandra_port),
            '--REDIS.port', 
            str(self._redis_uve.port),
            '--COLLECTOR.port', str(self.listen_port),
            '--DEFAULT.http_server_port', str(self.http_port),
            '--DEFAULT.syslog_port', str(self.syslog_port),
            '--DEFAULT.ipfix_port', str(self.analytics_fixture.ipfix_port),
            '--COLLECTOR.protobuf_port', str(self.protobuf_port),
            '--DEFAULT.log_file', self._log_file]
        if self._is_dup is True:
            args.append('--DEFAULT.dup')
        self._instance = subprocess.Popen(args, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE,
                             preexec_fn = AnalyticsFixture.enable_core)
        self._logger.info('Setting up Vizd: %s' % (' '.join(args))) 
    # end start

    def stop(self):
        if self._instance is not None:
            self._logger.info('Shutting down Vizd: 127.0.0.1:%d' 
                              % (self.listen_port))
            self._instance.terminate()
            (vizd_out, vizd_err) = self._instance.communicate()
            vcode = self._instance.returncode
            if vcode != 0:
                self._logger.info('vizd returned %d' % vcode)
                self._logger.info('vizd terminated stdout: %s' % vizd_out)
                self._logger.info('vizd terminated stderr: %s' % vizd_err)
            subprocess.call(['rm', self._log_file])
            assert(vcode == 0)
            self._instance = None
    # end stop

# end class Collector

class OpServer(object):
    def __init__(self, primary_collector, secondary_collector, redis_port, 
                 analytics_fixture, logger, is_dup=False):
        self.primary_collector = primary_collector
        self.secondary_collector = secondary_collector
        self.analytics_fixture = analytics_fixture
        self.listen_port = AnalyticsFixture.get_free_port()
        self.http_port = AnalyticsFixture.get_free_port()
        self.hostname = socket.gethostname()
        self._redis_port = redis_port
        self._instance = None
        self._logger = logger
        self._is_dup = is_dup
        if self._is_dup is True:
            self.hostname = self.hostname+'dup'
        self._generator_id = self.hostname+':'+NodeTypeNames[NodeType.ANALYTICS]+\
                            ':'+ModuleNames[Module.OPSERVER]+':0'
    # end __init__

    def set_primary_collector(self, collector):
        self.primary_collector = collector
    # end set_primary_collector

    def set_secondary_collector(self, collector):
        self.secondary_collector = collector
    # end set_secondary_collector

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def start(self):
        assert(self._instance == None)
        self._log_file = '/tmp/opserver.messages.' + str(self.listen_port)
        subprocess.call(['rm', '-rf', self._log_file])
        args = ['python', self.analytics_fixture.builddir + \
                '/analytics_test/bin/contrail-analytics-api',
                '--redis_server_port', str(self._redis_port),
                '--redis_query_port', 
                str(self.analytics_fixture.redis_uves[0].port),
                '--cassandra_server_list', '127.0.0.1:' +
                str(self.analytics_fixture.cassandra_port),
                '--http_server_port', str(self.http_port),
                '--log_file', self._log_file,
                '--rest_api_port', str(self.listen_port)]
        args.append('--redis_uve_list') 
        for redis_uve in self.analytics_fixture.redis_uves:
            args.append('127.0.0.1:'+str(redis_uve.port))
        args.append('--collectors')
        args.append(self.primary_collector)
        if self.secondary_collector is not None:
            args.append(self.secondary_collector)
        if self._is_dup:
            args.append('--dup')

        self._instance = subprocess.Popen(args,
                                          stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE)
        self._logger.info('Setting up OpServer: %s' % ' '.join(args))
    # end start

    def stop(self):
        if self._instance is not None:
            self._logger.info('Shutting down OpServer 127.0.0.1:%d' 
                              % (self.listen_port))
            self._instance.terminate()
            (op_out, op_err) = self._instance.communicate()
            ocode = self._instance.returncode
            if ocode != 0:
                self._logger.info('OpServer returned %d' % ocode)
                self._logger.info('OpServer terminated stdout: %s' % op_out)
                self._logger.info('OpServer terminated stderr: %s' % op_err)
            subprocess.call(['rm', self._log_file])
            self._instance = None
    # end stop

    def send_tracebuffer_request(self, src, mod, instance, tracebuf):
        vops = VerificationOpsSrv('127.0.0.1', self.listen_port)
        res = vops.send_tracebuffer_req(src, mod, instance, tracebuf)
        self._logger.info('send_tracebuffer_request: %s' % (str(res)))
        assert(res['status'] == 'pass')
    # end send_tracebuffer_request

# end class OpServer

class QueryEngine(object):
    def __init__(self, primary_collector, secondary_collector, 
                 analytics_fixture, logger):
        self.primary_collector = primary_collector
        self.secondary_collector = secondary_collector
        self.analytics_fixture = analytics_fixture
        self.listen_port = AnalyticsFixture.get_free_port()
        self.http_port = AnalyticsFixture.get_free_port()
        self.hostname = socket.gethostname()
        self._instance = None
        self._logger = logger
        self._generator_id = self.hostname+':'+NodeTypeNames[NodeType.ANALYTICS]+\
                            ':'+ModuleNames[Module.QUERY_ENGINE]+':0'
    # end __init__

    def set_primary_collector(self, collector):
        self.primary_collector = collector
    # end set_primary_collector

    def set_secondary_collector(self, collector):
        self.secondary_collector = collector
    # end set_secondary_collector

    def get_generator_id(self):
        return self._generator_id
    # end get_generator_id

    def start(self, analytics_start_time=None):
        assert(self._instance == None)
        self._log_file = '/tmp/qed.messages.' + str(self.listen_port)
        subprocess.call(['rm', '-rf', self._log_file])
        args = [self.analytics_fixture.builddir + '/query_engine/qedt',
                '--REDIS.port', str(self.analytics_fixture.redis_uves[0].port),
                '--DEFAULT.cassandra_server_list', '127.0.0.1:' +
                str(self.analytics_fixture.cassandra_port),
                '--DEFAULT.http_server_port', str(self.listen_port),
                '--DEFAULT.log_local', '--DEFAULT.log_level', 'SYS_DEBUG',
                '--DEFAULT.log_file', self._log_file,
                '--DEFAULT.collectors', self.primary_collector]
        if self.secondary_collector is not None:
            args.append('--DEFAULT.collectors')
            args.append(self.secondary_collector)
        if analytics_start_time is not None:
            args += ['--DEFAULT.start_time', str(analytics_start_time)]
        self._instance = subprocess.Popen(args,
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE,
                             preexec_fn = AnalyticsFixture.enable_core)
        self._logger.info('Setting up contrail-query-engine: %s' % ' '.join(args))
    # end start

    def stop(self):
        if self._instance is not None:
            self._logger.info('Shutting down contrail-query-engine: 127.0.0.1:%d'
                              % (self.listen_port))
            self._instance.terminate()
            (qe_out, qe_err) = self._instance.communicate()
            rcode = self._instance.returncode
            if rcode != 0:
                self._logger.info('contrail-query-engine returned %d' % rcode)
                self._logger.info('contrail-query-engine terminated stdout: %s' % qe_out)
                self._logger.info('contrail-query-engine terminated stderr: %s' % qe_err)
            subprocess.call(['rm', self._log_file])
            assert(rcode == 0)
            self._instance = None
    # end stop

# end class QueryEngine

class Redis(object):
    def __init__(self,builddir):
        self.builddir = builddir
        self.port = AnalyticsFixture.get_free_port()
        self.running = False
    # end __init__

    def start(self):
        assert(self.running == False)
        self.running = True
        mockredis.start_redis(self.port,self.builddir+'/testroot/bin/redis-server') 
    # end start

    def stop(self):
        if self.running:
            mockredis.stop_redis(self.port)
            self.running =  False
    #end stop

# end class Redis

class AnalyticsFixture(fixtures.Fixture):

    def __init__(self, logger, builddir, cassandra_port, ipfix_port = -1,
                 noqed=False, collector_ha_test=False): 
        self.builddir = builddir
        self.cassandra_port = cassandra_port
        self.ipfix_port = ipfix_port
        self.logger = logger
        self.noqed = noqed
        self.collector_ha_test = collector_ha_test

    def setUp(self):
        super(AnalyticsFixture, self).setUp()

        self.redis_uves = [Redis(self.builddir)]
        self.redis_uves[0].start()

        self.collectors = [Collector(self, self.redis_uves[0], self.logger)] 
        self.collectors[0].start()

        self.opserver_port = None
        if self.verify_collector_gen(self.collectors[0]):
            primary_collector = self.collectors[0].get_addr()
            secondary_collector = None
            if self.collector_ha_test:
                self.redis_uves.append(Redis(self.builddir))
                self.redis_uves[1].start()
                self.collectors.append(Collector(self, self.redis_uves[1],
                                                 self.logger, True))
                self.collectors[1].start()
                secondary_collector = self.collectors[1].get_addr()
            self.opserver = OpServer(primary_collector, secondary_collector, 
                                     self.redis_uves[0].port, 
                                     self, self.logger)
            self.opserver.start()
            self.opserver_port = self.opserver.listen_port
            self.query_engine = QueryEngine(primary_collector, 
                                            secondary_collector, 
                                            self, self.logger)
            if not self.noqed:
                self.query_engine.start()
    # end setUp

    def get_collector(self):
        return '127.0.0.1:'+str(self.collectors[0].listen_port)
    # end get_collector

    def get_collectors(self):
        return ['127.0.0.1:'+str(self.collectors[0].listen_port), 
                '127.0.0.1:'+str(self.collectors[1].listen_port)]
    # end get_collectors 

    def get_opserver_port(self):
        return self.opserver.listen_port
    # end get_opserver_port

    def verify_on_setup(self):
        result = True
        if self.opserver_port is None:
            result = result and False
            self.logger.error("Collector UVE not in Redis")
        if self.opserver_port is None:
            result = result and False
            self.logger.error("OpServer not started")
        if not self.verify_opserver_api():
            result = result and False
            self.logger.error("OpServer not responding")
        self.verify_is_run = True
        return result

    @retry(delay=2, tries=20)
    def verify_collector_gen(self, collector):
        '''
        See if the SandeshClient within vizd has been able to register
        with the collector within vizd
        '''
        vcl = VerificationCollector('127.0.0.1', collector.http_port)
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
        try:
            data = urllib2.urlopen(url).read()
        except urllib2.HTTPError, e:
            self.logger.info("HTTP error: %d" % e.code)
        except urllib2.URLError, e:
            self.logger.info("Network error: %s" % e.reason.args[1])

        self.logger.info("Checking OpServer %s" % str(data))
        if data == {}:
            return False
        else:
            return True

    @retry(delay=2, tries=10)
    def verify_collector_obj_count(self):
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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

    @retry(delay=1, tries=10)
    def verify_generator_list(self, collector, exp_genlist):
        vcl = VerificationCollector('127.0.0.1', collector.http_port)
        try:
            genlist = vcl.get_generators()['generators']
            self.logger.info('generator list: ' + str(genlist))
            self.logger.info('exp generator list: ' + str(exp_genlist))
            if len(genlist) != len(exp_genlist):
                return False
            for mod in exp_genlist:
                gen_found = False
                for gen in genlist:
                    if mod == gen['module_id']:
                        gen_found = True
                        if gen['state'] != 'Established':
                            return False
                        break
                if gen_found is not True:
                    return False
        except Exception as err:
            self.logger.error('Exception: %s' % err)
            return False
        return True

    @retry(delay=1, tries=10)
    def verify_generator_uve_list(self, exp_gen_list):
        self.logger.info('verify_generator_uve_list')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        # get generator list
        gen_list = vns.uve_query('generators?cfilt=ModuleClientState:client_info')
        try:
            actual_gen_list = [gen['name'] for gen in gen_list]
            self.logger.info('generators: %s' % str(actual_gen_list))
            for gen in exp_gen_list:
                if gen not in actual_gen_list:
                    return False
        except Exception as e:
            self.logger.error('Exception: %s' % e)
        return True
    # end verify_generator_uve_list

    @retry(delay=1, tries=6)
    def verify_message_table_messagetype(self):
        self.logger.info("verify_message_table_messagetype")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
    def verify_message_table_filter(self):
        self.logger.info("verify_message_table_where_filter")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        where_clause1 = "ModuleId = contrail-query-engine"
        where_clause2 = str("Source =" + socket.gethostname())

        exp_moduleids = ['contrail-analytics-api', 'contrail-collector', 'contrail-query-engine']

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

        # Limit
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
            if len(moduleids) == 1:  # 2 moduleids: contrail-collector/contrail-query-engine
                if moduleids[0] != 'contrail-analytics-api':
                    return False
                return True
            else:
                return False

    @retry(delay=1, tries=8)
    def verify_intervn_all(self, gen_obj):
        self.logger.info("verify_intervn_all")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        vrouter = generator_obj._hostname
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['T'], dir=1, where_clause='vrouter=%s'% vrouter)
        self.logger.info(str(res))
        if len(res) != generator_obj.num_flow_samples:
            return False
        
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
            where_clause='destip=10.10.10.2 AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        res = vns.post_query(
            'FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time),
            select_fields=['destvn', 'destip'],
            where_clause='destip=10.10.10.2 AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
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
            where_clause='destip=20.1.1.10 AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 0)

        # sport and protocol
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'sport', 'protocol'],
                             where_clause='sport=13 AND protocol=1 AND vrouter=%s'% vrouter)
        self.logger.info(str(res))
        assert(len(res) == 1)
        res = vns.post_query('FlowSeriesTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['sport', 'protocol'],
                             where_clause='sport=13 AND protocol=1 AND vrouter=%s'% vrouter)
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

        # Filter by action
        res = vns.post_query('FlowRecordTable',
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time),
                             select_fields=['UuidKey', 'action'],
                             where_clause='vrouter=%s'% vrouter,
                             filter='action=pass')
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        return True
    # end verify_flow_table

    def verify_flow_series_aggregation_binning(self, generator_object):
        generator_obj = generator_object[0]
        vrouter = generator_obj._hostname
        self.logger.info('verify_flow_series_aggregation_binning')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)

        # Helper function for stats aggregation 
        def _aggregate_stats(flow, start_time, end_time):
            stats = {'sum_bytes':0, 'sum_pkts':0}
            for f in flow.samples:
                if f._timestamp < start_time:
                    continue
                elif f._timestamp > end_time:
                    break
                stats['sum_bytes'] += f.flowdata.diff_bytes
                stats['sum_pkts'] += f.flowdata.diff_packets
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
            'AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
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
            'AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
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
            'AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
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
                      assert(r['packets'] == f.flowdata.diff_packets)
                      assert(r['bytes'] == f.flowdata.diff_bytes)
                      found = 1
                      break
            assert(found)

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
                flow.append({'packets':s.flowdata.diff_packets,
                            'bytes':s.flowdata.diff_bytes})
        sorted_flow = sorted(flow, key=itemgetter('packets', 'bytes'))
        assert(sorted_res == sorted_flow)

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
            'AND destvn=domain1:admin:vn2 AND vrouter=%s'% vrouter)
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
                        assert(r['bytes'] == sample.flowdata.diff_bytes)
                        assert(r['packets'] == sample.flowdata.diff_packets)
                        found = 1
                        break
                assert(found)

        # 15 vrouter
        self.logger.info("Flowseries: [sourcevn, destvn, vrouter]")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
                dict = {'vrouter':f._source, 'destvn':f.flowdata.destvn, 'sourcevn':f.flowdata.sourcevn}
                exp_result.append(dict)
        for flow in generator_obj.flows:
            for f in flow.samples:
                dict = {'vrouter':f._source, 'destvn':f.flowdata.destvn, 'sourcevn':f.flowdata.sourcevn}
                exp_result.append(dict)
        sorted_exp_result = sorted(exp_result, key=itemgetter('vrouter'))
        assert(sorted_res == sorted_exp_result)
        return True
    # end verify_flow_series_aggregation_binning

    def verify_fieldname_messagetype(self):
        self.logger.info('Verify stats table for stats name field');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port);
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

    def verify_fieldname_objecttype(self):
        self.logger.info('Verify stats table for stats name field');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port);
        query = Query(table="ObjectCollectorInfo",
                            start_time="now-600s",
                            end_time="now",
                            select_fields=["ObjectId"]);
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info(str(res))
        assert(len(res) > 0)
        return True

    @retry(delay=2, tries=5)
    def verify_collector_redis_uve_connection(self, collector, connected=True):
        self.logger.info('verify_collector_redis_uve_connection')
        vcl = VerificationCollector('127.0.0.1', collector.http_port)
        try:
            redis_uve = vcl.get_redis_uve_info()['RedisUveInfo']
            if redis_uve['status'] == 'Connected':
                return connected
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return not connected
    # end verify_collector_redis_uve_connection 

    @retry(delay=2, tries=5)
    def verify_opserver_redis_uve_connection(self, opserver, connected=True):
        self.logger.info('verify_opserver_redis_uve_connection')
        vops = VerificationOpsSrv('127.0.0.1', opserver.http_port)
        try:
            redis_uve = vops.get_redis_uve_info()['RedisUveInfo']
            if redis_uve['status'] == 'Connected':
                return connected
        except Exception as err:
            self.logger.error('Exception: %s' % err)
        return not connected
    # end verify_opserver_redis_uve_connection

    @retry(delay=2, tries=5)
    def verify_tracebuffer_in_analytics_db(self, src, mod, tracebuf):
        self.logger.info('verify trace buffer data in analytics db')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port);
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port);
        query = Query(table='ObjectCollectorInfo',
                             start_time=start_time, end_time=end_time,
                             select_fields=['ObjectLog'])
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        self.logger.info("collector object log: %s" % res)
        return res
    # end verify_collector_object_log

    @retry(delay=1, tries=5)
    def verify_collector_object_log_before_purge(self, start_time, end_time):
        self.logger.info('verify_collector_object_log_before_purge')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port);
        res = self.verify_collector_object_log(start_time, end_time)
        self.logger.info("collector object log before purging: %s" % res)
        if not res:
            return False
        return True
        # end verify_collector_object_log_before_purge

    def verify_database_purge_query(self, start_time, end_time):
        self.logger.info('verify database purge query');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port);
        json_qstr = json.dumps({'purge_input': 100})
        res = vns.post_purge_query_json(json_qstr)
        assert(res == 'started')
        return True
    # end verify_database_purge_query

    @retry(delay=2, tries=20)
    def verify_collector_object_log_after_purge(self, start_time, end_time):
        self.logger.info('verify_collector_object_log_after_purge')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        res = self.verify_collector_object_log(start_time, end_time)
        self.logger.info("collector object log after purging: %s" % res)
        if res != []:
            return False
        return True
    # end verify_collector_object_log_after_purge

    @retry(delay=1, tries=5)
    def verify_object_table_query(self):
        self.logger.info('verify_object_table_query')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)

        #ObjectTable query with only ObjectId
        self.logger.info('ObjectTable query with only ObjectId')
        object_id = object_id = self.collectors[0].hostname
        res = vns.post_query('ObjectCollectorInfo',
                             start_time='-10m', end_time='now',
                             select_fields=['ObjectId'],
                             where_clause='ObjectId = %s' % object_id)
        if not res:
            return False
        else:
            self.logger.info(res)
            for r in res:
                assert('ObjectId' in r)

        # ObjectTable query with ModuleId specified in where clause
        self.logger.info('ObjectTable query with ModuleId in where clause')
        object_id = object_id = self.collectors[0].hostname 
        module = 'contrail-collector'
        where_obj_id = 'ObjectId = %s' % object_id
        where_mod = 'ModuleId = %s' % module
        res = vns.post_query('ObjectCollectorInfo',
                             start_time='-10m', end_time='now',
                             select_fields=['ObjectId'],
                             where_clause=where_obj_id + 'AND' + where_mod)
        if not res:
            return False
        else:
            self.logger.info(res)
            for r in res:
                assert('ObjectId' in r)

        return True
    # end verify_object_table_query

    @retry(delay=1, tries=5)
    def verify_keyword_query(self, line, keywords=[]):
        self.logger.info('Verify where query with keywords');
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port);

        query = Query(table="MessageTable",
                            start_time="now-1h",
                            end_time="now",
                            select_fields=["Xmlmessage","Level"],
                            where=map(lambda x:[{"name": "Keyword", "value": x,
                                "op": 1}], keywords))
        json_qstr = json.dumps(query.__dict__)
        res = vns.post_query_json(json_qstr)
        assert(len(res)>0)
        self.logger.info(str(res))
        return len(res)>0
    # end verify_keyword_query

    @retry(delay=1, tries=5)
    def verify_generator_list_in_redis(self, redis_uve, exp_gen_list):
        self.logger.info('Verify generator list in redis')
        try:
            r = redis.StrictRedis(db=1, port=redis_uve.port)
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
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
	self.logger.info("VerificationOpsSrv")
        res = vns.post_query('StatTable.FieldNames.fields',
                             start_time='-1m',
                             end_time='now',
                             select_fields=['fields.value'],
                             where_clause = 'name=ObjectVNTable:Objecttype')
        self.logger.info(str(res))
	#Verify that 2 different n/w are present vn0 and vn1
	assert(len(res)==2)
        return True
    # end verify_fieldname_table

    def cleanUp(self):

        try:
            self.opserver.stop()
        except:
            pass
        self.query_engine.stop()
        for collector in self.collectors:
            collector.stop()
        for redis_uve in self.redis_uves:
            redis_uve.stop()
        super(AnalyticsFixture, self).cleanUp()

    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("", 0))
        cport = cs.getsockname()[1]
        cs.close()
        return cport

    @staticmethod
    def enable_core():
        try:
	    resource.setrlimit(resource.RLIMIT_CORE, (-1, -1))
        except:
            pass
