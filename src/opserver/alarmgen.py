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
     INSTANCE_ID_DEFAULT
from alarmgen_cfg import CfgParser
from uveserver import UVEServer
from partition_handler import PartitionHandler, UveStreamProc
from sandesh.alarmgen_ctrl.ttypes import PartitionOwnershipReq, \
    PartitionOwnershipResp, PartitionStatusReq, UVECollInfo, UVEGenInfo, \
    PartitionStatusResp, UVEAlarms, AlarmElement, UVETableAlarmReq, \
    UVETableAlarmResp, UVEAlarmInfo

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
        self._instance_id = '0'
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

        self._us = UVEServer(None, self._logger)
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

        PartitionOwnershipReq.handle_request = self.handle_PartitionOwnershipReq
        PartitionStatusReq.handle_request = self.handle_PartitionStatusReq
        UVETableAlarmReq.handle_request = self.handle_UVETableAlarmReq 

        self._workers = {}

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
                del self.tab_alarms[tab][uv]
                self._logger.info("UVE %s deleted" % uv)
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
        
    def handle_PartitionOwnershipReq(self, req):
        self._logger.info("Got PartitionOwnershipReq: %s" % str(req))
        status = False
        if req.ownership:
            if self._workers.has_key(req.partition):
                self._logger.info("Dup partition %d" % req.partition)
            else:
                uvedb = self._us.get_part(req.partition)
                ph = UveStreamProc(','.join(self._conf.kafka_broker_list()),
                                   req.partition, "uve-" + str(req.partition),
                                   self._logger, uvedb,
                                   self.handle_uve_notif)
                ph.start()
                self._workers[req.partition] = ph
                status = True
        else:
            #import pdb; pdb.set_trace()
            if self._workers.has_key(req.partition):
                ph = self._workers[req.partition]
                gevent.kill(ph)
                res,db = ph.get()
                print "Returned " + str(res)
                print "State :"
                for k,v in db.iteritems():
                    print "%s -> %s" % (k,str(v)) 
                del self._workers[req.partition]
                status = True
            else:
                self._logger.info("No partition %d" % req.partition)

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
                        ugi.uves = list(gen)
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

    def run(self):
        while True:
            gevent.sleep(1)

def setup_controller(argv):
    config = CfgParser(argv)
    config.parse()
    return Controller(config)

def main(args=None):
    controller = setup_controller(args or ' '.join(sys.argv[1:]))
    controller.run()

if __name__ == '__main__':
    main()
