#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
VNC Tags management for kubernetes
"""

from builtins import str

from cfgm_common.exceptions import RefsExistError, NoIdError
from vnc_api.gen.resource_client import (
    Tag
)

from kube_manager.vnc.config_db import TagKM
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config


class VncTags(object):

    def __init__(self):
        self._name = type(self).__name__
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._logger = vnc_kube_config.logger()

        self.proj_obj = None
        proj_name = vnc_kube_config.get_configured_project_name()
        if not vnc_kube_config.is_global_tags() and proj_name:
            proj_fq_name = vnc_kube_config.cluster_project_fq_name()
            try:
                self.proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
            except NoIdError as e:
                self._logger.error(
                    "Unable to locate project object for [%s]"
                    ". Error [%s]" %
                    (proj_fq_name, str(e)))
                self._logger.debug(
                    "All tags for this cluster will created with be in "
                    "global space as project object was not found.")
            else:
                self._logger.debug(
                    "All tags will be created within the scope of project [%s]" %
                    (proj_fq_name))

    def _construct_tag_name(self, type, value):
        return "=".join([type, value])

    def _construct_tag_fq_name(self, type, value):
        if self.proj_obj:
            tag_fq_name = self.proj_obj['fq_name'] + \
                [self._construct_tag_name(type, value)]
        else:
            tag_fq_name = [self._construct_tag_name(type, value)]
        return tag_fq_name

    def create(self, type, value):
        tag = Tag(name="=".join([type, value]),
                  parent_obj=self.proj_obj,
                  tag_type_name=type,
                  tag_value=value)
        try:
            self._vnc_lib.tag_create(tag)
        except RefsExistError:
            # Tags cannot be updated.
            pass

        try:
            tag_obj = self._vnc_lib.tag_read(fq_name=tag.get_fq_name())
        except NoIdError as e:
            self._logger.error(
                "Unable to create tag [%s]. Error [%s]" %
                (tag.get_fq_name(), str(e)))
            return
        # Cache the object in local db.
        TagKM.locate(tag_obj.uuid)

    def delete(self, type, value):
        tag_uuid = TagKM.get_fq_name_to_uuid(
            self._construct_tag_fq_name(type, value))
        try:
            self._vnc_lib.tag_delete(id=tag_uuid)

            TagKM.delete(tag_uuid)
            self._logger.debug("Tag (%s) deleted successfully."
                               % (self._construct_tag_fq_name(type, value)))
        except RefsExistError:
            self._logger.debug("Tag (%s) deletion failed. Tag is in use."
                               % (self._construct_tag_fq_name(type, value)))
        except NoIdError:
            self._logger.debug("Tag delete failed. Tag [%s] not found."
                               % (self._construct_tag_fq_name(type, value)))

        return

    def read(self, type, value):
        try:
            return self._vnc_lib.tag_read(
                fq_name=self._construct_tag_fq_name(type, value))
        except NoIdError:
            self._logger.debug("Tag read failed. Tag [%s] not found."
                               % (self._construct_tag_fq_name(type, value)))
        except Exception as e:
            self._logger.debug("Tag [%s] read failed. Error [%s]."
                               % (self._construct_tag_fq_name(type, value), e.message))
        return None

    def get_tags_fq_name(self, kv_dict, create=False):
        tags = []
        for k, v in kv_dict.items():
            if create:
                self.create(k, v)
            tags.append(self._construct_tag_name(k, v))
        return tags
