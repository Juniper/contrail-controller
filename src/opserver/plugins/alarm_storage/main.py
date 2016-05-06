from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json
import re

class StorageClusterState(AlarmBase):
    """Storage Cluster warning/errors.
       Storage Cluster is not in the normal operating state"""
    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):
        or_list = []
        v1 = uve_data["StorageCluster"]["info_stats"]
        if isinstance(v1[0], list):
            i = 0
            while v1[i] is not None:
                cluster_stat = v1[i][0][0]
                status =  cluster_stat['status']
                if status != 0:
                    break
                i = i + 1
        else:
            cluster_stat = v1[0]
            status =  cluster_stat['status']
        if status != 0:
            if status == 1:
                self._sev = AlarmBase.SYS_WARN
            else:
                self._sev = AlarmBase.SYS_ERR
            health_summary = cluster_stat['health_summary']
            and_list = [AlarmConditionMatch(condition=AlarmCondition(
                operation="!=", operand1="StorageCluster.info_stats.status",
                operand2=json.dumps(0),
                vars=["StorageCluster.info_stats.health_summary"]),
                match=[AlarmMatch(json_operand1_value=json.dumps(status),
                    json_vars={
                        "StorageCluster.info_stats.health_summary":\
                        json.dumps(health_summary)})])]
            or_list.append(AlarmRuleMatch(rule=and_list))
        if len(or_list):
            return or_list
        else:
            return None
