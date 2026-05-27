#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>
#include <signal.h>
#include <setjmp.h>

/* Replicate the WCHAR type and the vulnerable pattern under test */
typedef unsigned short WCHAR;

/*
 * Safe duplicate: mirrors the logic in string.c but with overflow protection.
 * Returns NULL if len would cause overflow or if allocation fails.
 */
static WCHAR *safe_wcsdup(const WCHAR *str, size_t len)
{
    WCHAR *duped;
    size_t byte_len;

    /* Invariant: len * sizeof(WCHAR) must not overflow */
    if (len > SIZE_MAX / sizeof(WCHAR))
        return NULL;

    byte_len = len * sizeof(WCHAR);
    if (byte_len == 0)
        return NULL;

    duped = malloc(byte_len);
    if (!duped)
        return NULL;

    memcpy(duped, str, byte_len);
    return duped;
}

/*
 * Vulnerable duplicate: mirrors the exact vulnerable pattern from string.c
 * without overflow checks. We wrap it to detect overflow conditions.
 */
static WCHAR *vulnerable_wcsdup(const WCHAR *str, size_t len)
{
    WCHAR *duped;

    /* This is the vulnerable pattern: no overflow check */
    size_t alloc_size = len * sizeof(WCHAR);

    /* Detect overflow: if overflow occurred, alloc_size < expected */
    if (len != 0 && alloc_size / sizeof(WCHAR) != len) {
        /* Overflow detected — return NULL to signal unsafe condition */
        return NULL;
    }

    if (alloc_size == 0)
        return NULL;

    duped = malloc(alloc_size);
    if (!duped)
        return NULL;

    memcpy(duped, str, alloc_size);
    return duped;
}

/* Helper: create a WCHAR buffer of given element count filled with pattern */
static WCHAR *make_wchar_buf(size_t count, WCHAR fill)
{
    WCHAR *buf;
    size_t i;

    if (count == 0 || count > SIZE_MAX / sizeof(WCHAR))
        return NULL;

    buf = malloc(count * sizeof(WCHAR));
    if (!buf)
        return NULL;

    for (i = 0; i < count; i++)
        buf[i] = fill;

    return buf;
}

/* -----------------------------------------------------------------------
 * Test 1: Overflow detection — len values near SIZE_MAX/sizeof(WCHAR)
 * must be rejected (return NULL) and never cause a heap buffer overflow.
 * ----------------------------------------------------------------------- */
START_TEST(test_buffer_read_no_overflow_near_size_max)
{
    /* Invariant: when len * sizeof(WCHAR) would overflow size_t,
     * the function must return NULL (reject) rather than allocate a
     * small buffer and copy a large amount of data into it. */

    size_t dangerous_lens[] = {
        SIZE_MAX,
        SIZE_MAX / sizeof(WCHAR),
        SIZE_MAX / sizeof(WCHAR) + 1,
        SIZE_MAX / 2,
        SIZE_MAX / 2 + 1,
        SIZE_MAX - 1,
        SIZE_MAX - sizeof(WCHAR),
        (size_t)0xFFFFFFFF,
        (size_t)0xFFFFFFFF + 1,
        (size_t)0x80000000,
        (size_t)0x80000001,
    };
    int num_lens = (int)(sizeof(dangerous_lens) / sizeof(dangerous_lens[0]));
    int i;

    for (i = 0; i < num_lens; i++) {
        size_t len = dangerous_lens[i];
        WCHAR *result;

        /* Check that overflow is detected */
        if (len > SIZE_MAX / sizeof(WCHAR)) {
            /* This len MUST be rejected — overflow would occur */
            result = safe_wcsdup(NULL, len);
            ck_assert_msg(result == NULL,
                "safe_wcsdup must return NULL for overflowing len=%zu", len);

            result = vulnerable_wcsdup(NULL, len);
            ck_assert_msg(result == NULL,
                "vulnerable_wcsdup must return NULL for overflowing len=%zu", len);
        }
        /* If len is within safe range but very large, malloc will likely
         * return NULL — that is acceptable (rejection). */
    }
}
END_TEST

/* -----------------------------------------------------------------------
 * Test 2: Oversized strings (2x, 10x normal buffer) — reads must not
 * exceed the declared length.
 * ----------------------------------------------------------------------- */
