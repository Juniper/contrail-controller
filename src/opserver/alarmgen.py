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
import time
import copy
import traceback
import signal
try:
    from collections import OrderedDict
except ImportError:
    # python 2.6 or earlier, use backport
    from ordereddict import OrderedDict
from pysandesh.sandesh_base import *
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType,\
    ConnectionStatus
from pysandesh.gen_py.sandesh_alarm.ttypes import SandeshAlarmAckResponseCode
from sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import AlarmTrace, \
    UVEAlarms, UVEAlarmInfo, AlarmTemplate, AllOf
from sandesh.analytics.ttypes import *
from sandesh.analytics.cpuinfo.ttypes import ProcessCpuInfo
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
    PartitionStatusResp, UVETableAlarmReq, UVETableAlarmResp, \
    AlarmgenTrace, UVEKeyInfo, UVETypeCount, UVETypeInfo, AlarmgenStatusTrace, \
    AlarmgenStatus, AlarmgenStats, AlarmgenPartitionTrace, \
    AlarmgenPartition, AlarmgenPartionInfo, AlarmgenUpdate, \
    UVETableInfoReq, UVETableInfoResp, UVEObjectInfo, UVEStructInfo, \
    UVETablePerfReq, UVETablePerfResp, UVETableInfo, UVEKeyCount

from sandesh.discovery.ttypes import CollectorTrace
from cpuinfo import CpuInfoData
from opserver_util import ServicePoller
from stevedore import hook, extension
from pysandesh.util import UTCTimestampUsec
from libpartition.libpartition import PartitionClient
import discoveryclient.client as client 
from kafka import KafkaClient, SimpleProducer
import redis
from collections import namedtuple

OutputRow = namedtuple("OutputRow",["key","typ","val"])

class AGTabStats(object):
    """ This class is used to store per-UVE-table information
        about the time taken and number of instances when
        a UVE was retrieved, published or evaluated for alarms
    """
    def __init__(self):
        self.reset()

    def record_get(self, get_time):
        self.get_time += get_time
        self.get_n += 1

    def record_pub(self, get_time):
        self.pub_time += get_time
        self.pub_n += 1

    def record_call(self, get_time):
        self.call_time += get_time
        self.call_n += 1
    
    def get_result(self):
        if self.get_n:
            return self.get_time / self.get_n
        else:
            return 0

    def pub_result(self):
        if self.pub_n:
            return self.pub_time / self.pub_n
        else:
            return 0

    def call_result(self):
        if self.call_n:
            return self.call_time / self.call_n
        else:
            return 0

    def reset(self):
        self.call_time = 0
        self.call_n = 0
        self.get_time = 0
        self.get_n = 0
        self.pub_time = 0
        self.pub_n = 0
        
    
class AGKeyInfo(object):
    """ This class is used to maintain UVE contents
    """

    def __init__(self, part):
        self._part = part
        # key of struct name, value of content dict

        self.current_dict = {}
        self.update({})
        
    def update_single(self, typ, val):
        # A single UVE struct has changed
        # If the UVE has gone away, the val is passed in as None

        self.set_removed = set()
        self.set_added = set()
        self.set_changed = set()
        self.set_unchanged = self.current_dict.keys()

        if typ in self.current_dict:
            # the "added" set must stay empty in this case
            if val is None:
                self.set_unchanged.remove(typ)
                self.set_removed.add(typ)
                del self.current_dict[typ]
            else:
                # both "added" and "removed" will be empty
                if val != self.current_dict[typ]:
                    self.set_unchanged.remove(typ)
                    self.set_changed.add(typ)
                    self.current_dict[typ] = val
        else:
            if val != None:
                self.set_added.add(typ)
                self.current_dict[typ] = val

    def update(self, new_dict):
        # A UVE has changed, and we have the entire new 
        # content of the UVE available in new_dict
        set_current = set(new_dict.keys())
        set_past = set(self.current_dict.keys())
        set_intersect = set_current.intersection(set_past)

        self.set_added = set_current - set_intersect
        self.set_removed = set_past - set_intersect
        self.set_changed = set()
        self.set_unchanged = set()
        for o in set_intersect:
            if new_dict[o] != self.current_dict[o]:
                self.set_changed.add(o)
            else:
                self.set_unchanged.add(o)
        self.current_dict = new_dict

    def values(self):
        return self.current_dict

    def added(self):
        return self.set_added

    def removed(self):
        return self.set_removed

    def changed(self):
        return self.set_changed

    def unchanged(self):
        return self.set_unchanged

class AlarmProcessor(object):
    def __init__(self, logger):
        self.uve_alarms = {}
        self._logger = logger

    def process_alarms(self, ext, uv, local_uve):
        nm = ext.entry_point_target.split(":")[1]
        sev = ext.obj.severity()

        try:
            or_list = ext.obj.__call__(uv, local_uve)
            self._logger.debug("Alarm[%s] %s: %s" % (uv, nm, str(or_list)))
            if or_list:
                self.uve_alarms[nm] = UVEAlarmInfo(type = nm, severity = sev,
                                       timestamp = 0, token = "",
                                       any_of = or_list, ack = False)
	except Exception as ex:
	    template = "Exception {0} in Alarm Processing. Arguments:\n{1!r}"
	    messag = template.format(type(ex).__name__, ex.args)
	    self._logger.error("%s : traceback %s" % \
			      (messag, traceback.format_exc()))
            self.uve_alarms[nm] = UVEAlarmInfo(type = nm, severity = sev,
                                   timestamp = 0, token = "",
                                   any_of = [AllOf(all_of=[])], ack = False)

