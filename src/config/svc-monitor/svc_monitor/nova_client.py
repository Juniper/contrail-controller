
from novaclient import client as nc
from novaclient import exceptions as nc_exc

class ServiceMonitorNovaClient(object):
    def __init__(self, args):
        self._args = args
        self._nova = {}

    def _novaclient_get(self, proj_name, reauthenticate=False):
        # return cache copy when reauthenticate is not requested
        if not reauthenticate:
            client = self._nova.get(proj_name)
            if client is not None:
                return client
    
        auth_url = self._args.auth_protocol + '://' + self._args.auth_host \
                   + ':' + self._args.auth_port + '/' + self._args.auth_version
        self._nova[proj_name] = nc.Client(
            '2', username=self._args.admin_user, project_id=proj_name,
            api_key=self._args.admin_password,
            region_name=self._args.region_name, service_type='compute',
            auth_url=auth_url, insecure=self._args.auth_insecure)
        return self._nova[proj_name]
    
    def oper(self, resource, oper, proj_name, **kwargs):
        n_client = self._novaclient_get(proj_name)
        try:
            resource_obj = getattr(n_client, resource)
            oper_func = getattr(resource_obj, oper)
            return oper_func(**kwargs)
        except nc_exc.Unauthorized:
            n_client = self._novaclient_get(proj_name, True)
            oper_func = getattr(n_client, oper)
            return oper_func(**kwargs)
        except nc_exc.NotFound:
            self.logger.log(
                "Error: %s %s=%s not found in project %s"
                % (resource, kwargs.keys()[0], kwargs.values()[0], proj_name))
            return None
        except nc_exc.NoUniqueMatch:
            self.logger.log(
                "Error: Multiple %s %s=%s found in project %s"
                % (resource, kwargs.keys()[0], kwargs.values()[0], proj_name))
            return None
        except Exception:
            return None
