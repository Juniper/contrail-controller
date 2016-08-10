from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class PendingCompactionTasks(AlarmBase):
    """Pending Compaction Tasks crosses a threshold.
       NodeMgr reports pending compaction tasks in
       CassandraStatusData.cassandra_compaction_task.pending_compaction_tasks"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'CassandraStatusData.' + \
                                    'cassandra_compaction_task.pending_compaction_tasks',
                        'operation': '>=',
                        'operand2': {
                            'json_value': '100'
                        },
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
