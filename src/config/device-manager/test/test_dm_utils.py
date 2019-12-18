#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from future import standard_library
standard_library.install_aliases()
from builtins import object
import sys
if sys.version_info[0] < 3:
    from io import BytesIO as StringIO
else:
    from io import StringIO as StringIO
from lxml import etree
from cfgm_common.tests.test_utils import stub


class FakeNetconfManager(object):
    model = 'mx480'
    version = '14.2R6'

    def __init__(self, host, *args, **kwargs):
        self.host = host
        self.connected = True
        self.configs = []

    def __enter__(self):
        return self
    __exit__ = stub

    def edit_config(self, target, config, test_option, default_operation):
        self.configs = [config]

    def close_session(self):
        self.host = None
        self.connected = True
        self.configs = []
        FakeDeviceConnect.reset()

    def get_config(self, source='running'):
        model = FakeNetconfManager.model
        version = FakeNetconfManager.version
        self.data_xml = '<?xml version="1.0" encoding="UTF-8"?><rpc-reply message-id="urn:uuid:3bb07fe2-755d-11e7-a499-525400f3f0a5">\n<data>\n<configuration commit-seconds="1501173210" commit-localtime="2017-07-27 09:33:30 PDT" commit-user="root">\n<version>17.2-20170228_dev_common.1</version>\n<groups>\n<name>re0</name>\n<system>\n<host-name>access_re</host-name>\n<backup-router>\n<address>10.49.175.254</address>\n</backup-router>\n</system>\n<interfaces>\n<interface>\n<name>fxp0</name>\n<unit>\n<name>0</name>\n<family>\n<inet>\n<address>\n<name>10.49.167.121/20</name>\n</address>\n</inet>\n</family>\n</unit>\n</interface>\n</interfaces>\n</groups>\n<groups>\n<name>re1</name>\n<system>\n<host-name>access_re1</host-name>\n<backup-router>\n<address>10.49.175.254</address>\n</backup-router>\n</system>\n<interfaces>\n<interface>\n<name>fxp0</name>\n<unit>\n<name>0</name>\n<family>\n<inet>\n<address>\n<name>10.49.167.120/20</name>\n</address>\n</inet>\n</family>\n</unit>\n</interface>\n</interfaces>\n</groups>\n<groups>\n<name>global</name>\n<system>\n<domain-name>englab.juniper.net</domain-name>\n<domain-search>englab.juniper.net</domain-search>\n<domain-search>juniper.net</domain-search>\n<domain-search>jnpr.net</domain-search>\n<time-zone>America/Los_Angeles</time-zone>\n<undocumented><debugger-on-break/></undocumented>\n<undocumented><dump-on-panic>\n</dump-on-panic></undocumented>\n<authentication-order>password</authentication-order>\n<authentication-order>radius</authentication-order>\n<authentication-order>tacplus</authentication-order>\n<root-authentication>\n<encrypted-password>$1$ZUlES4dp$OUwWo1g7cLoV/aMWpHUnC/</encrypted-password>\n</root-authentication>\n<name-server>\n<name>10.49.0.4</name>\n</name-server>\n<name-server>\n<name>10.49.0.37</name>\n</name-server>\n<radius-server>\n<name>10.48.144.16</name>\n<secret>$9$iHfz9Cu0BRQznCApIRSreWxNVw2GjkKM4JGimP</secret>\n</radius-server>\n<radius-server>\n<name>10.48.144.17</name>\n<secret>$9$iHfz9Cu0BRQznCApIRSreWxNVw2GjkKM4JGimP</secret>\n</radius-server>\n<login>\n<class>\n<name>wheel</name>\n<permissions>snmp</permissions>\n</class>\n<user>\n<name>regress</name>\n<uid>928</uid>\n<class>superuser</class>\n<undocumented><shell>csh</shell></undocumented>\n<authentication>\n<encrypted-password>$1$kPU..$w.4FGRAGanJ8U4Yq6sbj7.</encrypted-password>'
        return self

    @classmethod
    def set_model(cls, model):
        cls.model = model

    def rpc(self, ele):
        model = FakeNetconfManager.model
        version = FakeNetconfManager.version
        if self.host == '199.199.199.199': #unsupported netconf device ip
            version = 'Unknown'
            model = 'X-Model'
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
                </rpc-reply>'%(version, model, model, version)
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
        config = new_config.get_configuration().get_groups()
        cls.params = {
            "pr_config": obj,
            "config": config,
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


class FakeJobHandler(object):
    params = {}
    dev_params = {}

    @classmethod
    def send(cls, plugin, job_template, job_input, is_delete, retry):
        cls.params = {
            'plugin': plugin,
            'job_template':  job_template,
            'job_input':  job_input,
            'is_delete': is_delete,
            'retry': retry
        }
        dev_name = job_input.get('device_abstract_config', {}).get('system', {}).get('name')
        if dev_name:
            cls.dev_params[dev_name] = {
                'plugin': plugin,
                'job_template':  job_template,
                'job_input':  job_input,
                'is_delete': is_delete,
                'retry': retry
            }
    # end push

    @classmethod
    def get_job_input(cls):
        return cls.params.get('job_input')
    # end get_job_input

    @classmethod
    def get_dev_job_input(cls, dev_name):
        return cls.dev_params.get(dev_name, {}).get('job_input')
    # end get_job_input

    @classmethod
    def reset(cls):
        cls.params = {}
        cls.dev_params = {}
    # end reset
# end FakeJobHandler


def fake_job_handler_push(plugin, job_template, job_input, is_delete, retry):
    return FakeJobHandler.send(plugin, job_template, job_input, is_delete,
                               retry)
# end fake_job_handler_push
