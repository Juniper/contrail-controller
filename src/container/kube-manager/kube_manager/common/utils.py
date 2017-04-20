#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Kube Manager Utility methods and datastructures.
"""

from ast import literal_eval

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

def get_vn_dict_from_vn_dict_string(vn_dict_string):
    """Given a dictionary string representing VN, return a dictionary object.
    """
    if not vn_dict_string:
        err_msg = "Virtual network dict string is not specified/invalid"
        raise Exception(err_msg)

    vn = literal_eval(vn_dict_string)
    if not vn:
        err_msg = "Virtual network dict string is invalid."
        raise Exception(err_msg)

    return vn

def get_vn_fq_name_from_dict_string(vn_dict_string):
    """Given a dictionary string representing VN, return the VN's FQ-name.

    If enough information is not available to construct the FQ-name, an
    exception is retured.
    """
    fq_name_key = CustomNetwork.NetworkFQName()
    vn_fq_name = []

    vn = get_vn_dict_from_vn_dict_string(vn_dict_string)
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
    vn = get_vn_dict_from_vn_dict_string(vn_dict_string)
    domain_name = vn.get(CustomNetwork.domain_name_key, None)
    if not domain_name:
        err_msg = "Domain name not found in virtual network dict string(%s)."%\
            (vn_dict_string)
        raise Exception(err_msg)
    return domain_name

def get_project_name_from_vn_dict_string(vn_dict_string):
    """Given a dict-string representing VN FQ-name, return the VN's project.
    """
    vn = get_vn_dict_from_vn_dict_string(vn_dict_string)
    project_name = vn.get(CustomNetwork.project_name_key, None)
    if not project_name:
        err_msg = "Project not found in virtual network dict string(%s)." %\
            (vn_dict_string)
        raise Exception(err_msg)
    return project_name

def get_vn_name_from_vn_dict_string(vn_dict_string):
    """Given a dict-string representing VN FQ-name, return the name of VN.
    """
    vn = get_vn_dict_from_vn_dict_string(vn_dict_string)
    vn_name = vn.get(CustomNetwork.vn_name_key, None)
    if not vn_name:
        err_msg = "VN name not found in virtual network dict string(%s)." %\
            (vn_dict_string)
        raise Exception(err_msg)
    return vn_name

