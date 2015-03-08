#include "router.c"

#include "tests_utils.h"
#include "tests_router_utils.h"

void
test_target(const char *target)
{
    struct lgtd_lifx_packet_power_state payload = {
        .power = LGTD_LIFX_POWER_ON
    };
    bool ok = lgtd_router_send(target, LGTD_LIFX_SET_POWER_STATE, &payload);
    if (ok) {
        lgtd_errx(1, "router_send didn't return false for an unknown device");
    }
    if (lgtd_tests_gw_pkt_queue_size) {
        lgtd_errx(1, "no packets should have been sent");
    }
}

int
main(void)
{
    lgtd_lifx_wire_load_packet_infos_map();

    struct lgtd_lifx_gateway *gw_1 = lgtd_tests_insert_mock_gateway(1);
    lgtd_tests_insert_mock_bulb(gw_1, 1);
    struct lgtd_lifx_gateway *gw_2 = lgtd_tests_insert_mock_gateway(2);
    lgtd_tests_insert_mock_bulb(gw_2, 2);

    test_target("4");
    test_target("-1");
    test_target("blabla");
    test_target("**");
    test_target("ffffffffffffffffffffffffff");
    test_target("");

    return 0;
}