#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import re
import sys

ExternalEncoding = sys.getdefaultencoding()
Tag_pattern_ = re.compile(r'({.*})?(.*)')


def showIndent(outfile, level, pretty_print=False):
    for i in range(level - 1):
        outfile.write("    ")


def quote_xml(inStr):
    if not inStr:
        return ''
    s1 = (isinstance(inStr, basestring) and inStr or
          '%s' % inStr)
    s1 = s1.replace('&', '&amp;')
    s1 = s1.replace('<', '&lt;')
    s1 = s1.replace('>', '&gt;')
    return s1


def quote_attrib(inStr):
    s1 = (isinstance(inStr, basestring) and inStr or
          '%s' % inStr)
    s1 = s1.replace('&', '&amp;')
    s1 = s1.replace('<', '&lt;')
    s1 = s1.replace('>', '&gt;')
    if '"' in s1:
        if "'" in s1:
            s1 = '"%s"' % s1.replace('"', "&quot;")
        else:
            s1 = "'%s'" % s1
    else:
        s1 = '"%s"' % s1
    return s1


def quote_python(inStr):
    s1 = inStr
    if s1.find("'") == -1:
        if s1.find('\\n') == -1:
            return "'%s'" % s1
        else:
            return "'''%s'''" % s1
    else:
        if s1.find('"') != -1:
            s1 = s1.replace('"', '\\\\"')
        if s1.find('\\n') == -1:
            return '"%s"' % s1
        else:
            return '\"\"\"%s\"\"\"' % s1


class GeneratedsSuper(object):

    def gds_format_string(self, input_data, input_name=''):
        return input_data

    def gds_validate_string(self, input_data, node, input_name=''):
        return input_data

    def gds_format_integer(self, input_data, input_name=''):
        return '%d' % input_data

    def gds_validate_integer(self, input_data, node, input_name=''):
        return input_data

    def gds_format_integer_list(self, input_data, input_name=''):
        return '%s' % input_data

    def gds_validate_integer_list(self, input_data, node, input_name=''):
        values = input_data.split()
        for value in values:
            try:
                fvalue = float(value)
            except (TypeError, ValueError), exp:
                raise_parse_error(node, 'Requires sequence of integers')
        return input_data

    def gds_format_float(self, input_data, input_name=''):
        return '%f' % input_data

    def gds_validate_float(self, input_data, node, input_name=''):
        return input_data

    def gds_format_float_list(self, input_data, input_name=''):
        return '%s' % input_data

    def gds_validate_float_list(self, input_data, node, input_name=''):
        values = input_data.split()
        for value in values:
            try:
                fvalue = float(value)
            except (TypeError, ValueError), exp:
                raise_parse_error(node, 'Requires sequence of floats')
        return input_data

    def gds_format_double(self, input_data, input_name=''):
        return '%e' % input_data

    def gds_validate_double(self, input_data, node, input_name=''):
        return input_data

    def gds_format_double_list(self, input_data, input_name=''):
        return '%s' % input_data

    def gds_validate_double_list(self, input_data, node, input_name=''):
        values = input_data.split()
        for value in values:
            try:
                fvalue = float(value)
            except (TypeError, ValueError), exp:
                raise_parse_error(node, 'Requires sequence of doubles')
        return input_data

    def gds_format_boolean(self, input_data, input_name=''):
        return '%s' % input_data

    def gds_validate_boolean(self, input_data, node, input_name=''):
        return input_data

    def gds_format_boolean_list(self, input_data, input_name=''):
        return '%s' % input_data

    def gds_validate_boolean_list(self, input_data, node, input_name=''):
        values = input_data.split()
        for value in values:
            if value not in ('true', '1', 'false', '0', ):
                raise_parse_error(
                    node,
                    'Requires sequence of booleans'
                    ' ("true", "1", "false", "0")')
        return input_data

    def gds_str_lower(self, instring):
        return instring.lower()

    def get_path_(self, node):
        path_list = []
        self.get_path_list_(node, path_list)
        path_list.reverse()
        path = '/'.join(path_list)
        return path
    Tag_strip_pattern_ = re.compile(r'\{.*\}')

    def get_path_list_(self, node, path_list):
        if node is None:
            return
        tag = GeneratedsSuper.Tag_strip_pattern_.sub('', node.tag)
        if tag:
            path_list.append(tag)
        self.get_path_list_(node.getparent(), path_list)

    def get_class_obj_(self, node, default_class=None):
        class_obj1 = default_class
        if 'xsi' in node.nsmap:
            classname = node.get('{%s}type' % node.nsmap['xsi'])
            if classname is not None:
                names = classname.split(':')
                if len(names) == 2:
                    classname = names[1]
                class_obj2 = globals().get(classname)
                if class_obj2 is not None:
                    class_obj1 = class_obj2
        return class_obj1

    def gds_build_any(self, node, type_name=None):
        return None

    @staticmethod
    def populate_string(name):
        if "mac_address" in name:
            return '00:ca:fe:00:ba:be'
        elif "prefix" in name:
            return '10.5.6.0'
        elif "_network" in name or 'subnet' in name:
            return '10.5.6.0/24'
        elif ("address" in name or 'gateway' in name or
                "router" in name):
            return '10.5.6.253'
        elif "uuid" in name:
            return '0797d558-1d98-479e-a941-a05ae88dc159'
        elif "protocol" in name:
            return 'udp'
        elif "route_target" in name:
            return '192.168.1.42/32''192.168.1.42/32'
        else:
            return 'test-' + name

    @staticmethod
    def populate_unsignedLong(name):
        return 42

    @staticmethod
    def populate_unsignedInt(name):
        return 42

    @staticmethod
    def populate_integer(name):
        if "prefix" in name:
            return 24
        else:
            return 42

    @staticmethod
    def populate_dateTime(name):
        return "2002-05-30T09:30:10.5"

    @staticmethod
    def populate_time(name):
        return "09:30:10Z"

    @staticmethod
    def populate_boolean(name):
        return False
