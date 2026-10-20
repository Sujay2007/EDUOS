#ifndef PROCESS_TEST_H
#define PROCESS_TEST_H

/*
 * Execute the process management, address space isolation,
 * supervisor protection, and fault containment test suite.
 * Returns 0 on success, non-zero if any test fails.
 */
int process_test_all(void);

#endif /* PROCESS_TEST_H */
