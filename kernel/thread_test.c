#include "thread_test.h"
#include "thread.h"
#include "pit.h"
#include "serial.h"

/* --- Test 1: Single Thread Creation & Safe Exit --- */
static volatile int test1_ran = 0;

static void worker_test1(void *arg) {
    (void)arg;
    test1_ran = 1;
    /* Thread returns without calling thread_exit(), verifying safe return trampoline */
}

/* --- Test 2: Cooperative Multitasking with Yield --- */
static volatile int coop_a_count = 0;
static volatile int coop_b_count = 0;
static volatile int coop_a_done = 0;
static volatile int coop_b_done = 0;

static void worker_coop_a(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        coop_a_count++;
        thread_yield();
    }
    coop_a_done = 1;
}

static void worker_coop_b(void *arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        coop_b_count++;
        thread_yield();
    }
    coop_b_done = 1;
}

/* --- Test 3: Genuine Preemption Without Yield --- */
static volatile uint64_t preempt_a_count = 0;
static volatile uint64_t preempt_b_count = 0;
static volatile int preempt_a_done = 0;
static volatile int preempt_b_done = 0;

static void worker_preempt_a(void *arg) {
    (void)arg;
    for (volatile uint64_t i = 0; i < 10000000ULL; i++) {
        if ((i % 1000000ULL) == 0) {
            preempt_a_count++;
        }
    }
    preempt_a_done = 1;
}

static void worker_preempt_b(void *arg) {
    (void)arg;
    for (volatile uint64_t i = 0; i < 10000000ULL; i++) {
        if ((i % 1000000ULL) == 0) {
            preempt_b_count++;
        }
    }
    preempt_b_done = 1;
}

/* --- Test 4: Timed Sleep and Wakeup --- */
static volatile int sleeper_done = 0;
static volatile uint64_t sleep_start_ticks = 0;
static volatile uint64_t sleep_end_ticks = 0;

static void worker_sleeper(void *arg) {
    (void)arg;
    sleep_start_ticks = timer_get_ticks();
    thread_sleep(15); /* Sleep for 15 ticks = 150 ms */
    sleep_end_ticks = timer_get_ticks();
    sleeper_done = 1;
}

