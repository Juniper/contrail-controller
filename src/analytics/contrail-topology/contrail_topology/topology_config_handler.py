#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#


from topology_config_db import DBBaseCT, LogicalInterfaceCT, \
    VirtualMachineInterfaceCT
from config_handler import ConfigHandler


class TopologyConfigHandler(ConfigHandler):

    REACTION_MAP = {
        'logical_interface': {
            'self': []
        },
        'virtual_machine_interface': {
            'self': []
        }
    }

    def __init__(self, sandesh, rabbitmq_cfg, cassandra_cfg):
        service_id = sandesh.source_id()+':'+sandesh.module()+':'+ \
            sandesh.instance_id()
        super(TopologyConfigHandler, self).__init__(sandesh, service_id,
              rabbitmq_cfg, cassandra_cfg, DBBaseCT, self.REACTION_MAP)
    # end __init__

    def get_logical_interfaces(self):
        return LogicalInterfaceCT.items()
    # end get_logical_interfaces

    def get_virtual_machine_interfaces(self):
        return VirtualMachineInterfaceCT.items()
    # end get_virtual_machine_interfaces

    def get_virtual_machine_interface(self, fq_name, uuid=None):
        if fq_name:
            return VirtualMachineInterfaceCT.get(fq_name)
        elif uuid:
            return VirtualMachineInterfaceCT.get_by_uuid(uuid)
        return None
    # end get_virtual_machine_interface


# end class TopologyConfigHandler