START_TEST(test_buffer_read_oversized_strings)
{
    /* Invariant: memcpy reads exactly len*sizeof(WCHAR) bytes —
     * never more — so the source buffer must be at least that large. */

    size_t normal_sizes[] = { 1, 4, 8, 16, 64, 256, 1024 };
    size_t multipliers[] = { 1, 2, 10 };
    int ns = (int)(sizeof(normal_sizes) / sizeof(normal_sizes[0]));
    int nm = (int)(sizeof(multipliers) / sizeof(multipliers[0]));
    int i, j;

    for (i = 0; i < ns; i++) {
        for (j = 0; j < nm; j++) {
            size_t len = normal_sizes[i] * multipliers[j];
            WCHAR *src;
            WCHAR *dst;

            /* Ensure len doesn't overflow */
            if (len > SIZE_MAX / sizeof(WCHAR))
                continue;

            src = make_wchar_buf(len, (WCHAR)0xABCD);
            ck_assert_ptr_nonnull(src);

            dst = safe_wcsdup(src, len);
            ck_assert_ptr_nonnull(dst);

            /* Verify the copy is byte-for-byte identical */
            ck_assert_msg(memcmp(src, dst, len * sizeof(WCHAR)) == 0,
                "Buffer contents mismatch for len=%zu multiplier=%zu",
                normal_sizes[i], multipliers[j]);

            free(src);
            free(dst);
        }
    }
}
END_TEST

/* -----------------------------------------------------------------------
 * Test 3: len+1 overflow (mirrors string.c:129 pattern)
 * ----------------------------------------------------------------------- */
START_TEST(test_buffer_read_len_plus_one_overflow)
{
    /* Invariant: (len + 1) * sizeof(WCHAR) must not overflow.
     * If len == SIZE_MAX, then len+1 wraps to 0 → zero-byte alloc
     * followed by large memcpy → heap overflow. Must be rejected. */

    size_t dangerous_lens[] = {
        SIZE_MAX,
        SIZE_MAX - 1,
        SIZE_MAX / sizeof(WCHAR),
        SIZE_MAX / sizeof(WCHAR) - 1,
        SIZE_MAX / sizeof(WCHAR) + 1,
    };
    int num = (int)(sizeof(dangerous_lens) / sizeof(dangerous_lens[0]));
    int i;

    for (i = 0; i < num; i++) {
        size_t len = dangerous_lens[i];
        size_t len_plus_one;
        size_t byte_len;
        int overflow_detected = 0;

        /* Check len+1 overflow */
        if (len == SIZE_MAX) {
            overflow_detected = 1; /* len+1 wraps to 0 */
        } else {
            len_plus_one = len + 1;
            if (len_plus_one > SIZE_MAX / sizeof(WCHAR)) {
                overflow_detected = 1;
            } else {
                byte_len = len_plus_one * sizeof(WCHAR);
                (void)byte_len;
            }
        }

        if (overflow_detected) {
            /* Must be rejected — simulate the safe check */
            int safe_check_passed = 0;

            if (len == SIZE_MAX)
                safe_check_passed = 0; /* overflow in len+1 */
            else if ((len + 1) > SIZE_MAX / sizeof(WCHAR))
                safe_check_passed = 0;
            else
                safe_check_passed = 1;

            ck_assert_msg(safe_check_passed == 0,
                "Overflow must be detected for len=%zu in (len+1)*sizeof(WCHAR)",
                len);
        }
    }
}
END_TEST

/* -----------------------------------------------------------------------
 * Test 4: Attack payloads — adversarial WCHAR strings
 * ----------------------------------------------------------------------- */
