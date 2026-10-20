#ifndef APPS_H
#define APPS_H
void apps_list(void);
/* Fixed embedded images only, one foreground isolated process at a time. */
int app_run(const char *name, const char *argument, int *exit_status);
void apps_command(const char *arguments);
int apps_test_all(void);
#endif
