from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class PendingCassandraCompactionTasks(AlarmBase):
    """Pending compaction tasks in cassandra crosses the configured threshold."""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'CassandraStatusData.' + \
                                    'cassandra_compaction_task.pending_compaction_tasks',
                        'operation': '>=',
                        'operand2': {
                            'json_value': '300'
                        },
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
