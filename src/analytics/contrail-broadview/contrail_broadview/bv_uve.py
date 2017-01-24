#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import pprint, socket
from pysandesh.sandesh_base import *
from pysandesh.connection_info import ConnectionState
from gen_py.broadview.ttypes import \
         PRouterBroadViewInfo, \
         Device, \
         IngressPortPriorityGroup, \
         IngressPortServicePool, \
         IngressServicePool, \
         EgressPortServicePool, \
         EgressServicePool, \
         EgressUcQueue, \
         EgressUcQueueGroup, \
         EgressMcQueue, \
         EgressCpuQueue, \
         EgressRqeQueue
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT

class BroadViewOL(object):
    _raw_params = (
            "device",
            "ingress-service-pool",
            "ingress-port-service-pool",
            "ingress-port-priority-group",
            "egress-port-service-pool",
            "egress-service-pool",
            "egress-uc-queue",
            "egress-uc-queue-group",
            "egress-mc-queue",
            "egress-cpu-queue",
            "egress-rqe-queue",
            )
    def __init__(self, conf):
        self._conf = conf
        module = Module.CONTRAIL_BROADVIEW
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        self._hostname = socket.gethostname()
        self._instance_id = '0'
        if self._conf.sandesh_send_rate_limit() is not None:
            SandeshSystem.set_sandesh_send_rate_limit( \
                self._conf.sandesh_send_rate_limit())
        sandesh_global.init_generator(self._moduleid, self._hostname,
                                      self._node_type_name,
                                      self._instance_id,
                                      self._conf.collectors(), 
                                      self._node_type_name,
                                      self._conf.http_port(),
                                      ['contrail_broadview.gen_py'],
                                      config=self._conf.sandesh_config())
        sandesh_global.set_logging_params(
            enable_local_log=self._conf.log_local(),
            category=self._conf.log_category(),
            level=self._conf.log_level(),
            file=self._conf.log_file(),
            enable_syslog=self._conf.use_syslog(),
            syslog_facility=self._conf.syslog_facility())
        self.mk_maps()

    def mk_maps(self):
        self._r2s, self._s2r = {}, {}
        for rp in self._raw_params:
            x = rp.split('-')
            n = ''.join([x[0]] + map(lambda p: p.capitalize(), x[1:]))
            self._r2s[rp] = n
            self._s2r[n] = rp

    def map_realm_name(self, realm):
        return self._r2s.get(realm, None)

    def get_raw_params(self):
        return self._r2s.keys()

    def send(self, data):
        pprint.pprint(data)
        if 'device' in data:
            data['device'] = Device(data['device'])
        for prms in self._s2r.keys():
            if prms != 'device' and prms in data:
                cl = prms[0].upper() + prms[1:]
                fn = locals().get(cl, globals().get(cl))
                # print cl, fn, data[prms]
                data[prms] = map(lambda x: fn(**x), data[prms])
        objlog = PRouterBroadViewInfo(**data)
        objlog.send()
        
    def delete(self, name):
         PRouterBroadViewInfo(name=name, deleted=True).send()

