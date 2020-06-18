"""
This file contains sanity test for image upgrade workflow
"""
from __future__ import absolute_import
from builtins import str
from builtins import object
import sys
import hashlib
from . import config
import re
from .sanity_base import SanityBase
from . import config
import re

class FabricAnsibleModule(object):
    pass

sys.path.append('../..')
sys.modules['ansible.module_utils.fabric_utils'] = __import__('sanity_test_hitless_upgrade')
from library.swift_fileutil import FileSvcUtil
from .sanity_base import SanityBase

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
        self.upgrade_mode = cfg['upgrade_mode']
        self.advanced_params = cfg['advanced_params']
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
            image_filename = image_obj.get('image_name')
            image_version = image_obj.get('image_version')
            image_family_name = image_obj.get('image_family_name')
            image_vendor_name = image_obj.get('image_vendor_name')
            img_uri = None
            try:
                temp_file = 'images/' + image_name
                md5 = self._getmd5(temp_file)
                sha1 = self._getsha1(temp_file)
                self._logger.info("Uploading image to swift")
                img_uri = self._fileutilobj.createObjectFile(image_filename,
                                                             temp_file, md5, sha1)
                if not img_uri:
                    self._exit_with_error(
                        "Test failed due to swift uri being None after upload")
                image = self.create_image(image_name,
                                          image_filename,
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
            global_device_list = []
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
                    global_device_list.append(phy_obj_uuid)
                    prouter_name_list.append(phy_fq_name[-1])
                upgrade_dict['image_uuid'] = image_uuid
                upgrade_dict['device_list'] = device_list
                final_upgrade_list.append(upgrade_dict)
            fabric_fq_name = ['default-global-system-config', self.fabric]
            fabric = self._api.fabric_read(fq_name=fabric_fq_name)

            self.image_upgrade_maintenance_mode(global_device_list,
                                                final_upgrade_list,
                                                self.advanced_params,
                                                self.upgrade_mode,
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
    SanityTestHitless(config.load('sanity/config/test_config.yml',
                             'sanity/config/image_config.yml')).test_maintenance_mode()

