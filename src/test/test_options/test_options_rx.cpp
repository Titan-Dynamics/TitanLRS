// options_ApplyDoc as an RX build sees it: firmware_options_t and the key mapping both depend on
// TARGET_RX, so this file is built with it defined.
#define TARGET_RX 1

#include <cstring>
#include <unity.h>

#include "options_apply.h"

static firmware_options_t rxDefaults()
{
    firmware_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.domain = 1;
    opts.uart_baud = 420000;
    opts.lock_on_first_connection = true;
    return opts;
}

void test_apply_doc_rx_keys()
{
    firmware_options_t opts = rxDefaults();
    TEST_ASSERT_TRUE(options_ApplyJson(
        R"({"uid":[6,5,4,3,2,1],"domain":2,"flash-discriminator":42,"rcvr-uart-baud":115200,)"
        R"("lock-on-first-connection":false,"wifi-on-interval":30})",
        opts, 8));
    const uint8_t uid[6] = {6, 5, 4, 3, 2, 1};
    TEST_ASSERT_TRUE(opts.hasUID);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(uid, opts.uid, 6);
    TEST_ASSERT_EQUAL(2, opts.domain);
    TEST_ASSERT_EQUAL_UINT32(42, opts.flash_discriminator);
    TEST_ASSERT_EQUAL_UINT32(115200, opts.uart_baud);
    TEST_ASSERT_FALSE(opts.lock_on_first_connection);
    TEST_ASSERT_EQUAL(0, opts.wifi_auto_on_interval);
}

void test_apply_doc_rx_ignores_tx_keys()
{
    firmware_options_t opts = rxDefaults();
    const firmware_options_t before = opts;
    TEST_ASSERT_TRUE(options_ApplyJson(R"({"tlm-interval":500,"fan-runtime":3,"unlock-higher-power":true,"airport-uart-baud":9600})", opts, 8));
    TEST_ASSERT_EQUAL_MEMORY(&before, &opts, sizeof(opts));
}
