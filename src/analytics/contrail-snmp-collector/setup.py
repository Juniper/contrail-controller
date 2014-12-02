import re, setuptools

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    c = re.compile(r'\s*#.*')
    return filter(bool, map(lambda y: c.sub('', y).strip(), lines))

setuptools.setup(
        name='contrail-snmp-collector',
        version='0.1.0',
        description='contrail snmp collector package.',
        long_description=open('README.txt').read(),
        packages=setuptools.find_packages(),

        # metadata
        author="OpenContrail",
        author_email="dev@lists.opencontrail.org",
        license="Apache Software License",
        url="http://www.opencontrail.org/",

        install_requires=requirements('requirements.txt'),

        test_suite='contrail_snmp_scanner.tests',
        tests_require=requirements('test-requirements.txt'),
        entry_points = {
          'console_scripts' : [
            'contrail-snmp-collector = contrail_snmp_collector.main:emain',
            ],
        },
    )

