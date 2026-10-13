#ifndef SHELL_H
#define SHELL_H

/*
 * Initialize the interactive shell subsystem.
 */
void shell_init(void);

/*
 * Run the interactive shell REPL loop.
 * This function does not return under normal circumstances.
 */
void shell_run(void);

#endif /* SHELL_H */
