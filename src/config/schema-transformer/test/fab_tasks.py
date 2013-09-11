#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
from fabric.api import task, lcd, prefix, execute, local

@task
def setup_venv(build_top = "../../../../build"):
    venv_base = "%s/debug/cfgm/schema-transformer" %(build_top)
    with lcd(venv_base):
        venv_base = local("pwd", capture = True)
        local("virtualenv ut-venv")

    venv_dir = "%s/ut-venv" %(venv_base)
    with lcd(venv_dir):
        with prefix("source %s/bin/activate" %(venv_dir)):
            local("pip install --upgrade ../../common/dist/cfgm_common-0.1dev.tar.gz")
            local("pip install --upgrade ../dist/schema_transformer-0.1dev.tar.gz")
            local("pip install --upgrade ../../svc-monitor/dist/svc_monitor-0.1dev.tar.gz")
            local("pip install --upgrade ../../api-server/dist/vnc_cfg_api_server-0.1dev.tar.gz")
            local("pip install --upgrade ../../../api-lib/dist/vnc_api-0.1dev.tar.gz")
            local("pip install --upgrade ../../../sandesh/library/python/dist/sandesh-0.1dev.tar.gz")
            local("pip install paramiko==1.9.0")
            with lcd("../../../../../third_party/kazoo"):
                local("python setup.py install")
            with lcd("../../../../../third_party/ncclient"):
                local("python setup.py install")

            local("pip install fixtures==0.3.12")
            local("pip install testtools==0.9.32")
            local("pip install flexmock==0.9.7")
            local("pip install python-novaclient==2.13.0")
            # 2.6 requirements
            local("pip install ordereddict")
            local("pip install importlib")

#end setup_venv

@task
def destroy_venv(build_top = "../../../../build"):
    venv_base = "%s/debug/cfgm/schema-transformer" %(build_top)
    venv_dir = "%s/ut-venv" %(venv_base)
    local("rm -rf %s" %(venv_dir))
#end destroy_venv

@task
def run_tests(build_top = "../../../../build"):
    venv_base = "%s/debug/cfgm/schema-transformer" %(build_top)
    venv_dir = "%s/ut-venv" %(venv_base)
    pyver = "%s.%s" %(sys.version_info[0], sys.version_info[1])
    with lcd(venv_dir):
        with prefix("source bin/activate"):
            local("cp ../../../../../src/cfgm/schema-transformer/test/test_service.py lib/python%s/site-packages/schema_transformer/" %(pyver))
            local("which python")
            local("python lib/python%s/site-packages/schema_transformer/test_service.py" %(pyver))
#end run_tests

@task
def setup_and_run(build_top = "../../../../build"):
    execute(setup_venv, build_top)
    execute(run_tests, build_top)
#end setup_and_run
