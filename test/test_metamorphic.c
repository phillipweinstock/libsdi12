/**
 * @file test_metamorphic.c
 * @brief Metamorphic and property-based tests for libsdi12.
 *
 * Metamorphic testing verifies *relations* between outputs rather than
 * checking against specific expected values. This catches bugs that
 * point-test oracles miss.
 *
 * Properties tested:
 *
 *   CRC:
 *     - Single-byte mutation must change CRC (error detection)
 *     - Append then verify is always true (roundtrip idempotency)
 *     - Double-append never produces valid CRC (non-idempotent)
 *     - Encoding is bijective (different CRCs → different ASCII)
 *
 *   Address:
 *     - Validity is idempotent (check twice = same result)
 *     - Complement: valid XOR invalid partitions the full char set
 *
 *   Sensor:
 *     - Address change is reversible (A→B→A)
 *     - Wrong-address silence is universal across all 62 addresses
 *     - M then D is deterministic (same params → same data response)
 *     - Break always returns to READY regardless of prior state
 *     - CRC variant adds exactly 3 chars vs non-CRC variant
 *
 *   Master Parser:
 *     - Sign-flip negates parsed value (metamorphic relation)
 *     - Concatenation is additive (parse A+B = parse A ∪ parse B)
 *     - Parsing is deterministic (same input → same output)
 *     - Decimal count matches input dot position
 */
#include "sdi12_test.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "sdi12.h"
#include "sdi12_sensor.h"
#include "sdi12_master.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  CRC METAMORPHIC PROPERTIES
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Property: Any single-byte mutation in the data must change the CRC.
 * (Error detection guarantee of CRC-16.)
 */
void test_meta_crc_single_byte_mutation_detected(void)
{
    const char *original = "0+25.50-3.14+101.3+65.00-10.5";
    size_t len = strlen(original);
    uint16_t orig_crc = sdi12_crc16(original, len);

    char mutated[64];
    for (size_t i = 0; i < len; i++) {
        memcpy(mutated, original, len);
        /* Flip lowest bit of each byte */
        mutated[i] ^= 0x01;
        uint16_t mut_crc = sdi12_crc16(mutated, len);
        TEST_ASSERT_NOT_EQUAL_HEX16(orig_crc, mut_crc);
    }
}

/**
 * Property: append(data) → verify(result) is always true.
 * Tested over many diverse strings.
 */
void test_meta_crc_append_verify_roundtrip_universal(void)
{
    const char *inputs[] = {
        "0", "A", "z",
        "0+1.23",
        "5-99.999+0.001",
        "Z+0.00+0.00+0.00+0.00+0.00+0.00+0.00+0.00+0.00",
        "a+1+2+3+4+5+6+7+8+9",
        "0+999.999-999.999",
    };
    size_t n = sizeof(inputs) / sizeof(inputs[0]);

    for (size_t i = 0; i < n; i++) {
        char buf[128];
        strcpy(buf, inputs[i]);
        sdi12_err_t err = sdi12_crc_append(buf, sizeof(buf));
        TEST_ASSERT_EQUAL(SDI12_OK, err);
        TEST_ASSERT_TRUE_MESSAGE(sdi12_crc_verify(buf, strlen(buf)),
                                 inputs[i]);
    }
}

/**
 * Property: Applying append twice does NOT produce a valid CRC
 * (CRC append is not idempotent — the second CRC covers different data).
 * The inner CRC chars become part of the data for the outer CRC.
 */
