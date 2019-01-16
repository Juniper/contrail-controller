"""
This file contains sanity test for image upgrade workflow
"""
import sys
import hashlib
import config
import re
from sanity_base import SanityBase
import config
import re

class FabricAnsibleModule:
    pass

sys.path.append('../..')
sys.modules['ansible.module_utils.fabric_utils'] = __import__('sanity_test_hitless_upgrade')
from library.swift_fileutil import FileSvcUtil
from sanity_base import SanityBase

class SwiftFileUtil(FileSvcUtil):

    def createObjectFile(self, filename, tempfilepath, md5, sha1):
        try:
            meta = self.getObjectFileMeta(filename)
            if meta is not None and meta['etag'] == md5:
                file_uri = self.getobjectFileUri(filename)
                return file_uri
        except Exception:
            pass

        with open(tempfilepath, 'rb') as f:
            self.swift_conn.put_object(container=self.container_name,
                                       obj=filename, contents=f,
                                       chunk_size=65536,
                                       headers={'X-Object-Meta-md5': md5,
                                                'X-Object-Meta-sha1': sha1})
        # get object meta data to verify
        meta = self.getObjectFileMeta(filename)
        file_uri = None
        if meta is not None and meta['etag'] == md5:
            file_uri = self.getobjectFileUri(filename)
        return file_uri

    # Delete the file from the container
    def deleteObjectFile(self, filename):
        self.swift_conn.delete_object(container=self.container_name,
                                      obj=filename)

    def getObjectFileMeta(self, filename):
        return self.swift_conn.head_object(container=self.container_name,
                                           obj=filename)

class SanityTestHitless(SanityBase):

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, 'sanity_test_hitless_upgrade')
        self._namespaces = cfg['namespaces']
        self._prouter = cfg['prouter']
        self._swift_params = cfg['swift']
        self._keystone_ip = self._swift_params['keystone_ip']
        self._port = self._swift_params['keystone_port']
        self._image_details = cfg['images']
        self._image_upgrade_list = cfg['image_upgrade_list']
        self.fabric = cfg['fabric']
        self._auth_url = 'http://' + self._keystone_ip + ':' + str(self._port) \
                         + '/v3'
        try:
            self._logger.debug("Connecting to swift")
            self._fileutilobj = SwiftFileUtil("", self._auth_url,
                                              self._api_server['username'],
                                              self._api_server['password'],
                                              self._api_server['tenant'],
                                              self._swift_params[
                                                  'auth_version'],
                                              self._swift_params[
                                                  'container_name'],
                                              self._swift_params[
                                                  'temp_url_key'],
                                              self._swift_params[
                                                  'temp_url_key_2'],
                                              self._swift_params[
                                                  'connection_retry_count'],
                                              self._swift_params[
                                                  'chosen_temp_url_key']
                                              )
        except Exception as e:
            self._exit_with_error(
                "Test failed due to exception while connecting to swift: %s" %
                str(e))

    # Create and upload images. Test maintenance mode.
    def test_maintenance_mode(self):
        for image_obj in self._image_details:
            image_name = image_obj.get('image_name')
            image_version = image_obj.get('image_version')
            image_family_name = image_obj.get('image_family_name')
            image_vendor_name = image_obj.get('image_vendor_name')
            img_uri = None
            try:
                temp_file = 'images/' + image_name
                md5 = self._getmd5(temp_file)
                sha1 = self._getsha1(temp_file)
                self._logger.info("Uploading image to swift")
                img_uri = self._fileutilobj.createObjectFile(image_name,
                                                             temp_file, md5, sha1)
                if not img_uri:
                    self._exit_with_error(
                        "Test failed due to swift uri being None after upload")
                image = self.create_image(image_name,
                                          img_uri,
                                          image_version,
                                          image_family_name,
                                          image_vendor_name,
                                          )
            except Exception as ex:
                self._exit_with_error(
                    "Could not upload image file to swift: %s" % str(ex))

        # Test hitless image upgrade.
        try:
            final_upgrade_list = []
            prouter_name_list = []
            for item in self._image_upgrade_list:
                upgrade_dict = {}
                device_list = []
                image_fq_name = ['default-global-system-config', item.get('image')]
                image_uuid = self._api.device_image_read(fq_name=image_fq_name).uuid
                devices = item.get('device_list')
                for phy_obj in devices:
                    phy_fq_name = ['default-global-system-config', phy_obj]
                    phy_obj_uuid = self._api.physical_router_read(
                        fq_name=phy_fq_name).uuid
                    device_list.append(phy_obj_uuid)
                    prouter_name_list.append(phy_fq_name[-1])
                upgrade_dict['image_uuid'] = image_uuid
                upgrade_dict['device_list'] = device_list
                final_upgrade_list.append(upgrade_dict)
            advanced_params = {
                "bulk_device_upgrade_count": 4,
                "Juniper": {
                    "bgp": {
                        "bgp_flap_count": 4,
                        "bgp_flap_count_check": True,
                        "bgp_down_peer_count": 0,
                        "bgp_down_peer_count_check": True,
                        "bgp_peer_state_check": True
                    },
                    "alarm": {
                        "system_alarm_check": True,
                        "chassis_alarm_check": True
                    },
                    "interface": {
                        "interface_error_check": True,
                        "interface_drop_count_check": True,
                        "interface_carrier_transition_count_check": True
                    },
                    "routing_engine": {
                        "routing_engine_cpu_idle": 60,
                        "routing_engine_cpu_idle_check": True
                    },
                    "fpc": {
                        "fpc_cpu_5min_avg": 50,
                        "fpc_cpu_5min_avg_check": True,
                        "fpc_memory_heap_util": 45,
                        "fpc_memory_heap_util_check": True
                    },
                    "active_route_count_check": True,
                    "l2_total_mac_count_check": True,
                    "storm_control_flag_check": True
                }
            }
            fabric_fq_name = ['default-global-system-config', self.fabric]
            fabric = self._api.fabric_read(fq_name=fabric_fq_name)
            upgrade_mode = "upgrade"

            self.image_upgrade_maintenance_mode(final_upgrade_list,
                                                advanced_params, upgrade_mode,
                                                fabric, prouter_name_list)
        except Exception as ex:
            self._exit_with_error(
                "Image upgrade hitless test failed due to unexpected error: %s"
                % str(ex))

    def _getmd5(self, filepath):
        hasher = hashlib.md5()
        with open(filepath, 'rb') as afile:
            buf = afile.read(65536)
            while len(buf) > 0:
                hasher.update(buf)
                buf = afile.read(65536)
        return hasher.hexdigest()

    def _getsha1(self, filepath):
        hasher = hashlib.sha1()
        with open(filepath, 'rb') as afile:
            buf = afile.read(65536)
            while len(buf) > 0:
                hasher.update(buf)
                buf = afile.read(65536)
        return hasher.hexdigest()

if __name__ == "__main__":
    SanityTestHitless(config.load('config/test_config.yml',
                             'config/image_config.yml')).test_maintenance_mode()

