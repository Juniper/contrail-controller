class FilterModule(object):
    def filters(self):
        return {
            'parse_re_response':
                self.parse_re_response
        }
    # end filters

    # sample response:  {"route-engine-information": {"route-engine":[]}}
    @staticmethod
    def parse_re_response(device_response):
        """
        :param device_response:
        :return: returns the list of re's
        """
        re_resp = device_response['route-engine-information'] if \
            'route-engine-information' in device_response else {}
        re_details = re_resp['route-engine'] if 'route-engine' in re_resp \
            else None

        if re_details:
            if type(re_details) is dict:
                return ['re0']
            elif type(re_details) is list:
                return ['re0', 're1']
        return []
    # end parse_re_response
# end FilterModule
