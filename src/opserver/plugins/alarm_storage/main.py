from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class StorageClusterState(AlarmBase):
    """Storage Cluster warning/errors.
       Storage Cluster is not in the normal operating state"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'StorageCluster.info_stats.status',
                        'operation': '!=',
                        'operand2': {
                            'json_value': '0'
                        },
                        'variables': \
                            ['StorageCluster.info_stats.health_summary']
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
