/**
 * @file test_sensor.c
 * @brief Unit tests for sdi12_sensor.c — sensor (slave) command processing.
 *
 * Uses mock callbacks to capture responses without any hardware.
 *
 * Tests cover:
 *   - Initialization and validation
 *   - Acknowledge (a!, ?!)
 *   - Identification (aI!)
 *   - Standard measurement (aM!, aMC!)
 *   - Concurrent measurement (aC!, aCC!)
 *   - Send data (aD0!)
 *   - Continuous measurement (aR0!–aR9!, aRC0!–aRC9!)
 *   - Change address (aAb!)
 *   - High-volume stubs (aH!)
 *   - Address rejection (wrong address → no response)
 *   - Break signal handling
 *   - Extended commands (aX!)
 *   - Metadata commands (aIM!, aIM_001!)
 *   - Parameter registration limits
 */
#include "sdi12_test.h"
#include <stdio.h>
#include <string.h>
#include "sdi12.h"
#include "sdi12_sensor.h"

/* ── Mock infrastructure ────────────────────────────────────────────────── */

char mock_response[256];
size_t mock_response_len;
char mock_saved_address;
int mock_send_count;

void mock_send_response(const char *data, size_t len, void *user_data)
{
    (void)user_data;
    if (len > sizeof(mock_response) - 1) len = sizeof(mock_response) - 1;
    memcpy(mock_response, data, len);
    mock_response[len] = '\0';
    mock_response_len = len;
    mock_send_count++;
}

sdi12_value_t mock_read_param(uint8_t param_index, void *user_data)
{
    (void)user_data;
    sdi12_value_t val = {0.0f, 0};
    switch (param_index) {
    case 0: val.value = 42.0f;    val.decimals = 0; break;  /* Lux */
    case 1: val.value = 25.50f;   val.decimals = 2; break;  /* Temp */
    case 2: val.value = 101.3f;   val.decimals = 1; break;  /* Pressure */
    case 3: val.value = 65.00f;   val.decimals = 2; break;  /* Humidity */
    case 4: val.value = -10.5f;   val.decimals = 1; break;  /* Negative */
    default: break;
    }
    return val;
}

void mock_save_address(char address, void *user_data)
{
    (void)user_data;
    mock_saved_address = address;
}

char mock_load_address(void *user_data)
{
    (void)user_data;
    return mock_saved_address;
}

void reset_mocks(void)
{
    memset(mock_response, 0, sizeof(mock_response));
    mock_response_len = 0;
    mock_saved_address = '\0';
    mock_send_count = 0;
}

/** Create a standard test context with 5 params in group 0. */
sdi12_sensor_ctx_t create_test_ctx(char address)
{
    sdi12_sensor_ctx_t ctx;

    sdi12_ident_t ident;
    memset(&ident, 0, sizeof(ident));
    memcpy(ident.vendor, "TESTCO  ", SDI12_ID_VENDOR_LEN);
    memcpy(ident.model, "MOD001", SDI12_ID_MODEL_LEN);
    memcpy(ident.firmware_version, "100", SDI12_ID_FWVER_LEN);
    strncpy(ident.serial, "SN123", sizeof(ident.serial) - 1);
    ident.serial[sizeof(ident.serial) - 1] = '\0';

    sdi12_sensor_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_param;
    cb.save_address  = mock_save_address;
    cb.load_address  = mock_load_address;
    cb.user_data     = NULL;

    sdi12_sensor_init(&ctx, address, &ident, &cb);

    sdi12_sensor_register_param(&ctx, 0, "RP", "lux",  0);
    sdi12_sensor_register_param(&ctx, 0, "TA", "C",    2);
    sdi12_sensor_register_param(&ctx, 0, "PA", "Kpa",  1);
    sdi12_sensor_register_param(&ctx, 0, "XR", "%",    2);
    sdi12_sensor_register_param(&ctx, 0, "GR", "Ohm",  1);

    return ctx;
}

