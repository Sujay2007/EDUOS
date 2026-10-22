#ifndef EXPLORER_HELP_H
#define EXPLORER_HELP_H
#define EXPLORER_HELP_COUNT 15
/* Presentation only: no model pointers, allocation, or subsystem operations. */
struct explorer_help {
    const char *start, *details, *steps, *observe, *why, *change, *kernel;
};
extern const struct explorer_help explorer_help[EXPLORER_HELP_COUNT];
#endif
