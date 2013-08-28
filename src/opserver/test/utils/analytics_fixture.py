#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import socket
import fixtures
import subprocess
from util import retry
import mockredis
import redis
import urllib2
import copy
import os
from operator import itemgetter
from opserver_introspect_utils import VerificationOpsSrv
from collector_introspect_utils import VerificationCollector

#builddir = sys.path[0] + '/../../../build/debug'
class AnalyticsFixture(fixtures.Fixture):
    def __init__(self, logger, builddir, cassandra_port):
        self.builddir = builddir
        self.cassandra_port = cassandra_port
        self.logger = logger

    def setUp(self):
        super(AnalyticsFixture, self).setUp()

        self.redis_port = AnalyticsFixture.get_free_port()
        mockredis.start_redis(self.redis_port)

        self.redis_query_port = AnalyticsFixture.get_free_port()
        mockredis.start_redis(self.redis_query_port)

        self.http_port = AnalyticsFixture.get_free_port()
        self.listen_port = AnalyticsFixture.get_free_port()
        args = [self.builddir + '/analytics/vizd', \
            '--cassandra-server-list', '127.0.0.1:' + str(self.cassandra_port), \
            '--redis-port', str(self.redis_port), \
            '--listen-port', str(self.listen_port), \
            '--http-server-port', str(self.http_port), \
            '--log-file', '/tmp/vizd.messages.' + str(self.listen_port)]
        self.proc = subprocess.Popen(args,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.logger.info('Setting up Vizd : redis %d redis_query %d  cassandra-server-list %s listen %d' % \
            (self.redis_port, self.redis_query_port, '127.0.0.1:' + str(self.cassandra_port), self.listen_port))

        self.opserver_port = None

        if self.verify_collector_gen():

            openv = copy.deepcopy(os.environ)
            openv['PYTHONPATH'] = self.builddir + '/sandesh/library/python'
            self.opserver_port = AnalyticsFixture.get_free_port()
            args = [ 'python', self.builddir + '/opserver/opserver/opserver.py', \
                '--redis_server_port', str(self.redis_port), \
                '--redis_query_port', str(self.redis_query_port), \
                '--collector_port', str(self.listen_port), \
                '--http_server_port', str(0),
                '--log_file', '/tmp/opserver.messages.' + str(self.listen_port), 
                '--rest_api_port', str(self.opserver_port)] 
            self.opproc = subprocess.Popen(args, env=openv,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)        
            self.logger.info('Setting up OpServer: %d' % self.opserver_port)

            args = [ self.builddir + '/query_engine/qedt', \
                '--redis-port', str(self.redis_query_port), \
                '--collectors', "127.0.0.1:" + str(self.listen_port), \
                '--cassandra-server-list', '127.0.0.1:' + str(self.cassandra_port), \
                '--http-server-port', str(0), \
                '--log-local', '--log-level', 'SYS_DEBUG', \
                '--log-file', '/tmp/qed.messages.' + str(self.listen_port)]
            self.qeproc = subprocess.Popen(args,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)        
            self.logger.info('Setting up qed')

    def get_collector_port(self):
        return self.listen_port
    #end get_collector_port
    
    def get_opserver_port(self):
        return self.opserver_port
    #end get_opserver_port
    
    def verify_on_setup(self):
        result= True
        if self.opserver_port == None:
            result = result and False
            self.logger.error("Collector UVE not in Redis")
        if self.opserver_port == None:
            result = result and False
            self.logger.error("OpServer not started")
        if not self.verify_opserver_api():
            result = result and False
            self.logger.error("OpServer not responding")
        self.verify_is_run = True
        return result

    @retry(delay=2, tries=20)
    def verify_collector_gen(self):
        '''
        See if the SandeshClient within vizd has been able to register
        with the collector within vizd
        '''
        vcl = VerificationCollector('127.0.0.1', self.http_port)
        try:
            genlist = vcl.get_generators()['genlist']
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
            start_time = '-10m', end_time = 'now',
            select_fields = ["ObjectLog"],
            where_clause =  str('ObjectId=' + socket.gethostname()),
            sync = False)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            self.logger.info(str(res))
            return True

    @retry(delay=1, tries=6)
    def verify_message_table_messagetype(self):
        self.logger.info("verify_message_table_messagetype");
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        # query for CollectorInfo logs
        res = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId" ],
            where_clause =  "Messagetype = CollectorInfo")
        if (res == []):
            return False
        assert(len(res) > 0)

        # verify if the result returned is ok
        moduleids = list(set(x['ModuleId'] for x in res))
        self.logger.info("Modules: %s " % str(moduleids))
        # only one moduleid: Collector
        if (not((len(moduleids) == 1))):
            return False
        if (not ("Collector" in moduleids)):
            return False
        return True


    @retry(delay=1, tries=6)
    def verify_message_table_moduleid(self):
        self.logger.info("verify_message_table_moduleid");
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        # query for QueryEngine logs
        res_qe = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["Type", "Messagetype" ],
            where_clause =  "ModuleId = QueryEngine")
        # query for Collector logs
        res_c = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["Type", "Messagetype" ],
            where_clause =  "ModuleId = Collector")
        if (res_qe == []) or (res_c == []):
            return False
        assert(len(res_qe) > 0)
        assert(len(res_c) > 0)
        return True

    @retry(delay=1, tries=6)
    def verify_message_table_where_or(self):
        self.logger.info("verify_message_table_where_or");
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        where_clause1 =  "ModuleId = QueryEngine"
        where_clause2 =  str("Source =" + socket.gethostname())
        res = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId"],
            where_clause = str(where_clause1 + " OR  " + where_clause2))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            if ('Collector' in moduleids) and ('QueryEngine' in moduleids): 
                return True 
            else:
                return False 

    @retry(delay=1, tries=6)
    def verify_message_table_where_and(self):
        self.logger.info("verify_message_table_where_and");
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        where_clause1 =  "ModuleId = QueryEngine"
        where_clause2 =  str("Source =" + socket.gethostname())
        res = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId"],
            where_clause = str(where_clause1 + " AND  " + where_clause2))
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            if len(moduleids) == 1: # 1 moduleid: QueryEngine 
                return True 
            else:
                return False 

    @retry(delay=1, tries=6)
    def verify_message_table_filter(self):
        self.logger.info("verify_message_table_where_filter");
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        where_clause1 =  "ModuleId = QueryEngine"
        where_clause2 =  str("Source =" + socket.gethostname())
        res = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId"],
            where_clause = str(where_clause1 + " OR  " + where_clause2),
            filter = "ModuleId = QueryEngine")
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = list(set(x['ModuleId'] for x in res))
            self.logger.info(str(moduleids))
            if len(moduleids) != 1: # 1 moduleid: Collector
                return False 

        res1 = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId"],
            where_clause = str(where_clause1 + " AND  " + where_clause2),
            filter = "ModuleId = Collector")
        self.logger.info(str(res1))
        if res1 != []:
            return False 
        return True

    @retry(delay=1, tries=1)
    def verify_message_table_sort(self):
        self.logger.info("verify_message_table_sort:Ascending Sort")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        where_clause1 =  "ModuleId = QueryEngine"
        where_clause2 =  str("Source =" + socket.gethostname())
        
        exp_moduleids = ['Collector', 'OpServer', 'QueryEngine']

        # Ascending sort
        res = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId"],
            where_clause = str(where_clause1 + " OR  " + where_clause2),
            sort_fields = ["ModuleId"], sort = 1)
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
        self.logger.info("verify_message_table_sort:Descending Sort");
        res = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId"],
            where_clause = str(where_clause1 + " OR  " + where_clause2),
            sort_fields = ["ModuleId"], sort = 2)
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
            expected_res = sorted(res, key=itemgetter('ModuleId'), reverse=True)
            if res != expected_res:
                return False

        # Limit
        res = vns.post_query('MessageTable',
            start_time = '-10m', end_time = 'now',
            select_fields = ["ModuleId"],
            where_clause = str(where_clause1 + " OR  " + where_clause2),
            sort_fields = ["ModuleId"], sort = 1, limit=1)
        if res == []:
            return False
        else:
            assert(len(res) > 0)
            moduleids = []
            for x in res:
                if x['ModuleId'] not in moduleids:
                    moduleids.append(x['ModuleId'])
            self.logger.info(str(moduleids))
            if len(moduleids) == 1: # 2 moduleids: Collector/QueryEngine
                if moduleids[0] != 'Collector':
                    return False
                return True
            else:
                return False 

    @retry(delay=1, tries=10)
    def verify_flow_samples(self, generator_obj):
        self.logger.info("verify_flow_samples")
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        res = vns.post_query('FlowSeriesTable', 
                             start_time=str(generator_obj.flow_start_time),
                             end_time=str(generator_obj.flow_end_time), 
                             select_fields=['T'], where_clause='')
        self.logger.info(str(res))
        if len(res) == generator_obj.num_flow_samples:
            return True
        return False
    #end verify_flow_samples

    def verify_flow_table(self, generator_obj):
        # query flow records
        self.logger.info('verify_flow_table')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'agg-packets', 'agg-bytes'], 
            where_clause='')
        self.logger.info("FlowRecordTable result:%s" % str(res))
        assert(len(res) == generator_obj.flow_cnt)
        
        # query based on various WHERE parameters 

        # sourcevn and sourceip
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'sourcevn', 'sourceip'], 
            where_clause='sourceip=10.10.10.1 AND sourcevn=domain1:admin:vn1')
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['sourcevn', 'sourceip'], 
            where_clause='sourceip=10.10.10.1 AND sourcevn=domain1:admin:vn1')
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        # give non-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'sourcevn', 'sourceip'], 
            where_clause='sourceip=20.1.1.10')
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['sourcevn', 'sourceip'], 
            where_clause='sourceip=20.1.1.10 AND sourcevn=domain1:admin:vn1')
        self.logger.info(str(res))
        assert(len(res) == 0)
        
        # destvn and destip
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'destvn', 'destip'], 
            where_clause='destip=10.10.10.2 AND destvn=domain1:admin:vn2')
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['destvn', 'destip'], 
            where_clause='destip=10.10.10.2 AND destvn=domain1:admin:vn2')
        self.logger.info(str(res))
        assert(len(res) == generator_obj.num_flow_samples)
        # give non-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'destvn', 'destip'], 
            where_clause='destip=10.10.10.2 AND destvn=default-domain:default-project:default-virtual-network')
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['destvn', 'destip'], 
            where_clause='destip=20.1.1.10 AND destvn=domain1:admin:vn2')
        self.logger.info(str(res))
        assert(len(res) == 0)

        # sport and protocol
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'sport', 'protocol'], 
            where_clause='sport=13 AND protocol=1')
        self.logger.info(str(res))
        assert(len(res) == 1)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['sport', 'protocol'], 
            where_clause = 'sport=13 AND protocol=1')
        self.logger.info(str(res))
        assert(len(res) == 5)
        # give no-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'sport', 'protocol'], 
            where_clause='sport=20 AND protocol=17')
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['sport', 'protocol'], 
            where_clause = 'sport=20 AND protocol=1')
        self.logger.info(str(res))
        assert(len(res) == 0)

        # dport and protocol
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'dport', 'protocol'], 
            where_clause='dport=104 AND protocol=2')
        self.logger.info(str(res))
        assert(len(res) == 1)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['dport', 'protocol'], 
            where_clause = 'dport=104 AND protocol=2')
        self.logger.info(str(res))
        assert(len(res) == 5)
        # give no-existent values in the where clause
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'dport', 'protocol'], 
            where_clause='dport=10 AND protocol=17')
        self.logger.info(str(res))
        assert(len(res) == 0)
        res = vns.post_query('FlowSeriesTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['dport', 'protocol'], 
            where_clause='dport=10 AND protocol=17')
        self.logger.info(str(res))
        assert(len(res) == 0)

        # sort and limit
        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['UuidKey', 'protocol'], where_clause='', 
            sort_fields=['protocol'], sort=1)
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        assert(res[0]['protocol'] == 0)

        res = vns.post_query('FlowRecordTable',
            start_time=str(generator_obj.flow_start_time),
            end_time=str(generator_obj.flow_end_time), 
            select_fields=['protocol'], where_clause='', 
            sort_fields=['protocol'], sort=2, limit=1)
        self.logger.info(str(res))
        assert(len(res) == 1)
        assert(res[0]['protocol'] == 2)
        
        return True
    #end verify_flow_table

    def verify_flow_series_aggregation_binning(self, generator_obj):
        self.logger.info('verify_flow_series_aggregation_binning')
        vns = VerificationOpsSrv('127.0.0.1', self.opserver_port)
        
        # 1. stats
        self.logger.info('Flowseries: [sum(bytes),sum(packets)]')
        res = vns.post_query('FlowSeriesTable', 
                             start_time=str(generator_obj.flow_start_time), 
                             end_time=str(generator_obj.flow_end_time),
                select_fields=['sum(bytes)', 'sum(packets)'], where_clause='') 
        self.logger.info(str(res))
        assert(len(res) == 1)
        exp_sum_pkts = exp_sum_bytes = 0
        for f in generator_obj.flows:
            exp_sum_pkts += f.packets
            exp_sum_bytes += f.bytes
        assert(res[0]['sum(packets)'] == exp_sum_pkts)
        assert(res[0]['sum(bytes)'] == exp_sum_bytes)

        # 2. flow tuple + stats 
        self.logger.info('Flowseries: [sport, dport, sum(bytes), sum(packets)]')
        # Each flow has unique (sport, dport). Therefore, the following query
        # should return # records equal to the # flows.
        res = vns.post_query('FlowSeriesTable', 
                start_time=str(generator_obj.flow_start_time), 
                end_time=str(generator_obj.flow_end_time),
                select_fields=['sport', 'dport', 'sum(bytes)', 'sum(packets)'], 
                where_clause='')
        self.logger.info(str(res))
        assert(len(res) == generator_obj.flow_cnt)
        for r in res:
            cnt = 0
            for f in generator_obj.flows:
                if r['sport'] == f.sport and r['dport'] == f.dport:
                    assert(r['sum(packets)'] == f.packets)
                    assert(r['sum(bytes)'] == f.bytes)
                    break
                cnt += 1
            assert(cnt < generator_obj.flow_cnt)

        # All flows has the same (sourcevn, destvn). Therefore, the following 
        # query should return one record.
        res = vns.post_query('FlowSeriesTable', 
                start_time=str(generator_obj.flow_start_time), 
                end_time=str(generator_obj.flow_end_time),
                select_fields=['sourcevn', 'destvn', 'sum(bytes)', 'sum(packets)'], 
                where_clause='')
        self.logger.info(str(res))
        assert(len(res) == 1)
        exp_sum_pkts = exp_sum_bytes = 0
        for f in generator_obj.flows:
            exp_sum_pkts += f.packets
            exp_sum_bytes += f.bytes
        assert(res[0]['sum(packets)'] == exp_sum_pkts)
        assert(res[0]['sum(bytes)'] == exp_sum_bytes)
        
        # top 3 flows
        res = vns.post_query('FlowSeriesTable', 
                start_time=str(generator_obj.flow_start_time), 
                end_time=str(generator_obj.flow_end_time),
                select_fields=['sport', 'dport', 'sum(bytes)'], 
                where_clause='',
                sort_fields=['sum(bytes)'], sort=2, limit=3)
        self.logger.info(str(res))
        assert(len(res) == 3)
        exp_res = sorted(generator_obj.flows, key=lambda flow: flow.bytes, reverse=True)
        cnt = 0
        for r in res:
            assert(r['sport'] == exp_res[cnt].sport)
            assert(r['dport'] == exp_res[cnt].dport)
            assert(r['sum(bytes)'] == exp_res[cnt].bytes)
            cnt += 1

        # 3. T=<granularity> + stats
        self.logger.info('Flowseries: [T=<x>, sum(bytes), sum(packets)]')
        st = str(generator_obj.flow_start_time)
        et = str(generator_obj.flow_start_time+(30*1000*1000))
        granularity = 10
        res = vns.post_query('FlowSeriesTable', start_time=st, end_time=et,
                select_fields=['T=%s' % (granularity), 'sum(bytes)', 'sum(packets)'], 
                where_clause='sourcevn=domain1:admin:vn1 AND destvn=domain1:admin:vn2')
        self.logger.info(str(res))
        num_records = (int(et)-int(st))/(granularity*1000*1000)
        assert(len(res) == num_records)
        ts = [generator_obj.flow_start_time+((x+1)*granularity*1000*1000) for x in range(num_records)]
        exp_result = {
                       ts[0]:{'sum(bytes)':5500, 'sum(packets)': 65},
                       ts[1]:{'sum(bytes)':725,  'sum(packets)': 15},
                       ts[2]:{'sum(bytes)':700,  'sum(packets)': 8}
                     }
        assert(len(exp_result) == num_records)
        for r in res:
            try:
                stats = exp_result[r['T']]
            except KeyError:
                assert(False)
            assert(r['sum(bytes)'] == stats['sum(bytes)'])
            assert(r['sum(packets)'] == stats['sum(packets)'])

        # 4. T=<granularity> + tuples + stats
        self.logger.info('Flowseries: [T=<x>, protocol, sum(bytes), sum(packets)]')
        st = str(generator_obj.flow_start_time)
        et = str(generator_obj.flow_start_time+(10*1000*1000))
        granularity = 5
        res = vns.post_query('FlowSeriesTable', start_time=st, end_time=et,
                select_fields=['T=%s' % (granularity), 'protocol', 'sum(bytes)', 'sum(packets)'], 
                where_clause='sourcevn=domain1:admin:vn1 AND destvn=domain1:admin:vn2')
        self.logger.info(str(res))
        num_ts = (int(et)-int(st))/(granularity*1000*1000)
        ts = [generator_obj.flow_start_time+((x+1)*granularity*1000*1000) for x in range(num_ts)]
        exp_result = {
                         0:{ts[0]:{'sum(bytes)':450, 'sum(packets)':5}, 
                            ts[1]:{'sum(bytes)':250, 'sum(packets)':3}
                           },
                         1:{ts[0]:{'sum(bytes)':1050, 'sum(packets)':18},
                            ts[1]:{'sum(bytes)':750,  'sum(packets)':14}
                           },
                         2:{ts[0]:{'sum(bytes)':3000, 'sum(packets)':25}
                           }
                     }
        assert(len(res) == 5)
        for r in res:
            try:
                stats = exp_result[r['protocol']][r['T']]
            except KeyError:
                assert(False)
            assert(r['sum(bytes)'] == stats['sum(bytes)'])
            assert(r['sum(packets)'] == stats['sum(packets)'])

        return True
    #end verify_flow_series_aggregation_binning

    def cleanUp(self):
        super(AnalyticsFixture, self).cleanUp()
        rcode = 0
        if self.opserver_port != None:
            self.qeproc.terminate()
            (qe_out,qe_err) = self.qeproc.communicate()
            rcode = self.qeproc.returncode
            self.logger.info('qed returned %d' % rcode)
            self.logger.info('qed terminated stdout: %s' % qe_out)
            self.logger.info('qed terminated stderr: %s' % qe_err)
            subprocess.call(['rm','/tmp/qed.messages.' + str(self.listen_port)])
            self.opproc.terminate()
            subprocess.call(['rm','/tmp/opserver.messages.' + str(self.listen_port)])

        self.proc.terminate()
        subprocess.call(['rm','/tmp/vizd.messages.' + str(self.listen_port)])
        mockredis.stop_redis(self.redis_port)
        mockredis.stop_redis(self.redis_query_port)
        assert(rcode == 0)

    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("",0))
        cport = cs.getsockname()[1]
        cs.close() 
        return cport

