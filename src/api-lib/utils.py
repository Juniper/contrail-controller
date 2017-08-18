import os
import sys
import errno
import logging


AAA_MODE_VALID_VALUES = ['no-auth', 'cloud-admin', 'rbac']
OP_POST = 1
OP_GET = 2
OP_PUT = 3
OP_DELETE = 4


def hdr_client_tenant():
    return 'X-Tenant-Name'
# end hdr_tenant_client


def _obj_serializer_all(obj):
    if hasattr(obj, 'serialize_to_json'):
        return obj.serialize_to_json()
    else:
        return dict((k, v) for k, v in obj.__dict__.iteritems())
# end _obj_serializer_all


def str_to_class(class_name, module_name):
    try:
        return reduce(getattr, class_name.split("."), sys.modules[module_name])
    except Exception as e:
        logger = logging.getLogger(module_name)
        logger.warn("Exception: %s", str(e))
        return None
# end str_to_class


def obj_type_to_vnc_class(obj_type, module_name):
    return str_to_class(CamelCase(obj_type), module_name)
# end obj_type_to_vnc_class


def CamelCase(input):
    words = input.replace('_', '-').split('-')
    name = ''
    for w in words:
        name += w.capitalize()
    return name
# end CamelCase


def getCertKeyCaBundle(bundle, certs):
    if os.path.isfile(bundle):
        # Check if bundle needs to be replaced if
        # constituent files were updated
        bundle_is_stale = False
        bundle_mod_time = os.path.getmtime(bundle)
        for cert in certs:
            if os.path.getmtime(cert) > bundle_mod_time:
                bundle_is_stale = True
                break
        if not bundle_is_stale:
            return bundle

    try:
        os.makedirs(os.path.dirname(bundle))
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise
    with open(bundle, 'w') as ofile:
        for cert in certs:
            with open(cert) as ifile:
                for line in ifile:
                    ofile.write(line)
    os.chmod(bundle, 0o777)
    return bundle
# end CreateCertKeyCaBundle