/* ── Initialization Tests ───────────────────────────────────────────────── */

void test_sensor_init_ok(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx;

    sdi12_ident_t ident = {0};
    strncpy(ident.vendor, "TEST", sizeof(ident.vendor) - 1);
    strncpy(ident.model, "M1", sizeof(ident.model) - 1);
    strncpy(ident.firmware_version, "1", sizeof(ident.firmware_version) - 1);

    sdi12_sensor_callbacks_t cb = {0};
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_param;

    sdi12_err_t err = sdi12_sensor_init(&ctx, '0', &ident, &cb);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_CHAR('0', ctx.address);
    TEST_ASSERT_EQUAL(SDI12_STATE_READY, ctx.state);
}

void test_sensor_init_null_ctx(void)
{
    sdi12_ident_t ident = {0};
    sdi12_sensor_callbacks_t cb = {0};
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_param;

    TEST_ASSERT_EQUAL(SDI12_ERR_CALLBACK_MISSING,
                      sdi12_sensor_init(NULL, '0', &ident, &cb));
}

void test_sensor_init_invalid_address(void)
{
    sdi12_sensor_ctx_t ctx;
    sdi12_ident_t ident = {0};
    strncpy(ident.vendor, "TEST", sizeof(ident.vendor) - 1);
    strncpy(ident.model, "M1", sizeof(ident.model) - 1);
    strncpy(ident.firmware_version, "1", sizeof(ident.firmware_version) - 1);

    sdi12_sensor_callbacks_t cb = {0};
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_param;

    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_ADDRESS,
                      sdi12_sensor_init(&ctx, '!', &ident, &cb));
}

/* TX/RX switching is the send_response callback's job (see its doc
 * comment) — a sensor needs no separate direction callback. */
void test_sensor_init_needs_only_send_and_read(void)
{
    sdi12_sensor_ctx_t ctx;
    sdi12_ident_t ident = {0};
    strncpy(ident.vendor, "TEST", sizeof(ident.vendor) - 1);
    strncpy(ident.model, "M1", sizeof(ident.model) - 1);
    strncpy(ident.firmware_version, "1", sizeof(ident.firmware_version) - 1);

    sdi12_sensor_callbacks_t cb = {0};
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_param;

    TEST_ASSERT_EQUAL(SDI12_OK, sdi12_sensor_init(&ctx, '0', &ident, &cb));
}

void test_sensor_init_missing_send_callback(void)
{
    sdi12_sensor_ctx_t ctx;
    sdi12_ident_t ident = {0};
    strncpy(ident.vendor, "TEST", sizeof(ident.vendor) - 1);
    strncpy(ident.model, "M1", sizeof(ident.model) - 1);
    strncpy(ident.firmware_version, "1", sizeof(ident.firmware_version) - 1);

    sdi12_sensor_callbacks_t cb = {0};
    /* send_response = NULL (missing) */
    cb.read_param    = mock_read_param;

    TEST_ASSERT_EQUAL(SDI12_ERR_CALLBACK_MISSING,
                      sdi12_sensor_init(&ctx, '0', &ident, &cb));
}

void test_sensor_init_loads_persisted_address(void)
{
    reset_mocks();
    mock_saved_address = '5';

    sdi12_sensor_ctx_t ctx;
    sdi12_ident_t ident = {0};
    strncpy(ident.vendor, "TEST", sizeof(ident.vendor) - 1);
    strncpy(ident.model, "M1", sizeof(ident.model) - 1);
    strncpy(ident.firmware_version, "1", sizeof(ident.firmware_version) - 1);

    sdi12_sensor_callbacks_t cb = {0};
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_param;
    cb.load_address  = mock_load_address;

    sdi12_err_t err = sdi12_sensor_init(&ctx, '0', &ident, &cb);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_CHAR('5', ctx.address); /* loaded from flash */
}

/* ── Acknowledge (a! / ?!) ──────────────────────────────────────────────── */

void test_sensor_acknowledge(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0!", 2);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_STRING("0\r\n", mock_response);
}