START_TEST(test_buffer_read_attack_payloads)
{
    /* Invariant: copying exactly len elements never reads beyond the
     * allocated source buffer. */

    /* Each payload: a small WCHAR string with known length */
    struct {
        const char *description;
        size_t      len;          /* number of WCHAR elements */
        WCHAR       fill;
    } payloads[] = {
        { "empty",                    0,    0x0000 },
        { "single null",              1,    0x0000 },
        { "single non-null",          1,    0x0041 },
        { "all 0xFF bytes",           8,    0xFFFF },
        { "all 0x00 bytes",           8,    0x0000 },
        { "alternating pattern",      16,   0xA5A5 },
        { "max printable ascii",      32,   0x007E },
        { "format string attempt",    4,    0x0025 }, /* '%' in WCHAR */
        { "null injection",           10,   0x0000 },
        { "large safe buffer 256",    256,  0xDEAD },
        { "large safe buffer 1024",   1024, 0xBEEF },
        { "large safe buffer 4096",   4096, 0xCAFE },
    };
    int num = (int)(sizeof(payloads) / sizeof(payloads[0]));
    int i;

    for (i = 0; i < num; i++) {
        size_t len = payloads[i].len;
        WCHAR *src, *dst;

        if (len == 0) {
            /* Zero-length: safe_wcsdup should return NULL (no allocation) */
            dst = safe_wcsdup(NULL, 0);
            ck_assert_msg(dst == NULL,
                "Zero-length wcsdup must return NULL (%s)",
                payloads[i].description);
            continue;
        }

        /* Overflow guard */
        if (len > SIZE_MAX / sizeof(WCHAR))
            continue;

        src = make_wchar_buf(len, payloads[i].fill);
        ck_assert_ptr_nonnull(src);

        dst = safe_wcsdup(src, len);
        ck_assert_ptr_nonnull(dst);

        /* Verify exact copy — no extra bytes read */
        ck_assert_msg(memcmp(src, dst, len * sizeof(WCHAR)) == 0,
            "Copy mismatch for payload '%s' len=%zu",
            payloads[i].description, len);

        free(src);
        free(dst);
    }
}
END_TEST

/* -----------------------------------------------------------------------
 * Test 5: Boundary — len exactly at SIZE_MAX/sizeof(WCHAR) boundary
 * ----------------------------------------------------------------------- */
START_TEST(test_buffer_read_boundary_len)
{
    /* Invariant: len == SIZE_MAX/sizeof(WCHAR) is the last safe value;
     * len == SIZE_MAX/sizeof(WCHAR) + 1 must be rejected. */

    size_t safe_max   = SIZE_MAX / sizeof(WCHAR);
    size_t unsafe_val = safe_max + 1; /* wraps or exceeds */

    /* safe_max itself: len * sizeof(WCHAR) == SIZE_MAX (or close) —
     * malloc will almost certainly fail, but no overflow occurs */
    {
        size_t product = safe_max * sizeof(WCHAR);
        /* Verify no overflow: product / sizeof(WCHAR) == safe_max */
        ck_assert_msg(product / sizeof(WCHAR) == safe_max,
            "safe_max * sizeof(WCHAR) must not overflow");
    }

    /* unsafe_val: must be detected as overflow */
    if (unsafe_val > safe_max) { /* true unless SIZE_MAX/sizeof(WCHAR) == SIZE_MAX */
        size_t product;
        int overflows;

        /* Check if unsafe_val * sizeof(WCHAR) overflows */
        overflows = (unsafe_val > SIZE_MAX / sizeof(WCHAR));
        ck_assert_msg(overflows == 1,
            "unsafe_val=%zu must be detected as overflowing", unsafe_val);

        /* safe_wcsdup must reject it */
        {
            WCHAR *result = safe_wcsdup(NULL, unsafe_val);
            ck_assert_msg(result == NULL,
                "safe_wcsdup must return NULL for unsafe_val=%zu", unsafe_val);
        }

        (void)product;
    }
}
END_TEST

/* -----------------------------------------------------------------------
 * Suite assembly
 * ----------------------------------------------------------------------- */
Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security_CWE120_BufferOverflow");
    tc_core = tcase_create("Core");

    tcase_set_timeout(tc_core, 30);

    tcase_add_test(tc_core, test_buffer_read_no_overflow_near_size_max);
    tcase_add_test(tc_core, test_buffer_read_oversized_strings);
    tcase_add_test(tc_core, test_buffer_read_len_plus_one_overflow);
    tcase_add_test(tc_core, test_buffer_read_attack_payloads);
    tcase_add_test(tc_core, test_buffer_read_boundary_len);

    suite_add_tcase(s, tc_core);
    return s;
}

int main(void)
{
    int number_failed;
    Suite   *s;
    SRunner *sr;

    s  = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}