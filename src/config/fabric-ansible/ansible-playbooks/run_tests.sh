#!/bin/bash
build_top=${build_top:-$(pwd)/../../..}
tools_path=${tools_path:-$(pwd)/../../common/tests/}
root_path=${root_path:-$(pwd)}
fabric_module_dir=$root_path/ansible/lib/ansible/modules/network/fabric
module_test_dir=$root_path/ansible/test/units/modules/network/fabric
module_utils_dir=$root_path/ansible/lib/ansible/module_utils

installvenvopts=
installvenvopts="${installvenvopts} --find-links ${build_top}/config/common/dist/"
installvenvopts="${installvenvopts} --find-links ${build_top}/api-lib/dist/"
installvenvopts="${installvenvopts} --find-links ${build_top}/config/api-server/dist/"
installvenvopts="${installvenvopts} --find-links ${build_top}/config/schema-transformer/dist/"
installvenvopts="${installvenvopts} --find-links ${build_top}/tools/sandesh/library/python/dist/"
installvenvopts="${installvenvopts} --find-links ${build_top}/sandesh/common/dist/"
installvenvopts="${installvenvopts} --find-links ${build_top}/config/fabric-ansible/dist/"

echo "Downlaod ansible 2.4.3 ...: $root_path"
cd $root_path
[ ! -d ansible ] && git clone https://github.com/ansible/ansible.git
cd ansible
git checkout tags/v2.4.3.0-1

echo "Copy over fabric ansible modules and unit tests ..."
cd $root_path
mkdir -p $fabric_module_dir
cp -r library/* $fabric_module_dir/
mkdir -p $module_test_dir
cp -r test/units/* $module_test_dir/
cp module_utils/* $module_utils_dir/

echo "Create virtualevn .venv ..."
cd $root_path
if [ ! -d .venv ]; then
  virtualenv .venv
  source .venv/bin/activate && pip install --upgrade pip
  source .venv/bin/activate && pip install pylint
  source .venv/bin/activate && pip install $installvenvopts -r requirements.txt
  source .venv/bin/activate && pip install $installvenvopts -r test-requirements.txt
  source .venv/bin/activate && pip install -r ansible/requirements.txt
  source .venv/bin/activate && pip install -r ansible/test/runner/requirements/units.txt
fi

echo "Run pylint to analyze code and find bugs ..."
cd $root_path
source .venv/bin/activate && source ansible/hacking/env-setup
for code in $(find . -name "*.py"|grep -v ".venv"|grep -v "./ansible"); do
  echo "============ Analyze $code ========================================="
  pylint $code
done

echo "Run unit tests ..."
source .venv/bin/activate && source ansible/hacking/env-setup && ansible-test units --python 2.7 junos_facts
source .venv/bin/activate && source ansible/hacking/env-setup && ansible-test units --python 2.7 swift_fileutil
source .venv/bin/activate && source ansible/hacking/env-setup && ansible-test units --python 2.7 vnc_db_mod
