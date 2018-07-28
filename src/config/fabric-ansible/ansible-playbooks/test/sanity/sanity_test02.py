"""
This file contains sanity test for image upgrade workflow
"""
import sys
import hashlib
import config
import re

class FabricAnsibleModule:
    pass

sys.path.append('../..')
sys.modules['ansible.module_utils.fabric_utils'] = __import__('sanity_test02')
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


# pylint: disable=E1101
class SanityTest02(SanityBase):
    """
    Sanity test for image upgrade workflow:
    """

    def __init__(self, cfg):
        SanityBase.__init__(self, cfg, "sanity_test_02")
        self._namespaces = cfg['namespaces']
        self._prouter = cfg['prouter']
        self._prouter_ip = cfg['prouter']['ips'][0]
        self._prouter_password = cfg['prouter']['passwords'][0]
        self._swift_params = cfg['swift']
        self._image_details = cfg['image']
        self._keystone_ip = self._swift_params['keystone_ip']
        self._port = self._swift_params['keystone_port']
        self._image_name = self._image_details['image_name']
        self._image_version = self._image_details['image_version']
        self._image_family_name = self._image_details['image_family_name']
        self._image_vendor_name = self._image_details['image_vendor_name']
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

    # end __init__

    def test_image_upgrade(self):
        # upload image file to swift
        img_uri = None
        try:
            temp_file = 'images/' + self._image_name
            md5 = self._getmd5(temp_file)
            sha1 = self._getsha1(temp_file)
            self._logger.info("Uploading image to swift")
            img_uri = self._fileutilobj.createObjectFile(self._image_name,
                                                         temp_file, md5, sha1)
            if not img_uri:
                self._exit_with_error(
                    "Test failed due to swift uri being None after upload")
        except Exception as ex:
            self._exit_with_error(
                "Could not upload image file to swift: %s" % str(ex))

        # Test image upgrade after device discovery
        try:
            self.cleanup_fabric('fab01')
            fabric = self.create_fabric('fab01', self._prouter['passwords'])
            mgmt_namespace = self._namespaces['management']
            self.add_mgmt_ip_namespace(fabric, mgmt_namespace['name'],
                                       mgmt_namespace['cidrs'])
            self.add_asn_namespace(fabric, self._namespaces['asn'])

            prouters = self.discover_fabric_device(fabric)
            print prouters
            # Create image db object
            image = self.create_image(self._image_name,
                                                         img_uri,
                                                         self._image_version,
                                                         self._image_family_name,
                                                         self._image_vendor_name,
                                                         )
            # Run image upgrade playbook
            self.image_upgrade(image, prouters[0], fabric)
        except Exception as ex:
            self._exit_with_error(
                "Image upgrade test failed due to unexpected error: %s"
                % str(ex))

        # clean image from DB
        self.cleanup_image(self._image_name)
    # end test

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
    SanityTest02(config.load('config/test_config.yml',
                             'config/image_config.yml')).test_image_upgrade()
# end __main__

