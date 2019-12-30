from __future__ import (absolute_import, division)
__metaclass__ = type

from ansible.errors import AnsibleError
from ansible.plugins.lookup import LookupBase
from ansible.utils.listify import listify_lookup_plugin_terms
from lxml import etree

# https://gist.github.com/andyjsharp/501f79e8c56577d07fa77f040939714e#file-xmlfile-py
#
# Version using lxml and xpath
# forked from https://gist.github.com/danieldbower/7b34c45ad5e39576e2e5
#
# Version for Ansible 2.0
# forked from https://gist.github.com/benbramley/xmlfile.py
# See Ben's page for usage and documentation
#
# You can find some documentation for the syntax available for xpath at
# https://docs.python.org/2/library/xml.etree.elementtree.html#xpath-support
class LookupModule(LookupBase):

    def tostr(self, node):

        if isinstance(node, etree._Element):
            if len(node.getchildren()) == 0:
                return node.text
            return etree.tostring(node)
        return str(node)

    def read_xml(self, filename, dflt=None, xpath=None):

        try:
            xmlreader = etree.parse(filename)
#            nodes = xmlreader.findall(xpath)
            nodes = xmlreader.xpath(xpath)
            values = [self.tostr(node) for node in nodes]
            return values

        except Exception, e:
            raise AnsibleError("xmlfile: %s" % str(e))

        return dflt

    def run(self, terms, variables, **kwargs):

        basedir = self._loader.get_basedir()

        terms = listify_lookup_plugin_terms(terms, templar=self._templar, loader=self._loader)

        if isinstance(terms, basestring):
            terms = [ terms ]

        ret = []
        for term in terms:
            params = term.split()
            key = params[0]

            paramvals = {
                'file' : 'ansible.xml',
                'default' : None,
                'xpath' : None,
            }

            # parameters specified?
            try:
                for param in params:
                    name, value = param.split('=', 1)
                    assert(name in paramvals)
                    paramvals[name] = value
            except (ValueError, AssertionError), e:
                raise AnsibleError(e)

            path = self._loader.path_dwim_relative(basedir, 'files', paramvals['file'])

            var = self.read_xml(path, paramvals['default'], paramvals['xpath'])

            if var is not None:
                if type(var) is list:
                    for v in var:
                        ret.append(v)
                else:
                    ret.append(var)
        return ret