class Controller(object):

    @staticmethod
    def token(sandesh, timestamp):
        token = {'host_ip': sandesh.host_ip(),
                 'http_port': sandesh._http_server.get_port(),
                 'timestamp': timestamp}
        return base64.b64encode(json.dumps(token))

    def fail_cb(self, manager, entrypoint, exception):
        self._sandesh._logger.info("Load failed for %s with exception %s" % \
                                     (str(entrypoint),str(exception)))
        
    def __init__(self, conf, test_logger=None):
        self._conf = conf
        module = Module.ALARM_GENERATOR
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        self._hostname = socket.gethostname()
        self._instance_id = self._conf.worker_id()

        self.disc = None
        self._libpart_name = self._conf.host_ip() + ":" + self._instance_id
        self._libpart = None
        self._partset = set()
        if self._conf.discovery()['server']:
            self._max_out_rows = 20
            self.disc = client.DiscoveryClient(
                self._conf.discovery()['server'],
                self._conf.discovery()['port'],
                ModuleNames[Module.ALARM_GENERATOR])

        is_collector = True
        if test_logger is not None:
            is_collector = False
        self._sandesh = Sandesh()
        # Reset the sandesh send rate limit value
        if self._conf.sandesh_send_rate_limit() is not None:
            SandeshSystem.set_sandesh_send_rate_limit( \
                self._conf.sandesh_send_rate_limit())
        self._sandesh.init_generator(self._moduleid, self._hostname,
                                      self._node_type_name, self._instance_id,
                                      self._conf.collectors(),
                                      self._node_type_name,
                                      self._conf.http_port(),
                                      ['opserver.sandesh', 'sandesh'],
                                      host_ip=self._conf.host_ip(),
                                      discovery_client=self.disc,
                                      connect_to_collector = is_collector,
                                      alarm_ack_callback=self.alarm_ack_callback)
        if test_logger is not None:
            self._logger = test_logger
        else:
            self._sandesh.set_logging_params(
                enable_local_log=self._conf.log_local(),
                category=self._conf.log_category(),
                level=self._conf.log_level(),
                file=self._conf.log_file(),
                enable_syslog=self._conf.use_syslog(),
                syslog_facility=self._conf.syslog_facility())
            self._logger = self._sandesh._logger
        # Trace buffer list
        self.trace_buf = [
            {'name':'DiscoveryMsg', 'size':1000}
        ]
        # Create trace buffers 
        for buf in self.trace_buf:
            self._sandesh.trace_buffer_create(name=buf['name'], size=buf['size'])

        tables = set()
        mgrlist = extension.ExtensionManager('contrail.analytics.alarms')
        for elem in mgrlist:
            tables.add(elem.name)
        self._logger.error('Found extenstions for %s' % str(tables))

        self.mgrs = {}
        self.tab_alarms = {}
        self.ptab_info = {}
        self.tab_perf = {}
        self.tab_perf_prev = {}
        for table in tables:
            self.mgrs[table] = hook.HookManager(
                namespace='contrail.analytics.alarms',
                name=table,
                invoke_on_load=True,
                invoke_args=(),
                on_load_failure_callback=self.fail_cb
            )
            
            for extn in self.mgrs[table][table]:
                self._logger.info('Loaded extensions for %s: %s,%s doc %s' % \
                    (table, extn.name, extn.entry_point_target, extn.obj.__doc__))

            self.tab_alarms[table] = {}
            self.tab_perf[table] = AGTabStats()

        ConnectionState.init(self._sandesh, self._hostname, self._moduleid,
            self._instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus)

        self._us = UVEServer(None, self._logger, self._conf.redis_password())

        if not self.disc:
            self._max_out_rows = 2
            # If there is no discovery service, use fixed redis_uve list
            redis_uve_list = []
            try:
                for redis_uve in self._conf.redis_uve_list():
                    redis_ip_port = redis_uve.split(':')
                    redis_elem = (redis_ip_port[0], int(redis_ip_port[1]),0)
                    redis_uve_list.append(redis_elem)
            except Exception as e:
                print('Failed to parse redis_uve_list: %s' % e)
            else:
                self._us.update_redis_uve_list(redis_uve_list)

            # If there is no discovery service, use fixed alarmgen list
            self._libpart = self.start_libpart(self._conf.alarmgen_list())

        self._workers = {}
        self._uvestats = {}
        self._uveq = {}
        self._uveqf = {}

        PartitionOwnershipReq.handle_request = self.handle_PartitionOwnershipReq
        PartitionStatusReq.handle_request = self.handle_PartitionStatusReq
        UVETableAlarmReq.handle_request = self.handle_UVETableAlarmReq 
        UVETableInfoReq.handle_request = self.handle_UVETableInfoReq
        UVETablePerfReq.handle_request = self.handle_UVETablePerfReq

    def libpart_cb(self, part_list):

        agpi = AlarmgenPartionInfo()
        agpi.instance = self._instance_id
        agpi.partitions = part_list

        agp = AlarmgenPartition()
        agp.name = self._hostname
        agp.inst_parts = [agpi]
       
        agp_trace = AlarmgenPartitionTrace(data=agp, sandesh=self._sandesh)
        agp_trace.send(sandesh=self._sandesh) 

        newset = set(part_list)
        oldset = self._partset
        self._partset = newset

        self._logger.error('Partition List : new %s old %s' % \
            (str(newset),str(oldset)))
        
        for addpart in (newset-oldset):
            self._logger.error('Partition Add : %s' % addpart)
            self.partition_change(addpart, True)
        
        for delpart in (oldset-newset):
            self._logger.error('Partition Del : %s' % delpart)
            self.partition_change(delpart, False)

        self._logger.error('Partition List done : new %s old %s' % \
            (str(newset),str(oldset)))

    def start_libpart(self, ag_list):
        if not self._conf.zk_list():
            self._logger.error('Could not import libpartition: No zookeeper')
            return None
        if not ag_list:
            self._logger.error('Could not import libpartition: No alarmgen list')
            return None
        try:
            self._logger.error('Starting PC')
            agpi = AlarmgenPartionInfo()
            agpi.instance = self._instance_id
            agpi.partitions = []

            agp = AlarmgenPartition()
            agp.name = self._hostname
            agp.inst_parts = [agpi]
           
            agp_trace = AlarmgenPartitionTrace(data=agp, sandesh=self._sandesh)
            agp_trace.send(sandesh=self._sandesh) 

            pc = PartitionClient(self._conf.kafka_prefix() + "-alarmgen",
                    self._libpart_name, ag_list,
                    self._conf.partitions(), self.libpart_cb,
                    ','.join(self._conf.zk_list()), self._logger)
            self._logger.error('Started PC %s' % self._conf.kafka_prefix() + "-alarmgen")
            return pc
        except Exception as e:
            self._logger.error('Could not import libpartition: %s' % str(e))
            return None

    def handle_uve_notifq(self, part, uves):
        """
        uves : 
          This is a dict of UVEs that have changed, as per the following scheme:
          <UVE-Key> : None               # Any of the types may have changed
                                         # Used during stop_partition and GenDelete
          <UVE-Key> : { <Struct>: {} }   # The given struct may have changed
          <UVE-Key> : { <Struct>: None } # The given struct may have gone
          Our treatment of the 2nd and 3rd case above is the same
        """
        if part not in self._uveq:
            self._uveq[part] = {}
            self._logger.error('Created uveQ for part %s' % str(part))
        for uv,types in uves.iteritems():
            if types is None:
                self._uveq[part][uv] = None
            else:
                if uv in self._uveq[part]:
                    if self._uveq[part][uv] is not None:
                        for kk in types.keys():
                            self._uveq[part][uv][kk] = {}
                else:
                    self._uveq[part][uv] = {}
                    for kk in types.keys():
                        self._uveq[part][uv][kk] = {}

    def handle_resource_check(self, part, current_inst, msgs):
        """
        This function compares the set of synced redis instances
        against the set now being reported by UVEServer
       
        It returns :
        - The updated set of redis instances
        - A set of collectors to be removed
        - A dict with the collector to be added, with the contents
        """
        us_redis_inst = self._us.redis_instances()
        disc_instances = copy.deepcopy(us_redis_inst) 
        
        r_added = disc_instances - current_inst
        r_deleted = current_inst - disc_instances
        
        coll_delete = set()
        for r_inst in r_deleted:
            ipaddr = r_inst[0]
            port = r_inst[1]
            coll_delete.add(ipaddr + ":" + str(port))
        
        chg_res = {}
        for r_inst in r_added:
            coll, res = self._us.get_part(part, r_inst)
            chg_res[coll] = res

        return disc_instances, coll_delete, chg_res            

    def reconnect_agg_uve(self, lredis):
        self._logger.error("Connected to Redis for Agg")
        lredis.ping()
        for pp in self._workers.keys():
            self._workers[pp].reset_acq_time()
            self._workers[pp].kill(\
                    RuntimeError('UVE Proc failed'),
                    block=False)
            self.clear_agg_uve(lredis,
                self._instance_id,
                pp,
                self._workers[pp].acq_time())
            self.stop_uve_partition(pp)
        for part in self._uveq.keys():
            del self._uveq[part]

    def clear_agg_uve(self, redish, inst, part, acq_time):
	self._logger.error("Agg %s reset part %d, acq %d" % \
		(inst, part, acq_time))
	ppe2 = redish.pipeline()
	ppe2.hdel("AGPARTS:%s" % inst, part)
	ppe2.smembers("AGPARTKEYS:%s:%d" % (inst, part))
	pperes2 = ppe2.execute()
	ppe3 = redish.pipeline()
	# Remove all contents for this AG-Partition
	for elem in pperes2[-1]:
	    ppe3.delete("AGPARTVALUES:%s:%d:%s" % (inst, part, elem))
	ppe3.delete("AGPARTKEYS:%s:%d" % (inst, part))
	ppe3.hset("AGPARTS:%s" % inst, part, acq_time)
	pperes3 = ppe3.execute()

    def send_agg_uve(self, redish, inst, part, acq_time, rows):
        """ 
        This function writes aggregated UVEs to redis

        Each row has a UVE key, one of it's structs type names and the structs value
        If type is "None", it means that the UVE is being removed
        If value is none, it mean that struct of the UVE is being removed

        The key and typename information is also published on a redis channel
        """
        if not redish:
            assert() 
        old_acq_time = redish.hget("AGPARTS:%s" % inst, part)
        if old_acq_time is None:
            self._logger.error("Agg %s part %d new" % (inst, part))
            redish.hset("AGPARTS:%s" % inst, part, acq_time)
        else:
            # Is there stale information for this partition?
            if int(old_acq_time) != acq_time:
                self._logger.error("Agg %s stale info part %d, acqs %d,%d" % \
                        (inst, part, int(old_acq_time), acq_time))
                self.clear_agg_uve(redish, inst, part, acq_time)

        pub_list = []        
        ppe = redish.pipeline()
        check_keys = set()
        for row in rows: 
            vjson = json.dumps(row.val)
            typ = row.typ
            key = row.key
            pub_list.append({"key":key,"type":typ})
            if typ is None:
                self._logger.debug("Agg remove part %d, key %s" % (part,key))
                # The entire contents of the UVE should be removed
                ppe.srem("AGPARTKEYS:%s:%d" % (inst, part), key)
                ppe.delete("AGPARTVALUES:%s:%d:%s" % (inst, part, key))
            else:
                if row.val is None:
                    self._logger.debug("Agg remove part %d, key %s, type %s" % (part,key,typ))
                    # Remove the given struct from the UVE
                    ppe.hdel("AGPARTVALUES:%s:%d:%s" % (inst, part, key), typ)
                    check_keys.add(key)
                else:
                    self._logger.debug("Agg update part %d, key %s, type %s" % (part,key,typ))
                    ppe.sadd("AGPARTKEYS:%s:%d" % (inst, part), key)
                    ppe.hset("AGPARTVALUES:%s:%d:%s" % (inst, part, key),
                        typ, vjson)
        ppe.execute()
        
        # Find the keys that have no content (all structs have been deleted) 
        ppe4 = redish.pipeline()
        check_keys_list = list(check_keys)
        for kk in check_keys_list:
           ppe4.exists("AGPARTVALUES:%s:%d:%s" % (inst, part, kk))
        pperes4 = ppe4.execute()

        retry = False
        # From the index, removes keys for which there are now no contents
        ppe5 = redish.pipeline()
        idx = 0
        for res in pperes4:
            if not res:
                self._logger.error("Agg unexpected key %s from inst:part %s:%d" % \
                        (check_keys_list[idx], inst, part))
                ppe5.srem("AGPARTKEYS:%s:%d" % (inst, part), check_keys_list[idx])
                # TODO: alarmgen should have already figured out if all structs of 
                #       the UVE are gone, and should have sent a UVE delete
                #       We should not need to figure this out again
                retry = True
            idx += 1
        ppe5.execute()

        redish.publish('AGPARTPUB:%s:%d' % (inst, part), json.dumps(pub_list))

        if retry:
            self._logger.error("Agg unexpected rows %s" % str(rows))
            assert()
        
    def run_uve_processing(self):
        """
        This function runs in its own gevent, and provides state compression
        for UVEs.
        Kafka worker (PartitionHandler)  threads detect which UVE have changed
        and accumulate them onto a set. When this gevent runs, it processes
        all UVEs of the set. Even if this gevent cannot run for a while, the
        set should not grow in an unbounded manner (like a queue can)
        """

        lredis = None
        oldworkerset = None
        while True:
            workerset = {}
            for part in self._workers.keys():
                if self._workers[part]._up:
                    workerset[part] = self._workers[part].acq_time()
            if workerset != oldworkerset:
                if self.disc:
                    data = {
                        'ip-address': self._conf.host_ip(),
                        'instance-id': self._instance_id,
                        'redis-port': str(self._conf.redis_server_port()),
                        'partitions': json.dumps(workerset)
                    }
                    self._logger.error("Disc Publish to %s : %s"
                                  % (str(self._conf.discovery()), str(data)))
                    self.disc.publish(ALARM_GENERATOR_SERVICE_NAME, data)
                oldworkerset = copy.deepcopy(workerset)
             
            for part in self._uveqf.keys():
                self._logger.error("Stop UVE processing for %d:%d" % \
                        (part, self._uveqf[part]))
                self.stop_uve_partition(part)
                del self._uveqf[part]
                if part in self._uveq:
                    del self._uveq[part]
            prev = time.time()
            try:
                if lredis is None:
                    lredis = redis.StrictRedis(
                            host="127.0.0.1",
                            port=self._conf.redis_server_port(),
                            password=self._conf.redis_password(),
                            db=7)
                    self.reconnect_agg_uve(lredis)
                gevs = {}
                pendingset = {}
                for part in self._uveq.keys():
                    if not len(self._uveq[part]):
                        continue
                    self._logger.info("UVE Process for %d" % part)

                    # Allow the partition handlers to queue new UVEs without
                    # interfering with the work of processing the current UVEs
                    pendingset[part] = copy.deepcopy(self._uveq[part])
                    self._uveq[part] = {}

                    gevs[part] = gevent.spawn(self.handle_uve_notif,part,\
                        pendingset[part])
                if len(gevs):
                    gevent.joinall(gevs.values())
                    for part in gevs.keys():
                        # If UVE processing failed, requeue the working set
                        outp = gevs[part].get()
                        if outp is None:
                            self._logger.error("UVE Process failed for %d" % part)
                            self.handle_uve_notifq(part, pendingset[part])
                        elif not part in self._workers:
                            self._logger.error(
                                    "Part %d is gone, cannot process UVEs" % part)
                        else:
                            acq_time = self._workers[part].acq_time()
                            if len(outp):
                                rows = []
                                for ku,vu in outp.iteritems():
                                    if vu is None:
                                        # This message has no type!
                                        # Its used to indicate a delete of the entire UVE
                                        rows.append(OutputRow(key=ku, typ=None, val=None))
                                        if len(rows) >= self._max_out_rows:
                                            self.send_agg_uve(lredis,
                                                self._instance_id,
                                                part,
                                                acq_time,
                                                rows)
                                            rows[:] = []
                                        continue
                                    for kt,vt in vu.iteritems():
                                        rows.append(OutputRow(key=ku, typ=kt, val=vt))
                                        if len(rows) >= self._max_out_rows:
                                            self.send_agg_uve(lredis,
                                                self._instance_id,
                                                part,
                                                acq_time,
                                                rows)
                                            rows[:] = []
                                # Flush all remaining rows
                                if len(rows):
                                    self.send_agg_uve(lredis,
                                        self._instance_id,
                                        part,
                                        acq_time,
                                        rows)
                                    rows[:] = []

            except Exception as ex:
                template = "Exception {0} in uve proc. Arguments:\n{1!r}"
                messag = template.format(type(ex).__name__, ex.args)
                self._logger.error("%s : traceback %s" % \
                                  (messag, traceback.format_exc()))
                lredis = None
                gevent.sleep(1)
                        
            curr = time.time()
            if (curr - prev) < 0.5:
                gevent.sleep(0.5 - (curr - prev))
            else:
                self._logger.info("UVE Process saturated")
                gevent.sleep(0)
             
    def stop_uve_partition(self, part):
        if not part in self.ptab_info:
            return
        for tk in self.ptab_info[part].keys():
            for rkey in self.ptab_info[part][tk].keys():
                uk = tk + ":" + rkey
                if tk in self.tab_alarms:
                    if uk in self.tab_alarms[tk]:
                        del self.tab_alarms[tk][uk]
                        ustruct = UVEAlarms(name = rkey, deleted = True)
                        alarm_msg = AlarmTrace(data=ustruct, \
                                table=tk, sandesh=self._sandesh)
                        self._logger.error('send del alarm for stop: %s' % \
                                (alarm_msg.log()))
                        alarm_msg.send(sandesh=self._sandesh)
                del self.ptab_info[part][tk][rkey]
                self._logger.error("UVE %s deleted in stop" % (uk))
            del self.ptab_info[part][tk]
        del self.ptab_info[part]

    def handle_uve_notif(self, part, uves):
        """
        Call this function when a UVE has changed. This can also
        happed when taking ownership of a partition, or when a
        generator is deleted.
        Args:
            part   : Partition Number
            uve    : dict, where the key is the UVE Name.
                     The value is either a dict of UVE structs, or "None",
                     which means that all UVE structs should be processed.

        Returns: 
            status of operation (True for success)
        """
        self._logger.debug("Changed part %d UVEs : %s" % (part, str(uves)))
        success = True
        output = {}
        for uv,types in uves.iteritems():
            tab = uv.split(':',1)[0]
            if tab not in self.tab_perf:
                self.tab_perf[tab] = AGTabStats()

            if part in self._uvestats:
                # Record stats on UVE Keys being processed
                if not tab in self._uvestats[part]:
                    self._uvestats[part][tab] = {}
                if uv in self._uvestats[part][tab]:
                    self._uvestats[part][tab][uv] += 1
                else:
                    self._uvestats[part][tab][uv] = 1

            uve_name = uv.split(':',1)[1]
            prevt = UTCTimestampUsec() 
            filters = {}
            if types:
                filters["cfilt"] = {}
                for typ in types.keys():
                    filters["cfilt"][typ] = set()

            failures, uve_data = self._us.get_uve(uv, True, filters)

            if failures:
                success = False
            self.tab_perf[tab].record_get(UTCTimestampUsec() - prevt)
            # Handling Agg UVEs
            if not part in self.ptab_info:
                self._logger.error("Creating UVE table for part %s" % str(part))
                self.ptab_info[part] = {}

            if not tab in self.ptab_info[part]:
                self.ptab_info[part][tab] = {}

            if uve_name not in self.ptab_info[part][tab]:
                self.ptab_info[part][tab][uve_name] = AGKeyInfo(part)
            prevt = UTCTimestampUsec()
            output[uv] = {}
            touched = False
            if not types:
                self.ptab_info[part][tab][uve_name].update(uve_data)
                if len(self.ptab_info[part][tab][uve_name].removed()):
                    touched = True
                    self._logger.info("UVE %s removed structs %s" % (uve_name, \
                            self.ptab_info[part][tab][uve_name].removed()))
                    for rems in self.ptab_info[part][tab][uve_name].removed():
                        output[uv][rems] = None
                if len(self.ptab_info[part][tab][uve_name].changed()):
                    touched = True
                    self._logger.debug("UVE %s changed structs %s" % (uve_name, \
                            self.ptab_info[part][tab][uve_name].changed()))
                    for chgs in self.ptab_info[part][tab][uve_name].changed():
                        output[uv][chgs] = \
                                self.ptab_info[part][tab][uve_name].values()[chgs]
                if len(self.ptab_info[part][tab][uve_name].added()):
                    touched = True
                    self._logger.debug("UVE %s added structs %s" % (uve_name, \
                            self.ptab_info[part][tab][uve_name].added()))
                    for adds in self.ptab_info[part][tab][uve_name].added():
                        output[uv][adds] = \
                                self.ptab_info[part][tab][uve_name].values()[adds]
            else:
                for typ in types:
                    val = None
                    if typ in uve_data:
                        val = uve_data[typ]
                    self.ptab_info[part][tab][uve_name].update_single(typ, val)
                    if len(self.ptab_info[part][tab][uve_name].removed()):
                        touched = True
                        self._logger.info("UVE %s removed structs %s" % (uve_name, \
                                self.ptab_info[part][tab][uve_name].removed()))
                        for rems in self.ptab_info[part][tab][uve_name].removed():
                            output[uv][rems] = None
                    if len(self.ptab_info[part][tab][uve_name].changed()):
                        touched = True
                        self._logger.debug("UVE %s changed structs %s" % (uve_name, \
                                self.ptab_info[part][tab][uve_name].changed()))
                        for chgs in self.ptab_info[part][tab][uve_name].changed():
                            output[uv][chgs] = \
                                    self.ptab_info[part][tab][uve_name].values()[chgs]
                    if len(self.ptab_info[part][tab][uve_name].added()):
                        touched = True
                        self._logger.debug("UVE %s added structs %s" % (uve_name, \
                                self.ptab_info[part][tab][uve_name].added()))
                        for adds in self.ptab_info[part][tab][uve_name].added():
                            output[uv][adds] = \
                                    self.ptab_info[part][tab][uve_name].values()[adds]
            if not touched:
                del output[uv]
            local_uve = self.ptab_info[part][tab][uve_name].values()
            
            self.tab_perf[tab].record_pub(UTCTimestampUsec() - prevt)

            if len(local_uve.keys()) == 0:
                self._logger.info("UVE %s deleted in proc" % (uv))
                del self.ptab_info[part][tab][uve_name]
                output[uv] = None
                
                # Both alarm and non-alarm contents are gone.
                # We do not need to do alarm evaluation
                continue
            
            # Withdraw the alarm if the UVE has no non-alarm structs
            if len(local_uve.keys()) == 1 and "UVEAlarms" in local_uve:
                if tab in self.tab_alarms:
                    if uv in self.tab_alarms[tab]:
                        del self.tab_alarms[tab][uv]
                        ustruct = UVEAlarms(name = uve_name, deleted = True)
                        alarm_msg = AlarmTrace(data=ustruct, table=tab, \
                                sandesh=self._sandesh)
                        self._logger.info('send del alarm: %s' % (alarm_msg.log()))
                        alarm_msg.send(sandesh=self._sandesh)
                continue
 
            # Handing Alarms
            if not self.mgrs.has_key(tab):
                continue
            prevt = UTCTimestampUsec()

            #TODO: We may need to remove alarm from local_uve before 
            #      alarm evaluation
            # if "UVEAlarms" in uve_data:
            #     del uve_data["UVEAlarms"]
            prevt = UTCTimestampUsec()
            aproc = AlarmProcessor(self._logger)
            self.mgrs[tab].map(aproc.process_alarms, uv, local_uve)
            new_uve_alarms = aproc.uve_alarms
            self.tab_perf[tab].record_call(UTCTimestampUsec() - prevt)

            del_types = []
            if self.tab_alarms[tab].has_key(uv):
                for nm, uai in self.tab_alarms[tab][uv].iteritems():
                    uai2 = copy.deepcopy(uai)
                    uai2.timestamp = 0
                    uai2.token = ""
                    uai2.ack = False
                    # This type was present earlier, but is now gone
                    if not new_uve_alarms.has_key(nm):
                        del_types.append(nm)
                    else:
                        # This type has no new information
                        if uai2 == new_uve_alarms[nm]:
                            del new_uve_alarms[nm]
            if len(del_types) != 0  or \
                    len(new_uve_alarms) != 0:
                self._logger.debug("Alarm[%s] Deleted %s" % \
                        (tab, str(del_types))) 
                self._logger.debug("Alarm[%s] Updated %s" % \
                        (tab, str(new_uve_alarms))) 
                # These alarm types are new or updated
                for nm, uai2 in new_uve_alarms.iteritems():
                    uai = copy.deepcopy(uai2)
                    uai.timestamp = UTCTimestampUsec()
                    uai.token = Controller.token(self._sandesh, uai.timestamp)
                    if not self.tab_alarms[tab].has_key(uv):
                        self.tab_alarms[tab][uv] = {}
                    self.tab_alarms[tab][uv][nm] = uai
                # These alarm types are now gone
                for dnm in del_types:
                    del self.tab_alarms[tab][uv][dnm]
                    
                ustruct = None
                if len(self.tab_alarms[tab][uv]) == 0:
                    ustruct = UVEAlarms(name = uve_name,
                            deleted = True)
                    del self.tab_alarms[tab][uv]
                else:
                    alm_copy = copy.deepcopy(self.tab_alarms[tab][uv])
                    ustruct = UVEAlarms(name = uve_name,
                            alarms = alm_copy.values())
                alarm_msg = AlarmTrace(data=ustruct, table=tab, \
                        sandesh=self._sandesh)
                self._logger.info('send alarm: %s' % (alarm_msg.log()))
                alarm_msg.send(sandesh=self._sandesh)
        if success:
            return output
        else:
            return None
 
    def handle_UVETableInfoReq(self, req):
        if req.partition == -1:
            parts = self.ptab_info.keys()
        else:
            parts = [req.partition]
        
        self._logger.info("Got UVETableInfoReq : %s" % str(parts))
        np = 1
        for part in parts:
            if part not in self.ptab_info:
                continue
            tables = []
            for tab in self.ptab_info[part].keys():
                uvel = []
                for uk,uv in self.ptab_info[part][tab].iteritems():
                    types = []
                    for tk,tv in uv.values().iteritems():
                        types.append(UVEStructInfo(type = tk,
                                content = json.dumps(tv)))
                    uvel.append(UVEObjectInfo(
                            name = uk, structs = types))
                tables.append(UVETableInfo(table = tab, uves = uvel))
            resp = UVETableInfoResp(partition = part)
            resp.tables = tables

            if np == len(parts):
                mr = False
            else:
                mr = True
            resp.response(req.context(), mr)
            np = np + 1

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

    def handle_UVETablePerfReq(self, req):
        status = False
        if req.table == "all":
            parts = self.tab_perf_prev.keys()
        else:
            parts = [req.table]
        self._logger.info("Got UVETablePerfReq : %s" % str(parts))
        np = 1
        for pt in parts:
            resp = UVETablePerfResp(table = pt)
            resp.call_time = self.tab_perf_prev[pt].call_result()
            resp.get_time = self.tab_perf_prev[pt].get_result()
            resp.pub_time = self.tab_perf_prev[pt].pub_result()
            resp.updates = self.tab_perf_prev[pt].get_n

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
            if partno in self._workers:
                self._logger.info("Dup partition %d" % partno)
            else:
                cdisc = None
                if self.disc:
                    cdisc = client.DiscoveryClient(
                        self._conf.discovery()['server'],
                        self._conf.discovery()['port'],
                        ModuleNames[Module.ALARM_GENERATOR],
                        '%s-%s-%d' % (self._hostname,
                            self._instance_id, partno))
                ph = UveStreamProc(','.join(self._conf.kafka_broker_list()),
                        partno, self._conf.kafka_prefix()+"-uve-" + str(partno),
                        self._logger,
                        self.handle_uve_notifq, self._conf.host_ip(),
                        self.handle_resource_check,
                        self._instance_id,
                        self._conf.redis_server_port(),
                        self._conf.kafka_prefix()+"-workers")
                ph.start()
                self._workers[partno] = ph
                self._uvestats[partno] = {}
                tout = 600
                idx = 0
                while idx < tout:
                    # When this partitions starts,
                    # uveq will get created
                    if partno not in self._uveq:
                        gevent.sleep(.1)
                    else:
                        break
                    idx += 1
                if partno in self._uveq:
                    status = True 
                else:
                    # TODO: The partition has not started yet,
                    #       but it still might start later.
                    #       We possibly need to exit
                    status = False
                    self._logger.error("Unable to start partition %d" % partno)
        else:
            if partno in self._workers:
                ph = self._workers[partno]
                self._logger.error("Kill part %s" % str(partno))
                ph.kill()
                res,db = ph.get(False)
                self._logger.error("Returned " + str(res))
                self._uveqf[partno] = self._workers[partno].acq_time()
                del self._workers[partno]
                del self._uvestats[partno]

                tout = 600
                idx = 0
                while idx < tout:
                    # When this partitions stop.s
                    # uveq will get destroyed
                    if partno in self._uveq:
                        gevent.sleep(.1)
                    else:
                        break
                    idx += 1
                if partno not in self._uveq:
                    status = True 
                else:
                    # TODO: The partition has not stopped yet
                    #       but it still might stop later.
                    #       We possibly need to exit
                    status = False
                    self._logger.error("Unable to stop partition %d" % partno)
            else:
                self._logger.info("No partition %d" % partno)

        return status
    
    def handle_PartitionOwnershipReq(self, req):
        self._logger.info("Got PartitionOwnershipReq: %s" % str(req))
        status = self.partition_change(req.partition, req.ownership)

        resp = PartitionOwnershipResp()
        resp.status = status
	resp.response(req.context())
               
    def process_stats(self):
        ''' Go through the UVEKey-Count stats collected over 
            the previous time period over all partitions
            and send it out
        '''
        self.tab_perf_prev = copy.deepcopy(self.tab_perf)
        for kt in self.tab_perf.keys():
            #self.tab_perf_prev[kt] = copy.deepcopy(self.tab_perf[kt])
            self.tab_perf[kt].reset()

        s_partitions = set()
        s_keys = set()
        n_updates = 0
        for pk,pc in self._workers.iteritems():
            s_partitions.add(pk)
            din = pc.stats()
            dout = copy.deepcopy(self._uvestats[pk])
            self._uvestats[pk] = {}
            for ktab,tab in dout.iteritems():
                au_keys = []
                for uk,uc in tab.iteritems():
                    s_keys.add(uk)
                    n_updates += uc
                    ukc = UVEKeyCount()
                    ukc.key = uk
                    ukc.count = uc
                    au_keys.append(ukc)
                au_obj = AlarmgenUpdate(name=self._sandesh._source + ':' + \
                        self._sandesh._node_type + ':' + \
                        self._sandesh._module + ':' + \
                        self._sandesh._instance_id,
                        partition = pk,
                        table = ktab,
                        o = au_keys,
                        i = None,
                        sandesh=self._sandesh)
                self._logger.debug('send output stats: %s' % (au_obj.log()))
                au_obj.send(sandesh=self._sandesh)

            for ktab,tab in din.iteritems():
                au_notifs = []
                for kcoll,coll in tab.iteritems():
                    for kgen,gen in coll.iteritems():
                        for tk,tc in gen.iteritems():
                            tkc = UVETypeInfo()
                            tkc.type= tk
                            tkc.count = tc
                            tkc.generator = kgen
                            tkc.collector = kcoll
                            au_notifs.append(tkc)
                au_obj = AlarmgenUpdate(name=self._sandesh._source + ':' + \
                        self._sandesh._node_type + ':' + \
                        self._sandesh._module + ':' + \
                        self._sandesh._instance_id,
                        partition = pk,
                        table = ktab,
                        o = None,
                        i = au_notifs,
                        sandesh=self._sandesh)
                self._logger.debug('send input stats: %s' % (au_obj.log()))
                au_obj.send(sandesh=self._sandesh)

        au = AlarmgenStatus()
        au.name = self._hostname
        au.counters = []
        au.alarmgens = []
        ags = AlarmgenStats()
        ags.instance =  self._instance_id
        ags.partitions = len(s_partitions)
        ags.keys = len(s_keys)
        ags.updates = n_updates
        au.counters.append(ags)

        agname = self._sandesh._source + ':' + \
                        self._sandesh._node_type + ':' + \
                        self._sandesh._module + ':' + \
                        self._sandesh._instance_id
        au.alarmgens.append(agname)
 
        atrace = AlarmgenStatusTrace(data = au, sandesh = self._sandesh)
        self._logger.debug('send alarmgen status : %s' % (atrace.log()))
        atrace.send(sandesh=self._sandesh)
         
    def handle_PartitionStatusReq(self, req):
        ''' Return the entire contents of the UVE DB for the 
            requested partitions
        '''
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
                resp.offset = self._workers[pt]._partoffset
                resp.uves = []
                for kcoll,coll in self._workers[pt].contents().iteritems():
                    uci = UVECollInfo()
                    uci.collector = kcoll
                    uci.uves = []
                    for kgen,gen in coll.iteritems():
                        ugi = UVEGenInfo()
                        ugi.generator = kgen
                        ugi.uves = []
                        for tabk,tabc in gen.iteritems():
                            for uk,uc in tabc.iteritems():
                                ukc = UVEKeyInfo()
                                ukc.key = tabk + ":" + uk
                                ukc.types = []
                                for tk,tc in uc.iteritems():
                                    uvtc = UVETypeCount()
                                    uvtc.type = tk
                                    uvtc.count = tc["c"]
                                    uvtc.agg_uuid = str(tc["u"])
                                    ukc.types.append(uvtc)
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

    def alarm_ack_callback(self, alarm_req):
        '''
        Callback function for sandesh alarm acknowledge request.
        This method is passed as a parameter in the init_generator().
        Upon receiving the SandeshAlarmAckRequest, the corresponding
        handler defined in the sandesh library would invoke this callback.
        This function returns one of the response codes defined in
        SandeshAlarmAckResponseCode.
        '''
        self._logger.debug('Alarm acknowledge request callback: %s' %
                           str(alarm_req))
        table = alarm_req.table
        uname = alarm_req.table+':'+alarm_req.name
        atype = alarm_req.type
        try:
            alarm_type = self.tab_alarms[table][uname][atype]
        except KeyError:
            return SandeshAlarmAckResponseCode.ALARM_NOT_PRESENT
        else:
            # Either the timestamp sent by the client is invalid or
            # the alarm is updated.
            if alarm_type.timestamp != alarm_req.timestamp:
                return SandeshAlarmAckResponseCode.INVALID_ALARM_REQUEST
            # If the alarm was already acknowledged, just return SUCCESS.
            if alarm_type.ack:
                return SandeshAlarmAckResponseCode.SUCCESS
            # All sanity checks passed. Acknowledge the alarm.
            alarm_type.ack = True
            alarm = copy.deepcopy(self.tab_alarms[table][uname])
            alarm_data = UVEAlarms(name=alarm_req.name, alarms=alarm.values())
            alarm_sandesh = AlarmTrace(data=alarm_data, table=table,
                                       sandesh=self._sandesh)
            alarm_sandesh.send(sandesh=self._sandesh)
            return SandeshAlarmAckResponseCode.SUCCESS
    # end alarm_ack_callback

    def disc_cb_coll(self, clist):
        '''
        Analytics node may be brought up/down any time. For UVE aggregation,
        alarmgen needs to know the list of all Analytics nodes (redis-uves).
        Periodically poll the Collector list [in lieu of 
        redi-uve nodes] from the discovery. 
        '''
        self._logger.error("Discovery Collector callback : %s" % str(clist))
        newlist = []
        for elem in clist:
            ipaddr = elem["ip-address"]
            cpid = 0
            if "pid" in elem:
                cpid = int(elem["pid"])
            newlist.append((ipaddr, self._conf.redis_server_port(), cpid))
        self._us.update_redis_uve_list(newlist)

    def disc_cb_ag(self, alist):
        '''
        Analytics node may be brought up/down any time. For partitioning,
        alarmgen needs to know the list of all Analytics nodes (alarmgens).
        Periodically poll the alarmgen list from the discovery service
        '''
        self._logger.error("Discovery AG callback : %s" % str(alist))
        newlist = []
        for elem in alist:
            ipaddr = elem["ip-address"]
            inst = elem["instance-id"]
            newlist.append(ipaddr + ":" + inst)

        # We should always include ourselves in the list of memebers
        newset = set(newlist)
        newset.add(self._libpart_name)
        newlist = list(newset)
        if not self._libpart:
            self._libpart = self.start_libpart(newlist)
        else:
            self._libpart.update_cluster_list(newlist)

    def run_cpu_mon(self):
        alarmgen_cpu_info = CpuInfoData()
        while True:
            before = time.time()
            mod_cpu_info = ModuleCpuInfo()
            mod_cpu_info.module_id = self._moduleid
            mod_cpu_info.instance_id = self._instance_id
            mod_cpu_info.cpu_info = alarmgen_cpu_info.get_cpu_info(
                system=False)
            mod_cpu_state = ModuleCpuState()
            mod_cpu_state.name = self._hostname

            mod_cpu_state.module_cpu_info = [mod_cpu_info]

            alarmgen_cpu_state_trace = ModuleCpuStateTrace(\
                    data=mod_cpu_state, sandesh = self._sandesh)
            alarmgen_cpu_state_trace.send(sandesh=self._sandesh)

            aly_cpu_state = AnalyticsCpuState()
            aly_cpu_state.name = self._hostname

            aly_cpu_info = ProcessCpuInfo()
            aly_cpu_info.module_id= self._moduleid
            aly_cpu_info.inst_id = self._instance_id
            aly_cpu_info.cpu_share = mod_cpu_info.cpu_info.cpu_share
            aly_cpu_info.mem_virt = mod_cpu_info.cpu_info.meminfo.virt
            aly_cpu_info.mem_res = mod_cpu_info.cpu_info.meminfo.res
            aly_cpu_state.cpu_info = [aly_cpu_info]

            aly_cpu_state_trace = AnalyticsCpuStateTrace(\
                    data=aly_cpu_state, sandesh = self._sandesh)
            aly_cpu_state_trace.send(sandesh=self._sandesh)

            # Send out the UVEKey-Count stats for this time period
            self.process_stats()

            duration = time.time() - before
            if duration < 60:
                gevent.sleep(60 - duration)
            else:
                self._logger.error("Periodic collection took %s sec" % duration)

    def run(self):
        self.gevs = [ gevent.spawn(self.run_cpu_mon),
                      gevent.spawn(self.run_uve_processing)]

        if self.disc:
            sp1 = ServicePoller(self._logger, CollectorTrace,
                                self.disc,
                                COLLECTOR_DISCOVERY_SERVICE_NAME,
                                self.disc_cb_coll, self._sandesh)

            sp1.start()
            self.gevs.append(sp1)

            sp2 = ServicePoller(self._logger, AlarmgenTrace,
                                self.disc, ALARM_GENERATOR_SERVICE_NAME,
                                self.disc_cb_ag, self._sandesh)
            sp2.start()
            self.gevs.append(sp2)

        try:
            gevent.joinall(self.gevs)
        except KeyboardInterrupt:
            print 'AlarmGen Exiting on ^C'
        except gevent.GreenletExit:
            self._logger.error('AlarmGen Exiting on gevent-kill')
        except:
            raise
        finally:
            self._logger.error('AlarmGen stopping everything')
            self.stop()

    def stop(self):
        self._sandesh._client._connection.set_admin_state(down=True)
        self._sandesh.uninit()
        l = len(self.gevs)
        for idx in range(0,l):
            self._logger.error('AlarmGen killing %d of %d' % (idx+1, l))
            self.gevs[0].kill()
            self._logger.error('AlarmGen joining %d of %d' % (idx+1, l))
            self.gevs[0].join()
            self._logger.error('AlarmGen stopped %d of %d' % (idx+1, l))
            self.gevs = self.gevs[1:]

    def sigterm_handler(self):
        self.stop()
        exit()

def setup_controller(argv):
    config = CfgParser(argv)
    config.parse()
    return Controller(config)

def main(args=None):
    controller = setup_controller(args or ' '.join(sys.argv[1:]))
    gevent.hub.signal(signal.SIGTERM, controller.sigterm_handler)
    gv = gevent.getcurrent()
    gv._main_obj = controller
    controller.run()

if __name__ == '__main__':
    main()