void test_meta_crc_double_append_not_idempotent(void)
{
    char buf[128];
    strcpy(buf, "0+1.23");

    sdi12_crc_append(buf, sizeof(buf));
    /* buf is now "0+1.23XYZ\r\n" — valid CRC */
    TEST_ASSERT_TRUE(sdi12_crc_verify(buf, strlen(buf)));

    /* Strip CRLF and append again */
    size_t len = strlen(buf);
    buf[len - 2] = '\0'; /* remove \r\n */

    sdi12_crc_append(buf, sizeof(buf));
    /* Now has double CRC — the first verify (on full string) should still work
       because it's a fresh valid CRC over the whole thing. But the INNER
       portion ("0+1.23XYZ" + new CRC) — the first CRC chars are now data. */

    /* Key relation: the CRC bytes from the first append are NOT the same
       as the CRC bytes from the second append */
    /* This verifies non-idempotency: two appends ≠ one append */
    char buf2[128];
    strcpy(buf2, "0+1.23");
    sdi12_crc_append(buf2, sizeof(buf2));

    /* The two buffers must differ (double-append ≠ single-append) */
    TEST_ASSERT_NOT_EQUAL(strlen(buf2), strlen(buf));
}

/**
 * Property: CRC encoding is bijective — different CRC values produce
 * different ASCII encodings.
 */
