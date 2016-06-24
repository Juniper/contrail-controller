import logging
import inspect
import os

class CustomAttr(object):
    """This type handles non-flat data-types like
       int, str, bool.
    """
    def __init__(self, key, value):
        self._value = value
        self._key =  key

    def validate(self):
        pass

    def post_validation(self):
        pass

class CustomAttrTlsContainer(CustomAttr):
    def __init__(self, key, value):
        super(CustomAttrTlsContainer, self).__init__(key, value)

    def validate(self):
        return True

    def post_validation(self):
        return self._value

def validate_custom_attributes(custom_attributes_dict, section,
                               custom_attributes):
    section_dict = {}
    if custom_attributes and section in custom_attributes_dict:
        for key, value in custom_attributes.iteritems():
            if key in custom_attributes_dict[section]:
                #Sanitize the value
                try:
                    type_attr = custom_attributes_dict[section][key]['type']
                    limits = custom_attributes_dict[section][key]['limits']
                    if type_attr == 'int':
                        value = int(value)
                        if value in range(limits[0], limits[1]):
                            section_dict.update({key:value})
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                    elif type_attr == 'str':
                        if len(value) in range(limits[0], limits[1]):
                            section_dict.update({key:value})
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                    elif type_attr == 'bool':
                        if value in limits:
                            if value == 'True':
                                value = ''
                            elif value == 'False':
                                value = 'no '
                            section_dict.update({key:value})
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                    elif inspect.isclass(eval(type_attr)):
                        new_custom_attr = eval(type_attr)(key, value)
                        if new_custom_attr.validate():
                            value = new_custom_attr.post_validation()
                            section_dict.update({key:value})
                        else:
                            logging.info("Skipping key: %s, value: %s due to" \
                               "validation failure" % (key, value))
                except Exception as e:
                    logging.error(str(e))
                    continue

    return section_dict