void test_sensor_query_address(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('3');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "?!", 2);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_STRING("3\r\n", mock_response);
}

void test_sensor_wrong_address_no_response(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "5!", 2);
    TEST_ASSERT_EQUAL(SDI12_ERR_NOT_ADDRESSED, err);
    TEST_ASSERT_EQUAL(0, mock_send_count);
}

/* ── Identification (aI!) ───────────────────────────────────────────────── */

void test_sensor_identify(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0I!", 3);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* Response: 014TESTCO  MOD001100SN123\r\n */
    /* a=0, version=14, vendor=TESTCO__ (8), model=MOD001 (6), fw=100 (3), serial=SN123 */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
    TEST_ASSERT_EQUAL_CHAR('1', mock_response[1]);
    TEST_ASSERT_EQUAL_CHAR('4', mock_response[2]);
    /* Vendor starts at pos 3, 8 chars */
    TEST_ASSERT_EQUAL_CHAR('T', mock_response[3]);
    /* Check CR/LF at end */
    size_t len = strlen(mock_response);
    TEST_ASSERT_EQUAL_CHAR('\r', mock_response[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', mock_response[len - 1]);
}

/* ── Standard Measurement (aM!) ────────────────────────────────────────── */

void test_sensor_measurement_m(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0M!", 3);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* Sync measurement: 0tttN → 00005 (ttt=000, n=5) */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[1]);
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[2]);
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[3]);
    TEST_ASSERT_EQUAL_CHAR('5', mock_response[4]);

    /* State should be DATA_READY */
    TEST_ASSERT_EQUAL(SDI12_STATE_DATA_READY, ctx.state);
}

void test_sensor_measurement_mc_with_crc(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0MC!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_TRUE(ctx.crc_requested);
    TEST_ASSERT_EQUAL_CHAR('5', mock_response[4]); /* 5 params */
}

void test_sensor_measurement_m_empty_group(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Group 5 has no params registered */
    sdi12_err_t err = sdi12_sensor_process(&ctx, "0M5!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    /* Should respond with 0 values: 00000 */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[4]);
}

/* ── Concurrent Measurement (aC!) ──────────────────────────────────────── */

void test_sensor_measurement_c(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0C!", 3);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* Concurrent: 0tttNN → 000005 → "000005\r\n" */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
    /* nn = 05 (2-digit count) */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[4]);
    TEST_ASSERT_EQUAL_CHAR('5', mock_response[5]);
}

void test_sensor_measurement_cc_with_crc(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0CC!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_TRUE(ctx.crc_requested);
}

/* ── Send Data (aD0!) ───────────────────────────────────────────────────── */