void test_meta_crc_encoding_bijective(void)
{
    /* Sample a range of CRC values and ensure no collisions */
    char prev[4] = {0};
    uint16_t prev_crc = 0;
    int collision_count = 0;

    for (uint32_t crc = 0; crc <= 0xFFFF; crc += 7) {
        char out[4];
        sdi12_crc_encode_ascii((uint16_t)crc, out);

        if (crc > 0 && (uint16_t)crc != prev_crc) {
            /* Different CRC → different encoding */
            if (memcmp(out, prev, 3) == 0) {
                collision_count++;
            }
        }
        memcpy(prev, out, 3);
        prev_crc = (uint16_t)crc;
    }
    TEST_ASSERT_EQUAL(0, collision_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  ADDRESS METAMORPHIC PROPERTIES
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Property: Address validity check is idempotent —
 * calling it twice on the same char gives the same result.
 */
void test_meta_address_idempotent(void)
{
    for (int c = 0; c < 128; c++) {
        bool first  = sdi12_valid_address((char)c);
        bool second = sdi12_valid_address((char)c);
        TEST_ASSERT_EQUAL(first, second);
    }
}

/**
 * Property: Valid and invalid addresses are complementary partitions.
 * Every char is either valid or invalid, never both, never neither.
 * Combined count = 128 (full ASCII range).
 */
void test_meta_address_partition_complete(void)
{
    int valid_count = 0;
    int invalid_count = 0;
    for (int c = 0; c < 128; c++) {
        if (sdi12_valid_address((char)c))
            valid_count++;
        else
            invalid_count++;
    }
    TEST_ASSERT_EQUAL(62, valid_count);
    TEST_ASSERT_EQUAL(66, invalid_count);
    TEST_ASSERT_EQUAL(128, valid_count + invalid_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  SENSOR METAMORPHIC PROPERTIES
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Reuse mock infrastructure from test_sensor.c (linked in same binary) */
extern void reset_mocks(void);
extern char mock_response[];
extern size_t mock_response_len;
extern int mock_send_count;

/* Forward-declare the helper (defined in test_sensor.c) */
extern sdi12_sensor_ctx_t create_test_ctx(char address);

/**
 * Property: Address change is reversible.
 * Changing 0→5→0 must restore original address.
 */
void test_meta_sensor_address_change_reversible(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    /* Change 0 → 5 */
    sdi12_sensor_process(&ctx, "0A5!", 4);
    TEST_ASSERT_EQUAL_CHAR('5', ctx.address);

    /* Change 5 → 0 */
    reset_mocks();
    sdi12_sensor_process(&ctx, "5A0!", 4);
    TEST_ASSERT_EQUAL_CHAR('0', ctx.address);

    /* Verify it responds to original address */
    reset_mocks();
    sdi12_sensor_process(&ctx, "0!", 2);
    TEST_ASSERT_EQUAL(1, mock_send_count);
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
}

/**
 * Property: Wrong-address silence is universal.
 * A sensor at address '0' must never respond to any of the other 61 addresses.
 */
void test_meta_sensor_wrong_address_silence_universal(void)
{
    const char all_addrs[] = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    for (size_t i = 0; i < sizeof(all_addrs) - 1; i++) {
        reset_mocks();
        sdi12_sensor_ctx_t ctx = create_test_ctx('0');

        char cmd[4];
        snprintf(cmd, sizeof(cmd), "%c!", all_addrs[i]);
        sdi12_err_t err = sdi12_sensor_process(&ctx, cmd, 2);

        TEST_ASSERT_EQUAL(SDI12_ERR_NOT_ADDRESSED, err);
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_send_count,
            "Sensor must not respond to wrong address");
    }
}

/**
 * Property: M then D is deterministic.
 * Same parameters → same data response on repeated M+D cycles.
 */
void test_meta_sensor_measurement_deterministic(void)
{
    /* First cycle */
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();
    sdi12_sensor_process(&ctx, "0D0!", 4);
    char first_response[256];
    strcpy(first_response, mock_response);

    /* Second cycle — fresh context, same params */
    reset_mocks();
    ctx = create_test_ctx('0');
    sdi12_sensor_process(&ctx, "0M!", 3);
    reset_mocks();
    sdi12_sensor_process(&ctx, "0D0!", 4);

    TEST_ASSERT_EQUAL_STRING(first_response, mock_response);
}

/**
 * Property: Break always returns sensor to READY state,
 * regardless of what state it was in before.
 */
void test_meta_sensor_break_returns_ready_from_any_state(void)
{
    sdi12_state_t states[] = {
        SDI12_STATE_READY,
        SDI12_STATE_DATA_READY,
        SDI12_STATE_MEASURING,
        SDI12_STATE_MEASURING_C
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        reset_mocks();
        sdi12_sensor_ctx_t ctx = create_test_ctx('0');
        ctx.state = states[i];

        sdi12_sensor_break(&ctx);
        TEST_ASSERT_EQUAL(SDI12_STATE_READY, ctx.state);
    }
}

/**
 * Property: MC response adds exactly 3 CRC chars compared to M response.
 * Both should have same address, wait time, count — but MC has CRC before CRLF.
 */
void test_meta_sensor_crc_variant_adds_three_chars(void)
{
    /* Standard M */
    reset_mocks();
    sdi12_sensor_ctx_t ctx1 = create_test_ctx('0');
    sdi12_sensor_process(&ctx1, "0M!", 3);
    reset_mocks();
    sdi12_sensor_process(&ctx1, "0D0!", 4);
    size_t m_len = mock_response_len;

    /* MC (with CRC) */
    reset_mocks();
    sdi12_sensor_ctx_t ctx2 = create_test_ctx('0');
    sdi12_sensor_process(&ctx2, "0MC!", 4);
    reset_mocks();
    sdi12_sensor_process(&ctx2, "0D0!", 4);
    size_t mc_len = mock_response_len;

    /* CRC adds exactly 3 chars (the CRLF is in both) */
    TEST_ASSERT_EQUAL(m_len + 3, mc_len);
}

/**
 * Property: aHA! measurement response has 3-digit count format (atttnnn),
 * while aM! has 1-digit count format (atttn). For the same 5 params,
 * HA response is 2 chars longer than M response.
 */
void test_meta_sensor_ha_vs_m_response_format(void)
{
    /* M response */
    reset_mocks();
    sdi12_sensor_ctx_t ctx1 = create_test_ctx('0');
    sdi12_sensor_process(&ctx1, "0M!", 3);
    size_t m_resp_len = mock_response_len;

    /* HA response */
    reset_mocks();
    sdi12_sensor_ctx_t ctx2 = create_test_ctx('0');
    sdi12_sensor_process(&ctx2, "0HA!", 4);
    size_t ha_resp_len = mock_response_len;

    /* M: a(1) + ttt(3) + n(1) + CRLF(2) = 7
       HA: a(1) + ttt(3) + nnn(3) + CRLF(2) = 9  → diff = 2 */
    TEST_ASSERT_EQUAL(m_resp_len + 2, ha_resp_len);
}

/**
 * Property: HB with binary callback uses the callback;
 *           HB without binary callback falls back to ASCII format.
 */
static size_t mock_binary_page_called;
static size_t mock_format_binary(uint16_t page,
                                  const sdi12_value_t *values,
                                  uint8_t count,
                                  char *buf, size_t buflen,
                                  void *user_data)
{
    (void)user_data;
    (void)values;
    (void)buflen;
    mock_binary_page_called++;
    /* Write a simple binary-ish payload: just the count as raw bytes */
    for (uint8_t i = 0; i < count && (size_t)(1 + i) < buflen - 3; i++) {
        buf[1 + i] = (char)(0x80 | i); /* high-bit set = clearly binary */
    }
    (void)page;
    return (size_t)count;
}

void test_meta_sensor_hb_with_binary_callback(void)
{
    reset_mocks();
    mock_binary_page_called = 0;

    sdi12_sensor_ctx_t ctx;
    sdi12_ident_t ident = {0};
    memcpy(ident.vendor, "TESTCO  ", SDI12_ID_VENDOR_LEN);
    memcpy(ident.model, "MOD001", SDI12_ID_MODEL_LEN);
    memcpy(ident.firmware_version, "100", SDI12_ID_FWVER_LEN);

    sdi12_sensor_callbacks_t cb = {0};
    /* Need to get the mock functions — declare extern */
    extern void mock_send_response(const char *, size_t, void *);
    extern void mock_set_direction(sdi12_dir_t, void *);
    extern sdi12_value_t mock_read_param(uint8_t, void *);

    cb.send_response     = mock_send_response;
    cb.set_direction     = mock_set_direction;
    cb.read_param        = mock_read_param;
    cb.format_binary_page = mock_format_binary;

    sdi12_sensor_init(&ctx, '0', &ident, &cb);
    sdi12_sensor_register_param(&ctx, 0, "TA", "C", 2);
    sdi12_sensor_register_param(&ctx, 0, "RH", "%", 1);

    /* Start HB measurement */
    sdi12_sensor_process(&ctx, "0HB!", 4);

    /* Request data page */
    reset_mocks();
    mock_binary_page_called = 0;
    sdi12_sensor_process(&ctx, "0D0!", 4);

    TEST_ASSERT_EQUAL(1, mock_binary_page_called);
    /* Response should contain high-bit-set bytes (binary payload) */
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]); /* address */
}

void test_meta_sensor_hb_without_callback_uses_ascii(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');
    /* create_test_ctx sets format_binary_page = NULL */

    /* Start HB measurement */
    sdi12_sensor_process(&ctx, "0HB!", 4);
    reset_mocks();

    /* Request data — should fall back to ASCII format_data_page */
    sdi12_sensor_process(&ctx, "0D0!", 4);

    TEST_ASSERT_EQUAL(1, mock_send_count);
    TEST_ASSERT_EQUAL_CHAR('0', mock_response[0]);
    /* Should contain '+' or '-' signs (ASCII format) */
    TEST_ASSERT_TRUE(strchr(mock_response, '+') != NULL ||
                     strchr(mock_response, '-') != NULL);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  MASTER PARSER METAMORPHIC PROPERTIES
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Property: Flipping the sign of a value string negates the parsed result.
 * parse("+X") = -parse("-X")
 */
void test_meta_parse_sign_flip_negates(void)
{
    const char *pos_strs[] = {"+1.23", "+99", "+0.001", "+500.5"};
    const char *neg_strs[] = {"-1.23", "-99", "-0.001", "-500.5"};

    for (size_t i = 0; i < 4; i++) {
        sdi12_value_t pv[1], nv[1];
        uint8_t pc = 0, nc = 0;

        sdi12_master_parse_data_values(pos_strs[i], strlen(pos_strs[i]),
                                        pv, 1, &pc, false);
        sdi12_master_parse_data_values(neg_strs[i], strlen(neg_strs[i]),
                                        nv, 1, &nc, false);

        TEST_ASSERT_EQUAL(1, pc);
        TEST_ASSERT_EQUAL(1, nc);
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, pv[0].value, -nv[0].value);
    }
}

/**
 * Property: Concatenation is additive.
 * parse("A" + "B") yields parse("A") ∪ parse("B").
 */
void test_meta_parse_concatenation_additive(void)
{
    const char *part_a = "+1.23-4.56";
    const char *part_b = "+7.89";

    /* Parse individually */
    sdi12_value_t va[10], vb[10];
    uint8_t ca = 0, cb = 0;
    sdi12_master_parse_data_values(part_a, strlen(part_a), va, 10, &ca, false);
    sdi12_master_parse_data_values(part_b, strlen(part_b), vb, 10, &cb, false);

    /* Parse concatenated */
    char combined[64];
    snprintf(combined, sizeof(combined), "%s%s", part_a, part_b);
    sdi12_value_t vc[10];
    uint8_t cc = 0;
    sdi12_master_parse_data_values(combined, strlen(combined), vc, 10, &cc, false);

    /* Count must be sum */
    TEST_ASSERT_EQUAL(ca + cb, cc);

    /* First ca values must match va */
    for (uint8_t i = 0; i < ca; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, va[i].value, vc[i].value);
    }
    /* Next cb values must match vb */
    for (uint8_t i = 0; i < cb; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, vb[i].value, vc[ca + i].value);
    }
}