int thread_test_all(void) {
    serial_write("\r\n====================================================================\r\n");
    serial_write("Starting Kernel Multitasking & Scheduler Verification Suite...\r\n");

    /* ------------------------------------------------------------- */
    /* Test 1: Thread creation, execution, and return termination    */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[Thread Test 1] Creating worker thread and verifying entry & exit...\r\n");
    test1_ran = 0;
    struct thread *t1 = thread_create("test1_worker", worker_test1, NULL);
    if (t1 == NULL) {
        serial_write("  [FAIL] Failed to create worker thread!\r\n");
        return -1;
    }

    /* Yield CPU until worker executes and terminates */
    while (!test1_ran) {
        thread_yield();
    }

    serial_write("  Worker thread executed successfully (entry called).\r\n");
    if (scheduler_check_invariants() != 0) {
        serial_write("  [FAIL] Scheduler invariant violation after Test 1!\r\n");
        return -1;
    }
    serial_write("  [PASS] Thread creation, execution, and return trampoline verified!\r\n");

    /* ------------------------------------------------------------- */
    /* Test 2: Cooperative multitasking with voluntary yield         */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[Thread Test 2] Cooperative multitasking with thread_yield()...\r\n");
    coop_a_count = 0;
    coop_b_count = 0;
    coop_a_done = 0;
    coop_b_done = 0;

    struct thread *t2a = thread_create("coop_a", worker_coop_a, NULL);
    struct thread *t2b = thread_create("coop_b", worker_coop_b, NULL);
    if (t2a == NULL || t2b == NULL) {
        serial_write("  [FAIL] Failed to create cooperative worker threads!\r\n");
        return -2;
    }

    while (!coop_a_done || !coop_b_done) {
        thread_yield();
    }

    serial_write("  coop_a final counter: ");
    serial_write_dec(coop_a_count);
    serial_write(", coop_b final counter: ");
    serial_write_dec(coop_b_count);
    serial_write("\r\n");

    if (coop_a_count == 5 && coop_b_count == 5 && scheduler_check_invariants() == 0) {
        serial_write("  [PASS] Cooperative multitasking verified!\r\n");
    } else {
        serial_write("  [FAIL] Counter mismatch or invariant violation in cooperative threads!\r\n");
        return -2;
    }

    /* ------------------------------------------------------------- */
    /* Test 3: Genuine timer-driven preemption (NO voluntary yield)  */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[Thread Test 3] Timer-driven preemption (computation without yielding)...\r\n");
    preempt_a_count = 0;
    preempt_b_count = 0;
    preempt_a_done = 0;
    preempt_b_done = 0;

    struct scheduler_stats stats_before;
    scheduler_get_stats(&stats_before);

    struct thread *t3a = thread_create("preempt_a", worker_preempt_a, NULL);
    struct thread *t3b = thread_create("preempt_b", worker_preempt_b, NULL);
    if (t3a == NULL || t3b == NULL) {
        serial_write("  [FAIL] Failed to create preemption worker threads!\r\n");
        return -3;
    }

    /*
     * Main thread waits. Notice worker_preempt_a and worker_preempt_b NEVER
     * yield voluntarily. Progress between them occurs solely through PIT timer
     * interrupts at 100 Hz.
     */
    while (!preempt_a_done || !preempt_b_done) {
        thread_yield();
    }

    struct scheduler_stats stats_after;
    scheduler_get_stats(&stats_after);

    serial_write("  preempt_a count: ");
    serial_write_dec(preempt_a_count);
    serial_write(", preempt_b count: ");
    serial_write_dec(preempt_b_count);
    serial_write("\r\n");
    serial_write("  Timer-driven preemptions during test: ");
    serial_write_dec(stats_after.timer_preemptions - stats_before.timer_preemptions);
    serial_write("\r\n");

    if (preempt_a_done && preempt_b_done &&
        (stats_after.timer_preemptions > stats_before.timer_preemptions) &&
        scheduler_check_invariants() == 0) {
        serial_write("  [PASS] Genuine timer-driven preemption verified!\r\n");
    } else {
        serial_write("  [FAIL] Preemption test did not record timer preemptions or violated invariants!\r\n");
        return -3;
    }

    /* ------------------------------------------------------------- */
    /* Test 4: Timed sleep and wakeup (thread_sleep)                 */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[Thread Test 4] Non-busy timed sleep (thread_sleep(15) = ~150 ms)...\r\n");
    sleeper_done = 0;
    sleep_start_ticks = 0;
    sleep_end_ticks = 0;

    struct thread *t4 = thread_create("sleeper", worker_sleeper, NULL);
    if (t4 == NULL) {
        serial_write("  [FAIL] Failed to create sleeper thread!\r\n");
        return -4;
    }

    /* Give sleeper a turn to start and enter sleep */
    thread_yield();

    /* Verify thread transitions to BLOCKED */
    if (t4->state == THREAD_STATE_BLOCKED) {
        serial_write("  Sleeper thread state confirmed: BLOCKED (not consuming CPU)\r\n");
    }

    uint64_t idle_ticks_before = scheduler_get_idle_ticks();

    /* Wait for sleeper to wake up and finish */
    while (!sleeper_done) {
        thread_yield();
    }

    uint64_t idle_ticks_after = scheduler_get_idle_ticks();
    uint64_t elapsed_ticks = sleep_end_ticks - sleep_start_ticks;
    serial_write("  Sleeper start tick: ");
    serial_write_dec(sleep_start_ticks);
    serial_write(", end tick: ");
    serial_write_dec(sleep_end_ticks);
    serial_write(" (elapsed: ");
    serial_write_dec(elapsed_ticks);
    serial_write(" ticks)\r\n");

    serial_write("  Idle thread ticks during sleep: ");
    serial_write_dec(idle_ticks_after - idle_ticks_before);
    serial_write("\r\n");

    if (elapsed_ticks >= 15 && idle_ticks_after > idle_ticks_before && scheduler_check_invariants() == 0) {
        serial_write("  [PASS] Timed sleep, deadline wake-up, and idle-thread execution verified!\r\n");
    } else {
        serial_write("  [FAIL] Sleeper woke up prematurely, idle did not run, or invariant failed!\r\n");
        return -4;
    }

    /* ------------------------------------------------------------- */
    /* Test 5: Scheduler metrics and thread table verification       */
    /* ------------------------------------------------------------- */
    serial_write("\r\n[Thread Test 5] Checking scheduler execution telemetry...\r\n");
    struct scheduler_stats final_stats;
    scheduler_get_stats(&final_stats);

    serial_write("  Total Context Switches : ");
    serial_write_dec(final_stats.total_switches);
    serial_write("\r\n  Timer Preemptions      : ");
    serial_write_dec(final_stats.timer_preemptions);
    serial_write("\r\n  Voluntary Yields       : ");
    serial_write_dec(final_stats.voluntary_yields);
    serial_write("\r\n  Active Threads Count   : ");
    serial_write_dec(final_stats.active_threads);
    serial_write("\r\n  Running Threads Count  : ");
    serial_write_dec(final_stats.running_threads);
    serial_write("\r\n  Ready Threads Count    : ");
    serial_write_dec(final_stats.ready_threads);
    serial_write("\r\n");

    if (scheduler_check_invariants() != 0) {
        serial_write("  [FAIL] Invariant check failed: running_threads != 1!\r\n");
        return -5;
    }

    if (final_stats.running_threads != 1) {
        serial_write("  [FAIL] Multiple or zero threads marked RUNNING!\r\n");
        return -5;
    }

    if (final_stats.total_switches == 0 || final_stats.timer_preemptions == 0) {
        serial_write("  [FAIL] Scheduler counters invalid!\r\n");
        return -5;
    }

    if (final_stats.voluntary_yields > 500) {
        serial_write("  [FAIL] Excessive runaway voluntary yields detected!\r\n");
        return -5;
    }

    serial_write("  [PASS] Exactly one thread RUNNING verified; scheduler metrics reflect real execution!\r\n");

    serial_write("\r\n====================================================================\r\n");
    serial_write("All Kernel Multitasking & Scheduler Tests PASSED successfully!\r\n");
    return 0;
}
