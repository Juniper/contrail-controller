from vnc_api.vnc_api import VncApi
import vnc_api
from inflection import camelize

class VncUtils(object):

    @staticmethod
    def _init_vnc_api(auth_token):
        return VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                      auth_token=auth_token)
    @staticmethod
    def _get_vnc_cls(object_type):
        cls_name = camelize(object_type)
        return getattr(vnc_api.gen.resource_client, cls_name, None)

