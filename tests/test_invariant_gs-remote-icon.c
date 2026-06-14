#include <check.h>
#include <stdlib.h>
#include <string.h>

/* 
 * We cannot directly call the internal function, but we can test the invariant
 * that basename length must be >= 4 before the memcpy operation.
 * This simulates the vulnerable code path logic.
 */

static int safe_extension_replace(const char *uri_basename, char *output, size_t output_size)
{
    size_t len = strlen(uri_basename);
    
    /* Security invariant: basename must be at least 4 characters to safely replace extension */
    if (len < 4) {
        return -1; /* Reject short basenames to prevent underflow */
    }
    
    if (len >= output_size) {
        return -1;
    }
    
    strcpy(output, uri_basename);
    memcpy(output + len - 4, ".png", 4);
    return 0;
}

START_TEST(test_basename_length_invariant)
{
    /* Invariant: extension replacement must not underflow on short basenames */
    const char *payloads[] = {
        "abc",           /* Exploit case: 3 chars causes underflow */
        "abcd",          /* Boundary: exactly 4 chars, minimum safe */
        "file.svg",      /* Valid input: normal case */
        "",              /* Edge case: empty string */
        "x",             /* Edge case: single char */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        char output[256] = {0};
        size_t len = strlen(payloads[i]);
        int result = safe_extension_replace(payloads[i], output, sizeof(output));
        
        /* Security property: short basenames must be rejected, not cause buffer overflow */
        if (len < 4) {
            ck_assert_int_eq(result, -1);
        } else {
            ck_assert_int_eq(result, 0);
            /* Verify the extension was correctly replaced */
            ck_assert_str_eq(output + len - 4, ".png");
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_basename_length_invariant);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}