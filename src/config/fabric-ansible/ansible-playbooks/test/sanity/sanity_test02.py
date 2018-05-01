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
        self._prouter_ip = cfg['prouter']['ips'][0]
        self._prouter_password = cfg['prouter']['passwords'][0]
        self._device_name = 'mxdev'
        self._swift_params = cfg['swift_image']
        self._keystone_ip = self._swift_params['keystone_ip']
        self._port = self._swift_params['keystone_port']
        self._image_name = self._swift_params['image_name']
        self._image_family_name = self._swift_params['image_family_name']
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

        # Test image upgrade
        try:
            # Create image and device db objects
            image_version = self._get_image_version(self._image_name)

            image, device = self.create_image_and_device(self._image_name,
                                                         img_uri, image_version,
                                                         self._image_family_name,
                                                         self._prouter_ip,
                                                         self._prouter_password,
                                                         self._device_name)
            # Run image upgrade playbook
            self.image_upgrade(image, device)
        except Exception as ex:
            self._exit_with_error(
                "Image upgrade test failed due to unexpected error: %s"
                % str(ex))

        # clean device and image from DB
        self.cleanup_image_prouter(self._image_name, self._device_name)
    # end test


    def _get_image_version(self, image_name):
        image_version = None
        img_name = image_name.split("-")
        for i in img_name:
            version_reg = re.search(r'^\d+.+\d+$',i)
            if version_reg:
                image_version = version_reg.group()
                break
        if image_version is None:
            self._exit_with_error(
                "Could not extract image version from filename")

        return image_version

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
                             'config/swift_config.yml')).test_image_upgrade()
# end __main__
