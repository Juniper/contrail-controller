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
        """
        if device_response:
            re_resp = device_response.get('route-engine-information', {})
            re_details = re_resp.get('route-engine', {})
            if re_details:
                if type(re_details) is dict:
                    return ['re0']
                elif type(re_details) is list:
                    return ['re0', 're1']
        return []
    # end parse_re_response
# end FilterModule


# For UT
if __name__ == "__main__":
    fc = FilterModule()
    device_response = None
    print fc.parse_re_response(device_response)

    device_response = {'route-engine-information':{}}
    print fc.parse_re_response(device_response)

    device_response = {'route-engine-information':{'route-engine': {}}}
    print fc.parse_re_response(device_response)

    device_response = {'route-engine-information': {'route-engine': {
        'slot': 0}}}
    print fc.parse_re_response(device_response)

    device_response = {'route-engine-information': {'route-engine': [{
        'slot': 0}, {'slot': 1}]}}
    print fc.parse_re_response(device_response)