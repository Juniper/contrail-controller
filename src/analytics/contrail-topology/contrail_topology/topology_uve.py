import pprint, socket
from pysandesh.sandesh_base import *
from pysandesh.connection_info import ConnectionState
from gen_py.link.ttypes import LinkEntry, PRouterLinkEntry, PRouterLinkUVE
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT

class LinkUve(object):
    def __init__(self, conf):
        self._conf = conf
        module = Module.CONTRAIL_TOPOLOGY
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
                                      ['contrail_topology.gen_py'])
        sandesh_global.set_logging_params(
            enable_local_log=self._conf.log_local(),
            category=self._conf.log_category(),
            level=self._conf.log_level(),
            file=self._conf.log_file(),
            enable_syslog=self._conf.use_syslog(),
            syslog_facility=self._conf.syslog_facility())
        #ConnectionState.init(sandesh_global, self._hostname, self._moduleid,
        #    self._instance_id,
        #    staticmethod(ConnectionState.get_process_state_cb),
        #    NodeStatusUVE, NodeStatus)
        #
        # generator_init()

    def send(self, data):
        pprint.pprint(data)
        for prouter in data:
            lt = map(lambda x: LinkEntry(**x), data[prouter])
            uve = PRouterLinkUVE(data=PRouterLinkEntry(name=prouter, 
                        link_table=lt))
            uve.send()