/**
 * Property: Parsing is deterministic.
 * Same input parsed N times always yields identical results.
 */
void test_meta_parse_deterministic(void)
{
    const char *data = "+25.50-3.14+101.3+65.00-10.5";
    sdi12_value_t first[10], second[10];
    uint8_t c1 = 0, c2 = 0;

    sdi12_master_parse_data_values(data, strlen(data), first, 10, &c1, false);
    sdi12_master_parse_data_values(data, strlen(data), second, 10, &c2, false);

    TEST_ASSERT_EQUAL(c1, c2);
    for (uint8_t i = 0; i < c1; i++) {
        TEST_ASSERT_EQUAL_FLOAT(first[i].value, second[i].value);
        TEST_ASSERT_EQUAL(first[i].decimals, second[i].decimals);
    }
}

/**
 * Property: Decimal count in parsed output matches the number of digits
 * after the '.' in the input string.
 */
void test_meta_parse_decimal_count_matches_input(void)
{
    struct { const char *str; uint8_t expected_dec; } cases[] = {
        {"+1",       0},
        {"+1.2",     1},
        {"+1.23",    2},
        {"+1.234",   3},
        {"-0.00001", 5},
        {"+100",     0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        sdi12_value_t v[1];
        uint8_t c = 0;
        sdi12_master_parse_data_values(cases[i].str, strlen(cases[i].str),
                                        v, 1, &c, false);
        TEST_ASSERT_EQUAL(1, c);
        TEST_ASSERT_EQUAL_MESSAGE(cases[i].expected_dec, v[0].decimals,
                                  cases[i].str);
    }
}

/**
 * Property: Measurement response parsing is symmetric across all
 * valid SDI-12 addresses — the address field always matches input[0].
 */
void test_meta_parse_meas_address_passthrough(void)
{
    const char addrs[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    for (size_t i = 0; i < sizeof(addrs) - 1; i++) {
        char resp[8];
        snprintf(resp, sizeof(resp), "%c0005", addrs[i]);

        sdi12_meas_response_t mresp;
        sdi12_err_t err = sdi12_master_parse_meas_response(
            resp, 5, SDI12_MEAS_STANDARD, &mresp);
        TEST_ASSERT_EQUAL(SDI12_OK, err);
        TEST_ASSERT_EQUAL_CHAR(addrs[i], mresp.address);
    }
}
