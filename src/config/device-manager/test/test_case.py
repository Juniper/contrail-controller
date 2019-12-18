from __future__ import absolute_import
import gevent
import sys
from cfgm_common.tests import test_common
sys.path.insert(0, '../../../../build/production/config/device-manager/')
sys.path.insert(0, '../../../../build/debug/config/device-manager/device_manager')
sys.path.insert(0, '../../../../build/debug/config/device-manager/device_api')

from vnc_api.vnc_api import *
from random import randint
from ncclient import manager
from flexmock import flexmock
from .test_dm_utils import FakeDeviceConnect
from .test_dm_utils import FakeJobHandler
from .test_dm_utils import FakeNetconfManager
from .test_dm_utils import fake_netconf_connect
from .test_dm_utils import fake_send_netconf
from .test_dm_utils import fake_job_handler_push
from device_manager import mx_conf, qfx_5k, qfx_10k, overlay_conf, pnf_conf

class DMTestCase(test_common.TestCase):
    GSC = 'default-global-system-config'

    @classmethod
    def setUpClass(cls, extra_config_knobs=None, dm_config_knobs=None):
        extra_config = [
            ('DEFAULTS', 'multi_tenancy', 'False'),
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
        ]
        if extra_config_knobs:
            extra_config = extra_config + extra_config_knobs
        super(DMTestCase, cls).setUpClass(extra_config_knobs=extra_config)
        cls._dm_greenlet = gevent.spawn(test_common.launch_device_manager,
                                        cls.__name__, cls._api_server_ip,
                                        cls._api_server_port, dm_config_knobs)
        test_common.wait_for_device_manager_up()
        cls._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            cls._cluster_id, cls.__name__, cls._api_server_ip,
            cls._api_server_port)
        test_common.wait_for_schema_transformer_up()

    @classmethod
    def tearDownClass(cls):
        test_common.kill_device_manager(cls._dm_greenlet)
        test_common.kill_schema_transformer(cls._st_greenlet)
        super(DMTestCase, cls).tearDownClass()

    def setUp(self, extra_config_knobs=None):
        super(DMTestCase, self).setUp(extra_config_knobs=extra_config_knobs)
        flexmock(manager, connect=fake_netconf_connect)
        setattr(mx_conf.MxConf, 'device_send', fake_send_netconf)
        setattr(qfx_5k.Qfx5kConf, 'device_send', fake_send_netconf)
        setattr(qfx_10k.Qfx10kConf, 'device_send', fake_send_netconf)
        setattr(overlay_conf.OverlayConf, 'device_send',
                fake_job_handler_push)
        setattr(pnf_conf.PnfConf, 'device_send',
                fake_job_handler_push)
        if hasattr(self, 'product'):
            FakeNetconfManager.set_model(self.product)
        return

    def tearDown(self):
        FakeDeviceConnect.reset()
        FakeJobHandler.reset()
        super(DMTestCase, self).tearDown()

    def _get_ip_fabric_ri_obj(self):
        # TODO pick fqname hardcode from common
        rt_inst_obj = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    # end _get_ip_fabric_ri_obj

    def create_sflow_profile(self, name, sample_rate=None,
                             polling_interval=None,
                             adaptive_sample_rate=None,
                             agent_id=None,
                             enbld_intf_type=None,
                             enbld_intf_params=None):

        sflow_params = SflowParameters()
        stats_coll_freq = StatsCollectionFrequency()
        if sample_rate:
            stats_coll_freq.set_sample_rate(sample_rate)
        if polling_interval:
            stats_coll_freq.set_polling_interval(
                polling_interval)

        sflow_params.set_stats_collection_frequency(stats_coll_freq)

        if adaptive_sample_rate:
            sflow_params.set_adaptive_sample_rate(adaptive_sample_rate)

        if agent_id:
            sflow_params.set_agent_id(agent_id)

        if enbld_intf_type:
            sflow_params.set_enabled_interface_type(enbld_intf_type)

        if enbld_intf_params:
            for enbld_intf_param in enbld_intf_params:
                en_intf_name = enbld_intf_param.get('name')
                enbld_intf_param_obj = EnabledInterfaceParams(
                    name=en_intf_name
                )
                enbld_intf_scf = enbld_intf_param.get(
                    'stats_collection_frequency')
                enbld_scf_obj = StatsCollectionFrequency()
                if enbld_intf_scf:
                    if enbld_intf_scf.get('sample_rate'):
                        enbld_scf_obj.set_sample_rate(
                            enbld_intf_scf.get('sample_rate')
                        )
                    if enbld_intf_scf.get('polling_interval'):
                        enbld_scf_obj.set_polling_interval(
                            enbld_intf_scf.get('polling_interval')
                        )
                    enbld_intf_param_obj.set_stats_collection_frequency(
                        enbld_intf_scf
                    )

                sflow_params.add_enabled_interface_params(
                    enbld_intf_param_obj
                )
        sf_profile_obj = SflowProfile(name=name,
                                      sflow_parameters=sflow_params)

        self._vnc_lib.sflow_profile_create(sf_profile_obj)
        return sf_profile_obj
    # end create_sflow_profile

    def create_telemetry_profile(self, name, sflow_obj=None):

        tm_profile_obj = TelemetryProfile(name=name)
        if sflow_obj:
            tm_profile_obj.set_sflow_profile(sflow_obj)
        self._vnc_lib.telemetry_profile_create(tm_profile_obj)

        return tm_profile_obj
    # end create_telemetry_profile

    def create_storm_control_profile(self, name, bw_percent, traffic_type, actions, recovery_timeout=None):

        sc_params_list = StormControlParameters(
            storm_control_actions=actions,
            recovery_timeout=recovery_timeout,
            bandwidth_percent=bw_percent)

        if 'no-broadcast' in traffic_type:
            sc_params_list.set_no_broadcast(True)
        if 'no-multicast' in traffic_type:
            sc_params_list.set_no_multicast(True)
        if 'no-registered-multicast' in traffic_type:
            sc_params_list.set_no_registered_multicast(True)
        if 'no-unknown-unicast' in traffic_type:
            sc_params_list.set_no_unknown_unicast(True)
        if 'no-unregistered-multicast' in traffic_type:
            sc_params_list.set_no_unregistered_multicast(True)

        sc_profile = StormControlProfile(
            name=name,
            storm_control_parameters=sc_params_list
        )

        self._vnc_lib.storm_control_profile_create(sc_profile)
        return sc_profile


    def create_port_profile(self, name, sc_obj=None):
        port_profile = PortProfile(name=name)
        if sc_obj:
            port_profile.set_storm_control_profile(sc_obj)
        self._vnc_lib.port_profile_create(port_profile)

        return port_profile

    def create_fabric(self, name):
        fab = Fabric(
            name=name,
            fabric_credentials={
                'device_credential': [{
                    'credential': {
                        'username': 'root', 'password': '123'
                    },
                    'vendor': 'Juniper',
                    'device_family': None
                }]
            }
        )
        fab_uuid = self._vnc_lib.fabric_create(fab)
        return fab_uuid

    def create_router(self, name, mgmt_ip, vendor='juniper', product='mx',
            ignore_pr=False, role=None, ignore_bgp=False, rb_roles=None,
            node_profile=None, physical_role=None, overlay_role=None,
            fabric=None, family='junos'):
        bgp_router, pr = None, None
        if not ignore_bgp:
            bgp_router = BgpRouter(name=name,
                                   display_name=name+"-bgp",
                                   parent_obj=self._get_ip_fabric_ri_obj())
            params = BgpRouterParams()
            params.address = mgmt_ip
            params.identifier = '1.1.1.1'
            params.address_families = AddressFamilies(['route-target',
                'inet-vpn', 'e-vpn', 'inet6-vpn'])
            params.autonomous_system = randint(0, 64512)
            bgp_router.set_bgp_router_parameters(params)
            self._vnc_lib.bgp_router_create(bgp_router)

        if not ignore_pr:
            pr = PhysicalRouter(name=name, display_name=name)
            pr.physical_router_management_ip = mgmt_ip
            pr.physical_router_vendor_name = vendor
            pr.physical_router_product_name = product
            pr.physical_router_device_family = family
            pr.physical_router_vnc_managed = True
            pr.physical_router_underlay_managed = False
            if role:
                pr.physical_router_role = role
            if rb_roles:
                pr.routing_bridging_roles = RoutingBridgingRolesType(rb_roles=rb_roles)
            uc = UserCredentials('user', 'pw')
            pr.set_physical_router_user_credentials(uc)
            if not ignore_bgp:
                pr.set_bgp_router(bgp_router)
            if physical_role:
                pr.set_physical_role(physical_role)
            if overlay_role:
                pr.set_overlay_role(overlay_role)
            if fabric:
                pr.set_fabric(fabric)
            if node_profile:
                pr.set_node_profile(node_profile)
            self._vnc_lib.physical_router_create(pr)

        return bgp_router, pr
    # end create_router

    def delete_routers(self, bgp_router=None, pr=None):
        if pr:
            self._vnc_lib.physical_router_delete(fq_name=pr.get_fq_name())
        if bgp_router:
            self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())
        return

    @test_common.retries(5, hook=test_common.retry_exc_handler)
    def wait_for_routers_delete(self, bgp_fq=None, pr_fq=None):
        found = False
        if bgp_fq:
            try:
                self._vnc_lib.bgp_router_read(fq_name=bgp_fq)
                found = True
            except NoIdError:
                pass
        if pr_fq:
            try:
                self._vnc_lib.physical_router_read(fq_name=pr_fq)
                found = True
            except NoIdError:
                pass
        self.assertFalse(found)
        return

#end DMTestCase

