#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import mock
import unittest
from kube_manager.kube import kube_monitor


class Map(dict):
    """
    Facilitates access to dictionary elements as key and attributes.
    """
    def __init__(self, *args, **kwargs):
        super(Map, self).__init__(*args, **kwargs)
        for arg in args:
            if isinstance(arg, dict):
                for k, v in arg.items():
                    self[k] = v

        if kwargs:
            for k, v in kwargs.items():
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

    def get_k8s_url_resource(self, k8s_api_resources, resource_type):
        if resource_type in k8s_api_resources:
            k8s_url_resource =\
                k8s_api_resources[resource_type]['k8s_url_resource']
        else:
            k8s_url_resource = resource_type
        return k8s_url_resource
    # end get_k8s_url_resource

    def test_kube_monitor_api_v1_url_construction(self):
        api_v1_base_url = "{0}/api/v1".format(self.base_url)
        api_v2_base_url = "{0}/api/v2".format(self.base_url)

        def check_v1_url(x):
            return x.get_component_url() == "/".join([api_v1_base_url, k8s_url_resource])

        def check_v2_url(x):
            return x.get_component_url() == "/".join([api_v2_base_url, k8s_url_resource])

        def get_custom_group_url(group, version):
            return "/".join([self.base_url, group, version])

        def check_custom_url(x, custom_url):
            return x.get_component_url() == "/".join([custom_url, k8s_url_resource])

        # Validate that, if no explicit api_group and api_version is given, we will
        # use the default api_group and api_version for the known resource_type.
        resource_type = "namespace"
        namespace_monitor = kube_monitor.KubeMonitor(self.args,
                                                     resource_type=resource_type,
                                                     logger=mock.Mock())
        k8s_url_resource =\
            self.get_k8s_url_resource(namespace_monitor.k8s_api_resources, resource_type)
        self.assertTrue(check_v1_url(namespace_monitor))

        # Validate that, even an explicit api_version is given, we will construct
        # appropriate api_version for the known resource_type.
        resource_type = "pod"
        pod_monitor = kube_monitor.KubeMonitor(self.args,
                                               resource_type=resource_type,
                                               logger=mock.Mock(),
                                               api_version="v2")
        k8s_url_resource =\
            self.get_k8s_url_resource(pod_monitor.k8s_api_resources, resource_type)
        self.assertFalse(check_v2_url(pod_monitor))

        # Validate that, even an explicit api_group is given, we will construct
        # url with appropriate api_group for the known resource_type
        custom_url = get_custom_group_url("apis/extensions", "v1beta1")
        resource_type = "networkpolicy"
        netpol_monitor = kube_monitor.KubeMonitor(
            self.args,
            resource_type=resource_type,
            logger=mock.Mock(),
            api_group="networking.k8s.io")
        k8s_url_resource =\
            self.get_k8s_url_resource(netpol_monitor.k8s_api_resources, resource_type)
        self.assertTrue(check_custom_url(netpol_monitor, custom_url))

        # Validate that, if an explicit api_group and api_version given, we will construct
        # url with appropriate api_group and api_version for the unknown resource_type.
        custom_url = get_custom_group_url("networking.k8s.io", "v3")
        resource_type = "testresources"
        test_monitor = kube_monitor.KubeMonitor(
            self.args,
            resource_type=resource_type,
            logger=mock.Mock(),
            api_group="networking.k8s.io",
            api_version="v3")
        k8s_url_resource =\
            self.get_k8s_url_resource(test_monitor.k8s_api_resources, resource_type)
        self.assertTrue(check_custom_url(test_monitor, custom_url))

    # end test_kube_monitor_api_v1_url_construction

    def test_kube_monitor_beta_url_construction(self):
        api_beta1_base_url = "{0}/apis/extensions/v1beta1".format(self.base_url)
        api_beta2_base_url = "{0}/apis/extensions/v1beta2".format(self.base_url)

        def check_beta1_url(x):
            return x.get_component_url() == "/".join([api_beta1_base_url, k8s_url_resource])

        def check_beta2_url(x):
            return x.get_component_url() == "/".join([api_beta2_base_url, k8s_url_resource])

        # Validate that, if beta resource is requested, we will construct beta
        # url, with the right api_group and api_version for known resource_type
        resource_type = "ingress"
        ingress_monitor = kube_monitor.KubeMonitor(self.args, logger=mock.Mock(),
                                                   resource_type=resource_type)
        k8s_url_resource =\
            self.get_k8s_url_resource(ingress_monitor.k8s_api_resources, resource_type)
        self.assertTrue(check_beta1_url(ingress_monitor))

        # Validate that, if beta api of a specific version is requested for unknown
        # resource_type, we will construt beta url with passed api_group and api_version
        resource_type = "testresources"
        test_monitor = kube_monitor.KubeMonitor(self.args, logger=mock.Mock(),
                                                api_group="apis/extensions",
                                                api_version="v1beta2",
                                                resource_type=resource_type)
        k8s_url_resource =\
            self.get_k8s_url_resource(test_monitor.k8s_api_resources, resource_type)
        self.assertTrue(check_beta2_url(test_monitor))

    # end test_kube_monitor_beta_url_construction

# end KubeMonitorTest(unittest.TestCase):
