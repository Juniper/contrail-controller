#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#


from snmp_config_db import DBBaseSC, PhysicalRouterSC
from config_handler import ConfigHandler


class SnmpConfigHandler(ConfigHandler):

    REACTION_MAP = {
        'physical_router': {
            'self': []
        }
    }

    def __init__(self, sandesh, rabbitmq_cfg, cassandra_cfg):
        service_id = sandesh.source_id()+':'+sandesh.module()+':'+ \
            sandesh.instance_id()
        super(SnmpConfigHandler, self).__init__(sandesh, service_id,
              rabbitmq_cfg, cassandra_cfg, DBBaseSC, self.REACTION_MAP)
    # end __init__

    def get_physical_routers(self):
        return PhysicalRouterSC.items()
    # end get_physical_routers


# end class SnmpConfigHandler
