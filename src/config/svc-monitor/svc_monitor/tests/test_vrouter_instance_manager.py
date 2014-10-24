import mock
import unittest
import uuid
from vnc_api import vnc_api
from svc_monitor.vrouter_instance_manager import VRouterInstanceManager


class DBObjMatcher(object):
    """
    Object for assert_called_with to check if db object is created properly
    """
    def __init__(self, prefix):
        self.prefix = prefix

    def _has_field(self, name, ob):
        return (self.prefix + name) in ob

    def __eq__(self, other):
        if not(self._has_field("name", other)
               and self._has_field("uuid", other)
               and self._has_field("state", other)
               and self._has_field("vrouter", other)):
            return False
        if other[self.prefix + "vrouter"] == "None":
            return False
        return True


class VRouterInstanceManagerTest(unittest.TestCase):
    VM_UUID = str(uuid.uuid4())
    VR_UUID = str(uuid.uuid4())
    DB_PREFIX = "test"

    MOCKED_VR_BACK_REF = [{
        "uuid": VR_UUID
    }]

    def setUp(self):
        mocked_vnc = mock.MagicMock()
        mocked_vm_ob = mock.MagicMock()
        mocked_vm_ob.get_virtual_router_back_refs\
            .return_value = self.MOCKED_VR_BACK_REF
        mocked_vm_ob.uuid = self.VM_UUID
        mocked_vnc.virtual_machine_read.return_value = mocked_vm_ob

        self.mocked_vm_ob = mocked_vm_ob
        mocked_db = mock.MagicMock()
        mocked_db.get_vm_db_prefix.return_value = self.DB_PREFIX
        self.vrouter_manager = VRouterInstanceManager(
            db=mocked_db, logger=mock.MagicMock(),
            vnc_lib=mocked_vnc, vrouter_scheduler=mock.MagicMock())

    def test_create(self):
        st_obj = vnc_api.ServiceTemplate(name="test-template")
        svc_properties = vnc_api.ServiceTemplateType()
        svc_properties.set_service_virtualization_type('vrouter-instance')
        svc_properties.set_image_name('test')
        svc_properties.set_ordered_interfaces(True)
        if_list = [['management', False], ['left', False], ['right', False]]
        for itf in if_list:
            if_type = vnc_api.ServiceTemplateInterfaceType(shared_ip=itf[1])
            if_type.set_service_interface_type(itf[0])
            svc_properties.add_interface_type(if_type)
        svc_properties.set_vrouter_instance_type("docker")
        st_obj.set_service_template_properties(svc_properties)
        si_obj = vnc_api.ServiceInstance("test2")
        si_prop = vnc_api.ServiceInstanceType(
            left_virtual_network="left", right_virtual_network="right",
            management_virtual_network="management")
        si_prop.set_interface_list(
            [vnc_api.ServiceInstanceInterfaceType(virtual_network="left"),
             vnc_api.ServiceInstanceInterfaceType(virtual_network="right"),
             vnc_api.ServiceInstanceInterfaceType(
                 virtual_network="management")])
        si_prop.set_virtual_router_id(uuid.uuid4())
        si_obj.set_service_instance_properties(si_prop)
        si_obj.set_service_template(st_obj)
        si_obj.uuid = str(uuid.uuid4())
        st_obj.uuid = str(uuid.uuid4())

        self.vrouter_manager.create_service(st_obj, si_obj)
        self.vrouter_manager.db.service_instance_insert.assert_called_with(
            si_obj.get_fq_name_str(), DBObjMatcher(self.DB_PREFIX)
        )

    def test_delete(self):
        mocked_vr = mock.MagicMock()
        mocked_vr.uuid = self.VR_UUID

        self.vrouter_manager._vnc_lib.virtual_router_read.\
            return_value = mocked_vr

        self.vrouter_manager.delete_service(self.VM_UUID)
        self.vrouter_manager._vnc_lib.virtual_machine_delete\
            .assert_called_with(id=self.VM_UUID)
        mocked_vr.del_virtual_machine.assert_called_with(
            self.mocked_vm_ob)