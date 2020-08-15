import re
from setuptools import setup, find_packages


def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return list(filter(bool, map(lambda y: c.sub('', y).strip(), lines)))


setup(
    name='nodemgr',
    version='0.1dev',
    packages=find_packages(),
    package_data={'': ['*.html', '*.css', '*.xml', '*.yml']},
    zip_safe=False,
    long_description="Nodemgr Implementation",

    test_suite='nodemgr.tests',

    install_requires=requirements('requirements.txt'),
    tests_require=requirements('test-requirements.txt'),

    entry_points={
        'console_scripts': [
            'contrail-nodemgr = nodemgr.main:main',
        ],
    },
)
