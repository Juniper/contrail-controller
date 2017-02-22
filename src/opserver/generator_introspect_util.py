#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

#
# Generator Introspect Util
#


from introspect_util import IntrospectUtilBase, XmlDrv, EtreeToDict


class GeneratorIntrospectUtil(IntrospectUtilBase):
    def __init__(self, ip, port, config=None):
        super(GeneratorIntrospectUtil, self).__init__(ip, port, XmlDrv, config)
    # end __init__

    def send_alarm_ack_request(self, table, name, alarm_type, timestamp):
        path = 'Snh_SandeshAlarmAckRequest'
        query = {'table':table, 'name':name, 'type':alarm_type,
                 'timestamp':timestamp}
        xpath = '/SandeshAlarmAckResponse'
        res = self.dict_get(path, query)
        return EtreeToDict(xpath).get_all_entry(res)
    # end send_alarm_ack_request

# end class GeneratorIntrospectUtil
