#ifndef TESTS_H
#define TESTS_H

/* Run tests for paging and IDT table */
#define RUN_TESTS 1
/* Run tests for RTC */
#define RTC_TEST 0
/* Do not show loading message */
#define NOT_SHOW_MESSAGE 1
/* Run invalid page test (Will lead to Page Fault) */
#define INVALID_ADDR_TEST 1

// test launcher
void launch_tests();

#endif /* TESTS_H */
