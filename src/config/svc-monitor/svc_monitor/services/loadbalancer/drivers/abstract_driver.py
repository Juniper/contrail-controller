import abc


class ContrailLoadBalancerAbstractDriver(object):
    """Abstract lbaas driver that expose ~same API as lbaas plugin.

    The configuration elements (Vip,Member,etc) are the dicts that
    are returned to the tenant.
    Get operations are not part of the API - it will be handled
    by the lbaas plugin.
    """
    __metaclass__ = abc.ABCMeta

    @abc.abstractmethod
    def create_vip(self, vip):
        """A real driver would invoke a call to his backend
        and set the Vip status to ACTIVE/ERROR according
        to the backend call result
        self.plugin.update_status(Vip, vip["id"], constants.ACTIVE)
        """
        pass

    @abc.abstractmethod
    def update_vip(self, old_vip, vip):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Vip, id, constants.ACTIVE)
        """
        pass

    @abc.abstractmethod
    def delete_vip(self, vip):
        """A real driver would invoke a call to his backend
        and try to delete the Vip.
        if the deletion was successfull, delete the record from the database.
        if the deletion has failed, set the Vip status to ERROR.
        """
        pass

    @abc.abstractmethod
    def create_pool(self, pool):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Pool, pool["id"],
                                  constants.ACTIVE)
        """
        pass

    @abc.abstractmethod
    def update_pool(self, old_pool, pool):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Pool,
                                  pool["id"], constants.ACTIVE)
        """
        pass

    @abc.abstractmethod
    def delete_pool(self, pool):
        """Driver can call the code below in order to delete the pool.
        self.plugin._delete_db_pool(pool["id"])
        or set the status to ERROR if deletion failed
        """
        pass

    @abc.abstractmethod
    def stats(self, pool_id):
        pass

    @abc.abstractmethod
    def create_member(self, member):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Member, member["id"],
                                   constants.ACTIVE)
        """
        pass

    @abc.abstractmethod
    def update_member(self, old_member, member):
        """Driver may call the code below in order to update the status.
        self.plugin.update_status(Member,
                                  member["id"], constants.ACTIVE)
        """
        pass

    @abc.abstractmethod
    def delete_member(self, member):
        pass

    @abc.abstractmethod
    def update_health_monitor(self, 
                              old_health_monitor,
                              health_monitor,
                              pool_id):
        pass

    @abc.abstractmethod
    def create_pool_health_monitor(self, 
                                   health_monitor,
                                   pool_id):
        """Driver may call the code below in order to update the status.
        self.plugin.update_pool_health_monitor(
                                               health_monitor["id"],
                                               pool_id,
                                               constants.ACTIVE)
        """
        pass

    @abc.abstractmethod
    def delete_pool_health_monitor(self, health_monitor, pool_id):
        pass