void test_sensor_send_data_after_m(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Trigger measurement first */
    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();

    /* Now request data page 0 */
    sdi12_err_t err = sdi12_sensor_process(&ctx, "0D0!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* Response should start with address '0' and contain sign-prefixed values */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
    /* Should contain '+' or '-' for values */
    TEST_ASSERT_NOT_NULL(strchr(mock_response, '+'));
}

void test_sensor_send_data_with_crc(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Trigger CRC measurement */
    sdi12_sensor_process(&ctx, "0MC!", 4);
    reset_mocks();

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0D0!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* Response should have CRC (3 chars before CRLF) and be verifiable */
    TEST_ASSERT_TRUE(sdi12_crc_verify(mock_response, strlen(mock_response)));
}

void test_sensor_send_data_no_data(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Request data without prior measurement */
    sdi12_err_t err = sdi12_sensor_process(&ctx, "0D0!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    /* Should respond with just address + CRLF */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
}

/* ── Continuous Measurement (aR0!) ──────────────────────────────────────── */

void test_sensor_continuous_r0(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0R0!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* Immediate response with data values */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
    TEST_ASSERT_NOT_NULL(strchr(mock_response, '+'));
}

void test_sensor_continuous_rc0_with_crc(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0RC0!", 5);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* CRC should be valid */
    TEST_ASSERT_TRUE(sdi12_crc_verify(mock_response, strlen(mock_response)));
}

void test_sensor_continuous_empty_group(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Group 9 has no params */
    sdi12_err_t err = sdi12_sensor_process(&ctx, "0R9!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    /* Response should be just address + CRLF */
    TEST_ASSERT_EQUAL_STRING("0\r\n", mock_response);
}

/* ── Change Address (aAb!) ──────────────────────────────────────────────── */

void test_sensor_change_address(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0A5!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* New address in response */
    TEST_ASSERT_EQUAL_STRING("5\r\n", mock_response);
    /* Context updated */
    TEST_ASSERT_EQUAL_CHAR('5', ctx.address);
    /* Persisted via callback */
    TEST_ASSERT_EQUAL_CHAR('5', mock_saved_address);
}

void test_sensor_change_address_invalid(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* '!' is not a valid address */
    sdi12_err_t err = sdi12_sensor_process(&ctx, "0A!!", 4);
    /* The '!' is stripped as terminator so cmd becomes "0A!" with len 3,
       and cmd[2]='!' which is invalid */
    TEST_ASSERT_NOT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_CHAR('0', ctx.address); /* unchanged */
}

/* ── High-Volume Stubs (aH!) ───────────────────────────────────────────── */

void test_sensor_highvol_stub(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0H!", 3);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_STRING("0000000\r\n", mock_response);
}

/* ── Break Handling ─────────────────────────────────────────────────────── */

void test_sensor_break_aborts_measurement(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Start a measurement to get into DATA_READY state */
    sdi12_sensor_process(&ctx, "0M!", 3);
    TEST_ASSERT_EQUAL(SDI12_STATE_DATA_READY, ctx.state);

    /* Break should reset to READY */
    sdi12_sensor_break(&ctx);
    TEST_ASSERT_EQUAL(SDI12_STATE_READY, ctx.state);
}

void test_sensor_break_null_safe(void)
{
    /* Should not crash */
    sdi12_sensor_break(NULL);
}

/* ── Extended Commands (aX!) ────────────────────────────────────────────── */

static sdi12_err_t mock_xcmd_echo(const char *xcmd, char *resp_buf,
                                   size_t resp_buflen, void *user_data)
{
    (void)user_data;
    size_t pos = strlen(resp_buf); /* address already placed */
    snprintf(resp_buf + pos, resp_buflen - pos, "ECHO:%s", xcmd);
    return SDI12_OK;
}

void test_sensor_extended_command(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_sensor_register_xcmd(&ctx, "TEST", mock_xcmd_echo);

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0XTEST!", 7);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    /* Response should contain echoed data */
    TEST_ASSERT_NOT_NULL(strstr(mock_response, "ECHO:TEST"));
}

void test_sensor_extended_no_handler(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* No xcmd registered — should still respond with address */
    sdi12_err_t err = sdi12_sensor_process(&ctx, "0XFOO!", 6);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
}

/* ── Metadata Commands (aIM!, aIM_001!) ─────────────────────────────────── */

void test_sensor_identify_measurement(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0IM!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    /* Should respond with 0tttN format for M capability */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
    TEST_ASSERT_EQUAL_CHAR('5', mock_response[4]); /* 5 params in group 0 */
}

void test_sensor_identify_concurrent(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0IC!", 4);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    /* Should respond with 0tttNN format (2-digit count) */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[4]);
    TEST_ASSERT_EQUAL_CHAR('5', mock_response[5]);
}

void test_sensor_identify_param_metadata(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Request metadata for parameter 1 in M group */
    sdi12_err_t err = sdi12_sensor_process(&ctx, "0IM_001!", 8);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    /* Should contain SHEF code and units: "0,RP,lux;\r\n" */
    TEST_ASSERT_NOT_NULL(strstr(mock_response, "RP"));
    TEST_ASSERT_NOT_NULL(strstr(mock_response, "lux"));
}

void test_sensor_identify_param_metadata_second(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_err_t err = sdi12_sensor_process(&ctx, "0IM_002!", 8);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_NOT_NULL(strstr(mock_response, "TA"));
    TEST_ASSERT_NOT_NULL(strstr(mock_response, "C"));
}

/* ── Parameter Registration ─────────────────────────────────────────────── */

void test_sensor_register_max_params(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx;

    sdi12_ident_t ident = {0};
    strncpy(ident.vendor, "TEST", sizeof(ident.vendor) - 1);
    strncpy(ident.model, "M1", sizeof(ident.model) - 1);
    strncpy(ident.firmware_version, "1", sizeof(ident.firmware_version) - 1);

    sdi12_sensor_callbacks_t cb = {0};
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_param;

    sdi12_sensor_init(&ctx, '0', &ident, &cb);

    /* Register SDI12_MAX_PARAMS params */
    for (int i = 0; i < SDI12_MAX_PARAMS; i++) {
        sdi12_err_t err = sdi12_sensor_register_param(&ctx, 0, "XX", "u", 0);
        TEST_ASSERT_EQUAL(SDI12_OK, err);
    }

    /* Next one should fail */
    sdi12_err_t err = sdi12_sensor_register_param(&ctx, 0, "XX", "u", 0);
    TEST_ASSERT_EQUAL(SDI12_ERR_PARAM_LIMIT, err);
}

void test_sensor_group_count(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    TEST_ASSERT_EQUAL(5, sdi12_sensor_group_count(&ctx, 0));
    TEST_ASSERT_EQUAL(0, sdi12_sensor_group_count(&ctx, 1));
    TEST_ASSERT_EQUAL(0, sdi12_sensor_group_count(&ctx, 9));
}

/* ── Measurement Done (async service request) ───────────────────────────── */

void test_sensor_measurement_done_service_request(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Force into MEASURING state (simulate async) */
    ctx.state = SDI12_STATE_MEASURING;

    sdi12_value_t vals[2] = {
        {1.23f, 2},
        {4.56f, 2}
    };
    sdi12_err_t err = sdi12_sensor_measurement_done(&ctx, vals, 2);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(SDI12_STATE_DATA_READY, ctx.state);
    TEST_ASSERT_TRUE(ctx.data_available);
    /* Service request sent (address + CRLF) */
    TEST_ASSERT_EQUAL(1, mock_send_count);
}

void test_sensor_measurement_done_concurrent_no_sr(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Concurrent: NO service request */
    ctx.state = SDI12_STATE_MEASURING_C;

    sdi12_value_t vals[1] = {{9.99f, 2}};
    sdi12_err_t err = sdi12_sensor_measurement_done(&ctx, vals, 1);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(SDI12_STATE_DATA_READY, ctx.state);
    /* No service request sent */
    TEST_ASSERT_EQUAL(0, mock_send_count);
}

/* ── Negative Value Formatting ──────────────────────────────────────────── */

void test_sensor_negative_value_in_data(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Param 4 returns -10.5 */
    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();

    sdi12_sensor_process(&ctx, "0D0!", 4);
    /* Response should contain '-' for the negative value */
    TEST_ASSERT_NOT_NULL(strchr(mock_response, '-'));
}

/* ── D-Page Pagination ──────────────────────────────────────────────────── */

/** Every parameter formats as "+1nn.00" — exactly 7 chars. */
static sdi12_value_t mock_read_wide(uint8_t param_index, void *user_data)
{
    (void)user_data;
    sdi12_value_t val = { 100.0f + param_index, 2 };
    return val;
}

/** Context with 9 identical-width params: 5 fit on page 0 (5×7 = 35 chars,
 *  the M-mode limit), the remaining 4 must appear on page 1. */
static sdi12_sensor_ctx_t create_paging_ctx(char address)
{
    sdi12_sensor_ctx_t ctx;

    sdi12_ident_t ident;
    memset(&ident, 0, sizeof(ident));
    memcpy(ident.vendor, "TESTCO  ", SDI12_ID_VENDOR_LEN);
    memcpy(ident.model, "MOD001", SDI12_ID_MODEL_LEN);
    memcpy(ident.firmware_version, "100", SDI12_ID_FWVER_LEN);

    sdi12_sensor_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_wide;

    sdi12_sensor_init(&ctx, address, &ident, &cb);

    for (int i = 0; i < 9; i++) {
        sdi12_sensor_register_param(&ctx, 0, "TA", "C", 2);
    }

    return ctx;
}

void test_sensor_data_pagination_d0(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_paging_ctx('0');

    sdi12_sensor_process(&ctx, "0M!", 3);
    TEST_ASSERT_EQUAL_STRING("00009\r\n", mock_response);
    reset_mocks();

    sdi12_sensor_process(&ctx, "0D0!", 4);
    TEST_ASSERT_EQUAL_STRING("0+100.00+101.00+102.00+103.00+104.00\r\n",
                             mock_response);
}

void test_sensor_data_pagination_d1(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_paging_ctx('0');

    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();

    sdi12_sensor_process(&ctx, "0D1!", 4);
    TEST_ASSERT_EQUAL_STRING("0+105.00+106.00+107.00+108.00\r\n",
                             mock_response);
}

void test_sensor_data_pagination_past_end(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_paging_ctx('0');

    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();

    /* All 9 values fit on pages 0-1 — page 2 must be empty */
    sdi12_sensor_process(&ctx, "0D2!", 4);
    TEST_ASSERT_EQUAL_STRING("0\r\n", mock_response);
}

void test_sensor_data_pagination_with_crc(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_paging_ctx('0');

    sdi12_sensor_process(&ctx, "0MC!", 4);
    reset_mocks();

    sdi12_sensor_process(&ctx, "0D0!", 4);
    TEST_ASSERT_TRUE(sdi12_crc_verify(mock_response, strlen(mock_response)));
    TEST_ASSERT_NOT_NULL(strstr(mock_response, "+100.00"));
    reset_mocks();

    sdi12_sensor_process(&ctx, "0D1!", 4);
    TEST_ASSERT_TRUE(sdi12_crc_verify(mock_response, strlen(mock_response)));
    TEST_ASSERT_NOT_NULL(strstr(mock_response, "+105.00"));
}

/* ── Binary Packet Buffer Bounds (aDBn!) ────────────────────────────────── */

/** Greedy binary formatter: fills every byte the library offers. */
static size_t mock_format_binary_fill(uint16_t page,
                                      const sdi12_value_t *values,
                                      uint8_t count,
                                      char *buf, size_t buflen,
                                      void *user_data)
{
    (void)page; (void)values; (void)count; (void)user_data;
    buf[1] = (char)SDI12_BINTYPE_UINT8;
    for (size_t i = 2; i < buflen; i++)
        buf[i] = (char)(i & 0x7F);
    return buflen - 1;  /* type byte + payload, written from buf[1] */
}

void test_sensor_binary_packet_fits_buffer(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.cb.format_binary_page = mock_format_binary_fill;

    sdi12_sensor_process(&ctx, "0HB!", 4);
    reset_mocks();

    sdi12_sensor_process(&ctx, "0DB0!", 5);

    /* Whole packet must fit the sensor's response buffer */
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(ctx.resp_buf), mock_response_len);
    TEST_ASSERT_GREATER_OR_EQUAL(SDI12_BIN_PKT_OVERHEAD, mock_response_len);

    /* Packet self-consistency: addr + size(2 LE) + type + payload + CRC(2 LE) */
    uint16_t psz = (uint8_t)mock_response[1] |
                   ((uint16_t)(uint8_t)mock_response[2] << 8);
    TEST_ASSERT_EQUAL(mock_response_len - SDI12_BIN_PKT_OVERHEAD, psz);

    uint16_t crc = sdi12_crc16(mock_response, 4 + psz);
    uint16_t rx  = (uint8_t)mock_response[4 + psz] |
                   ((uint16_t)(uint8_t)mock_response[4 + psz + 1] << 8);
    TEST_ASSERT_EQUAL_HEX16(crc, rx);
}

/* ── Concurrent Measurement Abort Semantics (§4.4.8) ────────────────────── */

static uint16_t mock_start_meas_5s(uint8_t group, sdi12_meas_type_t type,
                                   void *user_data)
{
    (void)group; (void)type; (void)user_data;
    return 5;
}

void test_sensor_concurrent_survives_break(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.cb.start_measurement = mock_start_meas_5s;

    sdi12_sensor_process(&ctx, "0C!", 3);
    TEST_ASSERT_EQUAL(SDI12_STATE_MEASURING_C, ctx.state);

    /* §4.4.8: a break does NOT abort a concurrent measurement */
    sdi12_sensor_break(&ctx);
    TEST_ASSERT_EQUAL(SDI12_STATE_MEASURING_C, ctx.state);
}

void test_sensor_concurrent_aborted_by_addressed_command(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.cb.start_measurement = mock_start_meas_5s;

    sdi12_sensor_process(&ctx, "0C!", 3);
    TEST_ASSERT_EQUAL(SDI12_STATE_MEASURING_C, ctx.state);

    /* §4.4.7.1: ANY valid command addressed to the sensor — including a
       D command — aborts a concurrent measurement in progress.  The
       response to subsequent D commands is just the address + CRLF. */
    reset_mocks();
    sdi12_sensor_process(&ctx, "0D0!", 4);
    TEST_ASSERT_EQUAL_STRING("0\r\n", mock_response);
    TEST_ASSERT_EQUAL(SDI12_STATE_READY, ctx.state);
    TEST_ASSERT_FALSE(ctx.data_available);
}

void test_sensor_concurrent_not_aborted_by_other_address(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.cb.start_measurement = mock_start_meas_5s;

    sdi12_sensor_process(&ctx, "0C!", 3);
    TEST_ASSERT_EQUAL(SDI12_STATE_MEASURING_C, ctx.state);

    /* Commands to other sensors must not abort (§4.4.7) */
    sdi12_sensor_process(&ctx, "5M!", 3);
    TEST_ASSERT_EQUAL(SDI12_STATE_MEASURING_C, ctx.state);
}

/* ── Identify Measurement CRC Variants (aIMC!, aICC!) ───────────────────── */

void test_sensor_identify_mc_has_crc(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_sensor_process(&ctx, "0IMC!", 5);
    TEST_ASSERT_TRUE(sdi12_crc_verify(mock_response, strlen(mock_response)));
}

void test_sensor_identify_cc_has_crc(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_sensor_process(&ctx, "0ICC!", 5);
    TEST_ASSERT_TRUE(sdi12_crc_verify(mock_response, strlen(mock_response)));
}

/* ── Overlong Value Formatting ──────────────────────────────────────────── */

static sdi12_value_t mock_read_overlong(uint8_t param_index, void *user_data)
{
    (void)param_index; (void)user_data;
    /* "+12345.6777344" — 14 chars, exceeds SDI12_VALUE_MAX_CHARS (9) */
    sdi12_value_t val = { 12345.678f, 7 };
    return val;
}

void test_sensor_overlong_value_truncated_safely(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx;

    sdi12_ident_t ident;
    memset(&ident, 0, sizeof(ident));
    memcpy(ident.vendor, "TESTCO  ", SDI12_ID_VENDOR_LEN);
    memcpy(ident.model, "MOD001", SDI12_ID_MODEL_LEN);
    memcpy(ident.firmware_version, "100", SDI12_ID_FWVER_LEN);

    sdi12_sensor_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.send_response = mock_send_response;
    cb.read_param    = mock_read_overlong;

    sdi12_sensor_init(&ctx, '0', &ident, &cb);
    sdi12_sensor_register_param(&ctx, 0, "XX", "u", 7);

    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();

    sdi12_sensor_process(&ctx, "0D0!", 4);

    /* Value must be clamped to SDI12_VALUE_MAX_CHARS — no garbage bytes */
    size_t len = strlen(mock_response);
    TEST_ASSERT_LESS_OR_EQUAL(1 + SDI12_VALUE_MAX_CHARS + 2, len);
    TEST_ASSERT_TRUE(strncmp(mock_response, "0+12345.67", 10) == 0);
    TEST_ASSERT_EQUAL_CHAR('\r', mock_response[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', mock_response[len - 1]);
}

/* ── measurement_done NULL Guard ────────────────────────────────────────── */

void test_sensor_measurement_done_null_values(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.state = SDI12_STATE_MEASURING;

    sdi12_err_t err = sdi12_sensor_measurement_done(&ctx, NULL, 3);
    TEST_ASSERT_NOT_EQUAL(SDI12_OK, err);
}

/* ── Non-Finite and Out-of-Range Value Formatting ───────────────────────── */

/** What a dead ADC or failed conversion hands the library. */
static sdi12_value_t mock_read_param_nonfinite(uint8_t idx, void *user_data)
{
    (void)user_data;
    sdi12_value_t v = {0.0f, 0};
    switch (idx) {
    case 0: v.value = nanf("");   v.decimals = 0; break;
    case 1: v.value = INFINITY;   v.decimals = 2; break;
    case 2: v.value = -INFINITY;  v.decimals = 1; break;
    case 3: v.value = -0.0f;      v.decimals = 2; break;
    default: break;  /* 0.0, decimals 0 */
    }
    return v;
}

void test_sensor_nonfinite_values_emit_valid_sentinels(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.cb.read_param = mock_read_param_nonfinite;

    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();
    sdi12_sensor_process(&ctx, "0D0!", 4);

    /* Non-finite inputs must saturate to the spec-max sentinel, never
     * leak printf's "inf"/"nan" text or a double sign onto the bus.
     * -0.0 is finite and must format as an ordinary positive zero. */
    TEST_ASSERT_EQUAL_STRING("0+9999999+9999999-9999999+0.00+0\r\n",
                             mock_response);
}

static sdi12_value_t mock_read_param_huge(uint8_t idx, void *user_data)
{
    (void)user_data;
    sdi12_value_t v = {1.0f, 0};
    switch (idx) {
    case 0: v.value = 1e30f;  break;   /* undefined cast to unsigned long */
    case 1: v.value = -1e30f; break;
    default: break;
    }
    return v;
}

void test_sensor_huge_values_clamped_to_spec_max(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.cb.read_param = mock_read_param_huge;

    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();
    sdi12_sensor_process(&ctx, "0D0!", 4);

    /* §4.4.8 Table 11 caps a value at 7 digits — magnitudes beyond
     * 9999999 must clamp, not overflow through an undefined cast. */
    TEST_ASSERT_EQUAL_STRING("0+9999999-9999999+1+1+1\r\n", mock_response);
}

static sdi12_value_t mock_read_param_7dec(uint8_t idx, void *user_data)
{
    (void)user_data;
    sdi12_value_t v = {1.0f, 0};
    if (idx == 0) { v.value = 0.9999999f; v.decimals = 7; }
    return v;
}

void test_sensor_decimals_clamped_to_fit_nine_chars(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    ctx.cb.read_param = mock_read_param_7dec;

    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();
    sdi12_sensor_process(&ctx, "0D0!", 4);

    /* decimals=7 cannot fit the 9-char value cap (§4.4.8 Table 11):
     * sign + digit + point + 7 decimals = 10 chars. The library must
     * clamp to 6 decimals and ROUND — not chop a digit off the end of
     * an over-long string. 0.9999999 rounds to 1.000000; a chop would
     * emit +0.999999. */
    TEST_ASSERT_EQUAL_STRING("0+1.000000+1+1+1+1\r\n", mock_response);
}
