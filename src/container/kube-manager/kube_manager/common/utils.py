#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Kube Manager Utility methods and datastructures.
"""

from ast import literal_eval


def get_dict_from_dict_string(dict_string, dict_string_kind):
    """Given a dictionary string of a kind, return its dictionary object.
    """
    if not dict_string:
        err_msg = dict_string_kind + " dict string is not specified/invalid"
        raise Exception(err_msg)

    result_dict = literal_eval(dict_string)
    if not result_dict:
        err_msg = dict_string_kind + " dict string [%s] is invalid." %\
            (dict_string)
        raise Exception(err_msg)

    return result_dict


class CustomNetwork(object):
    """Defines the keywords and format of a custom network specification.
    """
    domain_name_key = 'domain'
    project_name_key = 'project'
    vn_name_key = 'name'

    @classmethod
    def NetworkFQName(cls):
        """Get a fq-network name key specification.
        """
        return [cls.domain_name_key, cls.project_name_key, cls.vn_name_key]


def get_vn_fq_name_from_dict_string(vn_dict_string):
    """Given a dictionary string representing VN, return the VN's FQ-name.

    If enough information is not available to construct the FQ-name, an
    exception is retured.
    """
    fq_name_key = CustomNetwork.NetworkFQName()
    vn_fq_name = []

    vn = get_dict_from_dict_string(vn_dict_string, "virtual_network")
    for key in fq_name_key:
        value = vn.get(key, None)
        if value:
            vn_fq_name.append(value)
        else:
            err_msg = "[%s] not specified in VN dict string. "\
                "Unable to construct VN name from dict string." %\
                (key)
            raise Exception(err_msg)
    return vn_fq_name


def get_domain_name_from_vn_dict_string(vn_dict_string):
    """Given a dict-string representing VN FQ-name, return the VN's domain.
    """
    vn = get_dict_from_dict_string(vn_dict_string, "virtual_network")
    domain_name = vn.get(CustomNetwork.domain_name_key, None)
    if not domain_name:
        err_msg = ("Domain name not found in virtual network dict "
                   "string(%s)." % (vn_dict_string))
        raise Exception(err_msg)
    return domain_name


def get_project_name_from_vn_dict_string(vn_dict_string):
    """Given a dict-string representing VN FQ-name, return the VN's project.
    """
    vn = get_dict_from_dict_string(vn_dict_string, "virtual_network")
    project_name = vn.get(CustomNetwork.project_name_key, None)
    if not project_name:
        err_msg = "Project not found in virtual network dict string(%s)." %\
            (vn_dict_string)
        raise Exception(err_msg)
    return project_name


def get_vn_name_from_vn_dict_string(vn_dict_string):
    """Given a dict-string representing VN FQ-name, return the name of VN.
    """
    vn = get_dict_from_dict_string(vn_dict_string, "virtual_network")
    vn_name = vn.get(CustomNetwork.vn_name_key, None)
    if not vn_name:
        err_msg = "VN name not found in virtual network dict string(%s)." %\
            (vn_dict_string)
        raise Exception(err_msg)
    return vn_name


class FipPoolFQName(object):
    """Defines the keywords and format of a Fip-Pool FQName specification.
    """
    domain_name_key = 'domain'
    project_name_key = 'project'
    vn_name_key = 'network'
    fip_pool_name_key = 'name'

    @classmethod
    def fip_pool_fq_name_key(cls):
        """Get a fq-network name key specification.
        """
        return [cls.domain_name_key, cls.project_name_key, cls.vn_name_key,
                cls.fip_pool_name_key]


def get_fip_pool_fq_name_from_dict_string(fip_pool_dict_string):
    """Given a dictionary string representing a fip-pool, return its FQ-name.

    If enough information is not available to construct the FQ-name, an
    exception is retured.
    """
    fip_pool_fq_name = []
    fq_name_key = FipPoolFQName.fip_pool_fq_name_key()
    fip_pool = get_dict_from_dict_string(fip_pool_dict_string, "fip_pool")
    for key in fq_name_key:
        value = fip_pool.get(key, None)
        if value:
            fip_pool_fq_name.append(value)
        else:
            err_msg = "[%s] not specified in fip-pool dict string [%s]. "\
                "Unable to construct fip-pool name." %\
                (key, fip_pool_dict_string)
            raise Exception(err_msg)
    return fip_pool_fq_name


class ProjectFQName(object):
    """Defines the keywords and format of a Project FQName specification.
    """
    domain_name_key = 'domain'
    name = 'project'

    @classmethod
    def project_fq_name_key(cls):
        """Get a fq-network name key specification.
        """
        return [cls.domain_name_key, cls.project_name_key]


def get_domain_name_from_project_dict_string(proj_dict_string):
    """Given a dict-string representing project FQ-name,return its domain.
    """
    proj = get_dict_from_dict_string(proj_dict_string, "project")
    domain_name = proj.get(ProjectFQName.domain_name_key, None)
    return domain_name


def get_project_name_from_project_dict_string(proj_dict_string):
    """Given a dict-string representing project FQ-name, return its name.
    """
    proj = get_dict_from_dict_string(proj_dict_string, "project")
    project_name = proj.get(ProjectFQName.name, None)
    return project_name
