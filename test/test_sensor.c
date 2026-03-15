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
sdi12_dir_t mock_direction;
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

void mock_set_direction(sdi12_dir_t dir, void *user_data)
{
    (void)user_data;
    mock_direction = dir;
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
    mock_direction = SDI12_DIR_RX;
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
    cb.set_direction = mock_set_direction;
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
    cb.set_direction = mock_set_direction;
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
    cb.set_direction = mock_set_direction;
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
    cb.set_direction = mock_set_direction;
    cb.read_param    = mock_read_param;

    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_ADDRESS,
                      sdi12_sensor_init(&ctx, '!', &ident, &cb));
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
    cb.set_direction = mock_set_direction;
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
    cb.set_direction = mock_set_direction;
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
    cb.set_direction = mock_set_direction;
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
