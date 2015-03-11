
from novaclient import client as nc
from novaclient import exceptions as nc_exc

class ServiceMonitorNovaClient(object):
    def __init__(self, args, logger):
        self._args = args
        self.logger = logger
        self._nova = {}

    def _novaclient_get(self, proj_name, reauthenticate=False):
        # return cache copy when reauthenticate is not requested
        if not reauthenticate:
            client = self._nova.get(proj_name)
            if client is not None:
                return client

        auth_protocol = (self._args.nova_auth_protocol or
                         self._args.auth_protocol)
        auth_host = (self._args.nova_auth_host or self._args.auth_host)
        auth_port = self._args.nova_auth_port or self._args.auth_port
        auth_version = self._args.nova_auth_version or self._args.auth_version
        admin_user = self._args.nova_admin_user or self._args.admin_user
        admin_password = (self._args.nova_admin_password or
                          self._args.admin_password)
        auth_insecure = (self._args.nova_auth_insecure or
                         self._args.auth_insecure)

        auth_url = auth_protocol + '://' + auth_host \
                   + ':' + auth_port + '/' + auth_version
        self._nova[proj_name] = nc.Client(
            '2', username=admin_user, project_id=proj_name,
            api_key=admin_password,
            region_name=self._args.region_name, service_type='compute',
            auth_url=auth_url, insecure=auth_insecure,
            endpoint_type='internalURL')
        return self._nova[proj_name]

    def _novaclient_exec(self, resource, oper, proj_name, **kwargs):
        n_client = self._novaclient_get(proj_name)
        try:
            resource_obj = getattr(n_client, resource)
            oper_func = getattr(resource_obj, oper)
            if oper == 'get':
                return oper_func(kwargs['id'])
            else:
                return oper_func(**kwargs)
        except nc_exc.NotFound:
            self.logger.log_error(
                "%s %s=%s not found in project %s"
                % (resource, kwargs.keys()[0], kwargs.values()[0], proj_name))
            return None
        except nc_exc.NoUniqueMatch:
            self.logger.log_error(
                "Multiple %s %s=%s found in project %s"
                % (resource, kwargs.keys()[0], kwargs.values()[0], proj_name))
            return None
        except Exception as e:
            self.logger.log_error("nova error %s" % str(e))
            return None

    def oper(self, resource, oper, proj_name, **kwargs):
        try:
            return self._novaclient_exec(resource, oper,
                proj_name, **kwargs)
        except nc_exc.Unauthorized:
            try:
                return self._novaclient_exec(resource, oper,
                    proj_name, **kwargs)
            except nc_exc.Unauthorized:
                self.logger.log_error(
                    "%s %s=%s not authorized in project %s"
                    % (resource, kwargs.keys()[0], kwargs.values()[0], proj_name))
                return None
