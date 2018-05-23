#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import mock
from mock import patch
import unittest
from cfgm_common.vnc_db import DBBase
from kube_manager.kube import kube_monitor
from vnc_api.vnc_api import *

class Map(dict):
    """
    Facilitates access to dictionary elements as key and attributes.
    """
    def __init__(self, *args, **kwargs):
        super(Map, self).__init__(*args, **kwargs)
        for arg in args:
            if isinstance(arg, dict):
                for k, v in arg.iteritems():
                    self[k] = v

        if kwargs:
            for k, v in kwargs.iteritems():
                self[k] = v

    def __getattr__(self, attr):
        return self.get(attr)

    def __setattr__(self, key, value):
        self.__setitem__(key, value)

    def __setitem__(self, key, value):
        super(Map, self).__setitem__(key, value)
        self.__dict__.update({key: value})

    def __delattr__(self, item):
        self.__delitem__(item)

    def __delitem__(self, key):
        super(Map, self).__delitem__(key)
        del self.__dict__[key]

class KubeMonitorTest(unittest.TestCase):
    def setUp(self):
        self.args = Map()
        self.args['orchestrator'] = ""
        self.args['token'] = "deadbeef"
        self.args['kubernetes_api_server'] = "127.0.0.1"
        self.args['kube_object_cache'] = "False"
        self.args['kubernetes_api_secure_port'] = 6443
        kube_monitor.KubeMonitor._is_kube_api_server_alive = mock.Mock(return_value=True)
        self.base_url = "https://{0}:{1}".format(
                                self.args.kubernetes_api_server,
                                self.args.kubernetes_api_secure_port)
    # end setUp

    def tearDown(self):
        pass
    # end tearDown

    def test_kube_monitor_api_v1_url_construction(self):

        api_v1_base_url = "{0}/api/v1".format(self.base_url)
        check_v1_url = lambda x: x.get_component_url() == \
                                "/".join([api_v1_base_url, resource_name])

        api_v2_base_url = "{0}/api/v2".format(self.base_url)
        check_v2_url = lambda x: x.get_component_url() == \
                                "/".join([api_v2_base_url, resource_name])

        get_custom_group_url =\
            lambda group, version: "/".join([self.base_url, group, version])
        check_custom_url = lambda x, custom_url: x.get_component_url() == \
                                "/".join([custom_url, resource_name])

        # Validate that, if no explicit version is given, we will default
        # to api/v1.
        resource_name = "namespace"
        namespace_monitor = kube_monitor.KubeMonitor(self.args,
                                                     resource_name=resource_name,
                                                     logger=mock.Mock())
        self.assertTrue(check_v1_url(namespace_monitor))

        # Validate that, if an explicit version is given, we will construct
        # appropriate api version.
        resource_name = "pods"
        pod_monitor = kube_monitor.KubeMonitor(self.args,
                                               resource_name=resource_name,
                                               logger=mock.Mock(),
                                               api_version="v2")
        self.assertTrue(check_v2_url(pod_monitor))

        # Validate that, if an explicit group is given, we will construct
        # url with appropriate group but with version v1.
        custom_netpol_url = get_custom_group_url("networking.k8s.io", "v1")
        resource_name = "networkpolicies"
        netpol_monitor = kube_monitor.KubeMonitor(self.args,
                                               resource_name=resource_name,
                                               logger=mock.Mock(),
                                               api_group="networking.k8s.io")
        self.assertTrue(check_custom_url(netpol_monitor, custom_netpol_url))

        # Validate that, if an explicit group and version given, we will construct
        # url with appropriate group and version.
        custom_netpol_url = get_custom_group_url("networking.k8s.io", "v3")
        resource_name = "networkpolicies"
        netpol_monitor = kube_monitor.KubeMonitor(self.args,
                                               resource_name=resource_name,
                                               logger=mock.Mock(),
                                               api_group="networking.k8s.io",
                                               api_version="v3")
        self.assertTrue(check_custom_url(netpol_monitor, custom_netpol_url))

    # end test_kube_monitor_api_v1_url_construction


    def test_kube_monitor_beta_url_construction(self):
        api_beta1_base_url = "{0}/apis/extensions/v1beta1".format(self.base_url)
        check_beta1_url = lambda x: x.get_component_url() == \
                                "/".join([api_beta1_base_url, resource_name])

        api_beta2_base_url = "{0}/apis/extensions/v1beta2".format(self.base_url)
        check_beta2_url = lambda x: x.get_component_url() == \
                                "/".join([api_beta2_base_url, resource_name])

        # Validate that, if beta api is requested, we will construct beta
        # url, with default version of v1beta1.
        resource_name = "ingresses"
        ingress_monitor = kube_monitor.KubeMonitor(self.args, logger=mock.Mock(),
                                                   beta=True,
                                                   resource_name=resource_name)
        self.assertTrue(check_beta1_url(ingress_monitor))

        # Validate that, if beta api of a specific version is requested, we will
        # construct beta url, with requested version.
        resource_name = "endpoints"
        ep_monitor = kube_monitor.KubeMonitor(self.args, logger=mock.Mock(),
                                                   beta=True, api_version="v1beta2",
                                                   resource_name=resource_name)
        self.assertTrue(check_beta2_url(ep_monitor))

    # end test_kube_monitor_beta_url_construction

#end KubeMonitorTest(unittest.TestCase):
