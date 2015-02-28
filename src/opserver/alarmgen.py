#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver
#
# Operational State Server for VNC
#

from gevent import monkey
monkey.patch_all()
import sys
import json
import socket
import pprint
try:
    from collections import OrderedDict
except ImportError:
    # python 2.6 or earlier, use backport
    from ordereddict import OrderedDict
from pysandesh.sandesh_base import *
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType,\
    ConnectionStatus
from sandesh.analytics.ttypes import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT, COLLECTOR_DISCOVERY_SERVICE_NAME,\
     ALARM_GENERATOR_SERVICE_NAME
from alarmgen_cfg import CfgParser
from uveserver import UVEServer
from partition_handler import PartitionHandler, UveStreamProc
from sandesh.alarmgen_ctrl.ttypes import PartitionOwnershipReq, \
    PartitionOwnershipResp, PartitionStatusReq, UVECollInfo, UVEGenInfo, \
    PartitionStatusResp, UVEAlarms, AlarmElement, UVETableAlarmReq, \
    UVETableAlarmResp, UVEAlarmInfo, AlarmgenTrace, AlarmTrace, UVEKeyInfo
from sandesh.discovery.ttypes import CollectorTrace

from opserver_util import ServicePoller
from stevedore import hook

class Controller(object):
    
    @staticmethod
    def fail_cb(manager, entrypoint, exception):
        sandesh_global._logger.info("Load failed for %s with exception %s" % \
                                     (str(entrypoint),str(exception)))
        
    def __init__(self, conf):
        self._conf = conf
        module = Module.ALARM_GENERATOR
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        self._hostname = socket.gethostname()
        self._instance_id = self._conf.worker_id()
        sandesh_global.init_generator(self._moduleid, self._hostname,
                                      self._node_type_name, self._instance_id,
                                      self._conf.collectors(), 
                                      self._node_type_name,
                                      self._conf.http_port(),
                                      ['opserver.sandesh', 'sandesh'])
        sandesh_global.set_logging_params(
            enable_local_log=self._conf.log_local(),
            category=self._conf.log_category(),
            level=self._conf.log_level(),
            file=self._conf.log_file(),
            enable_syslog=self._conf.use_syslog(),
            syslog_facility=self._conf.syslog_facility())
        self._logger = sandesh_global._logger

        # Trace buffer list
        self.trace_buf = [
            {'name':'DiscoveryMsg', 'size':1000}
        ]
        # Create trace buffers 
        for buf in self.trace_buf:
            sandesh_global.trace_buffer_create(name=buf['name'], size=buf['size'])

        tables = [ "ObjectCollectorInfo",
                   "ObjectDatabaseInfo",
                   "ObjectVRouter",
                   "ObjectBgpRouter",
                   "ObjectConfigNode" ] 
        self.mgrs = {}
        self.tab_alarms = {}
        for table in tables:
            self.mgrs[table] = hook.HookManager(
                namespace='contrail.analytics.alarms',
                name=table,
                invoke_on_load=True,
                invoke_args=(),
                on_load_failure_callback=Controller.fail_cb
            )
            
            for extn in self.mgrs[table][table]:
                self._logger.info('Loaded extensions for %s: %s,%s' % \
                    (table, extn.name, extn.entry_point_target))

            self.tab_alarms[table] = {}

        ConnectionState.init(sandesh_global, self._hostname, self._moduleid,
            self._instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus)

        self._us = UVEServer(None, self._logger, self._conf.redis_password())

        self._workers = {}

        self.disc = None
        self._libpart_name = self._hostname + ":" + self._instance_id
        self._libpart = None
        self._partset = set()
        if self._conf.discovery()['server']:
            import discoveryclient.client as client 
            data = {
                'ip-address': self._hostname ,
                'port': self._instance_id
            }
            self.disc = client.DiscoveryClient(
                self._conf.discovery()['server'],
                self._conf.discovery()['port'],
                ModuleNames[Module.ALARM_GENERATOR])
            self._logger.info("Disc Publish to %s : %s"
                          % (str(self._conf.discovery()), str(data)))
            self.disc.publish(ALARM_GENERATOR_SERVICE_NAME, data)
        else:
            # If there is no discovery service, use fixed redis_uve list
            redis_uve_list = []
            try:
                for redis_uve in self._conf.redis_uve_list():
                    redis_ip_port = redis_uve.split(':')
                    redis_ip_port = (redis_ip_port[0], int(redis_ip_port[1]))
                    redis_uve_list.append(redis_ip_port)
            except Exception as e:
                self._logger.error('Failed to parse redis_uve_list: %s' % e)
            else:
                self._us.update_redis_uve_list(redis_uve_list)

            # If there is no discovery service, use fixed alarmgen list
            self._libpart = self.start_libpart(self._conf.alarmgen_list())

        PartitionOwnershipReq.handle_request = self.handle_PartitionOwnershipReq
        PartitionStatusReq.handle_request = self.handle_PartitionStatusReq
        UVETableAlarmReq.handle_request = self.handle_UVETableAlarmReq 


    def libpart_cb(self, part_list):

        newset = set(part_list)
        oldset = self._partset
        self._partset = newset

        self._logger.info('Partition List : new %s old %s' % \
            (str(newset),str(oldset)))
        
        for addpart in (newset-oldset):
            self._logger.info('Partition Add : %s' % addpart)
            self.partition_change(addpart, True)
        
        for delpart in (oldset-newset):
            self._logger.info('Partition Del : %s' % delpart)
            self.partition_change(delpart, True)

    def start_libpart(self, ag_list):
        if not self._conf.zk_list():
            self._logger.error('Could not import libpartition: No zookeeper')
            return None
        if not ag_list:
            self._logger.error('Could not import libpartition: No alarmgen list')
            return None
        try:
            from libpartition.libpartition import PartitionClient
            self._logger.error('Starting PC')

            pc = PartitionClient("alarmgen",
                    self._libpart_name, ag_list,
                    self._conf.partitions(), self.libpart_cb,
                    ','.join(self._conf.zk_list()))
            self._logger.error('Started PC')
            return pc
        except Exception as e:
            self._logger.error('Could not import libpartition: %s' % str(e))
            return None

    def handle_uve_notif(self, uves):
        self._logger.debug("Changed UVEs : %s" % str(uves))
        no_handlers = set()
        for uv in uves:
            tab = uv.split(':',1)[0]
            if not self.mgrs.has_key(tab):
                no_handlers.add(tab)
                continue
            itr = self._us.multi_uve_get(uv, True, None, None, None, None)
            uve_data = itr.next()['value']
            if len(uve_data) == 0:
                self._logger.info("UVE %s deleted" % uv)
                if self.tab_alarms[tab].has_key(uv):
		    del self.tab_alarms[tab][uv]
                    ustruct = UVEAlarms(name = uv, deleted = True)
                    alarm_msg = AlarmTrace(data=ustruct, table=tab)
                    self._logger.info('send del alarm: %s' % (alarm_msg.log()))
                    alarm_msg.send()
                continue
            results = self.mgrs[tab].map_method("__call__", uv, uve_data)
            new_uve_alarms = {}
            for res in results:
                nm, errs = res
                self._logger.info("Alarm[%s] %s: %s" % (tab, nm, str(errs)))
                elems = []
                for ae in errs:
                    rule, val = ae
                    rv = AlarmElement(rule, val)
                    elems.append(rv)
                if len(elems):
                    new_uve_alarms[nm] = UVEAlarmInfo(type = nm,
                                           description = elems, ack = False)
            if (not self.tab_alarms[tab].has_key(uv)) or \
                       pprint.pformat(self.tab_alarms[tab][uv]) != \
                       pprint.pformat(new_uve_alarms):
                ustruct = UVEAlarms(name = uv, alarms = new_uve_alarms.values(),
                                    deleted = False)
                alarm_msg = AlarmTrace(data=ustruct, table=tab)
                self._logger.info('send alarm: %s' % (alarm_msg.log()))
                alarm_msg.send()
            self.tab_alarms[tab][uv] = new_uve_alarms
            
        if len(no_handlers):
            self._logger.info('No Alarm Handlers for %s' % str(no_handlers))

    def handle_UVETableAlarmReq(self, req):
        status = False
        if req.table == "all":
            parts = self.tab_alarms.keys()
        else:
            parts = [req.table]
        self._logger.info("Got UVETableAlarmReq : %s" % str(parts))
        np = 1
        for pt in parts:
            resp = UVETableAlarmResp(table = pt)
            uves = []
            for uk,uv in self.tab_alarms[pt].iteritems():
                alms = []
                for ak,av in uv.iteritems():
                    alms.append(av)
                uves.append(UVEAlarms(name = uk, alarms = alms))
            resp.uves = uves 
            if np == len(parts):
                mr = False
            else:
                mr = True
            resp.response(req.context(), mr)
            np = np + 1

    
    def partition_change(self, partno, enl):
        """
        Call this function when getting or giving up
        ownership of a partition
        Args:
            partno : Partition Number
            enl    : True for acquiring, False for giving up
        Returns: 
            status of operation (True for success)
        """
        status = False
        if enl:
            if self._workers.has_key(partno):
                self._logger.info("Dup partition %d" % partno)
            else:
                uvedb = self._us.get_part(partno)
                ph = UveStreamProc(','.join(self._conf.kafka_broker_list()),
                                   partno, "uve-" + str(partno),
                                   self._logger, uvedb,
                                   self.handle_uve_notif)
                ph.start()
                self._workers[partno] = ph
                status = True
        else:
            if self._workers.has_key(partno):
                ph = self._workers[partno]
                gevent.kill(ph)
                res,db = ph.get()
                print "Returned " + str(res)
                print "State :"
                for k,v in db.iteritems():
                    print "%s -> %s" % (k,str(v)) 
                del self._workers[partno]
                status = True
            else:
                self._logger.info("No partition %d" % partno)

        return status
    
    def handle_PartitionOwnershipReq(self, req):
        self._logger.info("Got PartitionOwnershipReq: %s" % str(req))
        status = self.partition_change(req.partition, req.ownership)

        resp = PartitionOwnershipResp()
        resp.status = status
	resp.response(req.context())
                
    def handle_PartitionStatusReq(self, req):
        
        if req.partition == -1:
            parts = self._workers.keys()
        else:
            parts = [req.partition]
        
        self._logger.info("Got PartitionStatusReq: %s" % str(parts))
        np = 1
        for pt in parts:
            resp = PartitionStatusResp()
            resp.partition = pt
            if self._workers.has_key(pt):
                resp.enabled = True
                resp.uves = []
                for kcoll,coll in self._workers[pt].contents().iteritems():
                    uci = UVECollInfo()
                    uci.collector = kcoll
                    uci.uves = []
                    for kgen,gen in coll.iteritems():
                        ugi = UVEGenInfo()
                        ugi.generator = kgen
                        ugi.uves = []
                        for uk,uc in gen.iteritems():
                            ukc = UVEKeyInfo()
                            ukc.key = uk
                            ukc.count = uc
                            ugi.uves.append(ukc)
                        uci.uves.append(ugi)
                    resp.uves.append(uci)
            else:
                resp.enabled = False
            if np == len(parts):
                mr = False
            else:
                mr = True
            resp.response(req.context(), mr)
            np = np + 1

    def disc_cb_coll(self, clist):
        '''
        Analytics node may be brought up/down any time. For UVE aggregation,
        alarmgen needs to know the list of all Analytics nodes (redis-uves).
        Periodically poll the Collector list [in lieu of 
        redi-uve nodes] from the discovery. 
        '''
        newlist = []
        for elem in clist:
            (ipaddr,port) = elem
            newlist.append((ipaddr, self._conf.redis_server_port()))
        self._us.update_redis_uve_list(newlist)

    def disc_cb_ag(self, alist):
        '''
        Analytics node may be brought up/down any time. For partitioning,
        alarmgen needs to know the list of all Analytics nodes (alarmgens).
        Periodically poll the alarmgen list from the discovery service
        '''
        newlist = []
        for elem in alist:
            (ipaddr, inst) = elem
            newlist.append(ipaddr + ":" + inst)

        # We should always include ourselves in the list of memebers
        newset = set(newlist)
        newset.add(self._libpart_name)
        newlist = list(newset)
        if not self._libpart:
            self._libpart = self.start_libpart(newlist)
        else:
            self._libpart.update_cluster_list(newlist)

    def run(self):
        while True:
            gevent.sleep(60)

def setup_controller(argv):
    config = CfgParser(argv)
    config.parse()
    return Controller(config)

def main(args=None):
    controller = setup_controller(args or ' '.join(sys.argv[1:]))
    gevs = [ 
        gevent.spawn(controller.run)]

    if controller.disc:
        sp1 = ServicePoller(controller._logger, CollectorTrace, controller.disc, \
                           COLLECTOR_DISCOVERY_SERVICE_NAME, controller.disc_cb_coll)
        sp1.start()
        gevs.append(sp1)

        sp2 = ServicePoller(controller._logger, AlarmgenTrace, controller.disc, \
                           ALARM_GENERATOR_SERVICE_NAME, controller.disc_cb_ag)
        sp2.start()
        gevs.append(sp2)

    gevent.joinall(gevs)

if __name__ == '__main__':
    main()
