#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
sys.path.append("../common/tests")
from lxml import etree
from test_utils import stub

class FakeNetconfManager(object):
    def __init__(self, host, *args, **kwargs):
        self.host = host
        self.configs = []

    def __enter__(self):
        return self
    __exit__ = stub

    def edit_config(self, target, config, test_option, default_operation):
        self.configs.append(config)

    def rpc(self, ele):
        version = '14.2R6'
        model = 'mx480'
        name = 'mx480'
        if self.host == '199.199.199.199': #unsupported netconf device ip
            version = 'Unknown'
            model = 'X-Model'
            name = 'X-Name'
        res = '<rpc-reply xmlns:junos="http://xml.juniper.net/junos/%s/junos"> \
                  <software-information> \
                     <host-name>cmbu-tasman</host-name> \
                     <product-model>%s</product-model> \
                     <product-name>%s</product-name> \
                     <package-information> \
                         <name>junos-version</name> \
                         <comment>Junos: %s</comment> \
                     </package-information> \
                     <package-information> \
                         <name>junos</name> \
                         <comment>JUNOS Base OS boot 14.2R6</comment> \
                     </package-information> \
                  </software-information> \
                </rpc-reply>'%(version, model, name, version)
        return etree.fromstring(res)

    commit = stub
# end FakeNetconfManager

netconf_managers = {}
def fake_netconf_connect(host, *args, **kwargs):
    return netconf_managers.setdefault(host, FakeNetconfManager(host, args, kwargs))

class FakeDeviceConnect(object):
    params = {}

    @classmethod
    def get_xml_data(cls, config):
        xml_data = StringIO()
        config.export_xml(xml_data, 1)
        return xml_data.getvalue()
    # end get_xml_data

    @classmethod
    def send_netconf(cls, obj, new_config, default_operation="merge", operation="replace"):
        cls.params = {
                                     "pr_config": obj,
                                     "config": new_config,
                                     "default_operation": default_operation,
                                     "operation": operation
                                   }
        return len(cls.get_xml_data(new_config))
    # end send_netconf

    @classmethod
    def get_xml_config(cls):
        return cls.params.get('config')
    # end get_xml_config

    @classmethod
    def reset(cls):
        cls.params = {}
    # end get_xml_config
# end

def fake_send_netconf(self, new_config, default_operation="merge", operation="replace"):
    return FakeDeviceConnect.send_netconf(self, new_config, default_operation, operation)
