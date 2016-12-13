#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import importlib
import inspect
import pkgutil
import sys
sys.path.append('../../common/tests')

from test_case import ApiServerIrondTestCase
from test_case import ApiServerTestCase
import tests

IGNORED_MODULES = [
    'test_case',
    'test_with_irond',
    'test_rdbms',
]


def get_all_test_class_for_api_server(package):
    results = []

    if isinstance(package, basestring):
        package = importlib.import_module(package)

    for _, name, is_package in pkgutil.walk_packages(package.__path__):
        if (not is_package
                and (name in IGNORED_MODULES or not name.startswith('test_'))):
            continue
        full_name = package.__name__ + '.' + name
        importlib.import_module(full_name)
        if is_package:
            results.extend(get_all_test_class_for_api_server(full_name))
        for cls_name, cls in inspect.getmembers(sys.modules[full_name],
                                                inspect.isclass):
            if (not cls_name.startswith('Test') or
                    not issubclass(cls, ApiServerTestCase)):
                continue
            results.append(cls)
    return results


for cls in get_all_test_class_for_api_server(tests):
    new_cls_name = '%sWithIrond' % cls.__name__
    new_cls = type(new_cls_name, (cls, ApiServerIrondTestCase), {})
    setattr(importlib.import_module(__name__), new_cls.__name__, new_cls)
    del cls
    del new_cls
