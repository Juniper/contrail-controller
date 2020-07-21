from builtins import object


class FilterModule(object):
    def filters(self):
        return {
            'is_wait_required': self.is_wait_required
        }
    # end filters

    def is_wait_required(self, curr_down_peer_count, health_check_params):

        advanced_params_input = health_check_params.get('Juniper', {})
        health_check_abort = health_check_params.get(
                                'health_check_abort', True)
        bgp_down_peer_check = advanced_params_input.get('bgp', {}).get(
                                'bgp_down_peer_count_check', True)
        bgp_down_peer_count = advanced_params_input.get('bgp', {}).get(
                                'bgp_down_peer_count', 0)
        bgp_peer_state_check = advanced_params_input.get('bgp', {}).get(
                                'bgp_peer_state_check', True)

        #if health_check_abort is false then no wait logic.
        #if health_check_abort is true and down_peer_check is true
        #             - then wait till down_peer_count is in the range
        #if health_check_abort is true and down_peer_check is false and
        #             peer_state is true - then wait for till down_peer_count is zero
        #if health_check_abort is true and down_peer_check is false and peer_state is false
        #             - then no wait logic
        if health_check_abort \
            and not bgp_down_peer_check \
                and not bgp_peer_state_check:
            return False
        if health_check_abort:
            if bgp_down_peer_check:
                if int(curr_down_peer_count) in range(0, bgp_down_peer_count + 1):
                    return False
                else:
                    return True
            if bgp_peer_state_check:
                if int(curr_down_peer_count) != 0:
                    return True
                else:
                    return False
        else:
            return False
    # end is_wait_required
# end FilterModule
