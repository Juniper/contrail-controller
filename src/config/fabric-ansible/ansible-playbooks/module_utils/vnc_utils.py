from vnc_api.vnc_api import VncApi
import vnc_api
from inflection import camelize
from job_manager.job_utils import JobVncApi


class VncUtils(object):

    @staticmethod
    def _init_vnc_api(job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
              'auth_token': '0B02D162-180F-4452-96D0-E9FCAAFC4378',
              'api_server_host': ["10.87.74.204", "10.87.70.216", "10.66.43.21"]
            }
        :return: VncApi
        """
        return JobVncApi.vnc_init(job_ctx)
    # end _init_vnc_api

    @staticmethod
    def _get_vnc_cls(object_type):
        cls_name = camelize(object_type)
        return getattr(vnc_api.gen.resource_client, cls_name, None)
