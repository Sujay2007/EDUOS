#include "syscall.h"
#include "vmm.h"
#include "serial.h"
#include "thread.h"
#include "process.h"
#include "fs.h"
#include "graphics.h"
#include <stddef.h>

extern void user_exit_to_kernel(int exit_code);
static struct syscall_stats activity;

static int copy_user_filename(uint64_t address, char name[FS_NAME_BYTES]) {
    for (unsigned i = 0; i < FS_NAME_BYTES; i++) {
        if (address >= USER_SPACE_MAX_ADDR ||
            user_buffer_validate((const void *)address, 1, 0)) return SYSCALL_EFAULT;
        name[i] = *(const char *)address++;
        if (!name[i]) return 0;
    }
    return SYSCALL_EINVAL;
}
static int fs_error(int result) {
    if (result == FS_NOT_FOUND) return SYSCALL_ENOENT;
    if (result == FS_UNMOUNTED) return SYSCALL_ENODEV;
    if (result == FS_INVALID) return SYSCALL_EINVAL;
    return SYSCALL_EIO;
}

void syscall_get_stats(struct syscall_stats *out) {
    if (!out) return;
    uint64_t flags = interrupts_save_and_disable();
    *out = activity;
    interrupts_restore(flags);
}

int user_buffer_validate(const void *ptr, size_t len, int require_write) {
    if (len == 0) {
        return 0;
    }

    if (ptr == NULL) {
        return -1;
    }

    uint64_t start = (uint64_t)ptr;
    uint64_t end = start + len;

    /* Check for integer wrap-around */
    if (end < start) {
        return -1;
    }

    /* Enforce that the entire buffer strictly resides in user space */
    if (start >= USER_SPACE_MAX_ADDR || end > USER_SPACE_MAX_ADDR) {
        return -1;
    }

    /* Verify each 4 KiB page spanning the buffer */
    uint64_t page_start = start & ~(PAGE_SIZE - 1);
    uint64_t page_end = (end - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t page = page_start; page <= page_end; page += PAGE_SIZE) {
        uint64_t pte_flags = 0;
        uint64_t phys = vmm_virt_to_phys(page, &pte_flags);

        if (phys == 0) {
            return -1; /* Page not mapped */
        }

        if (!(pte_flags & PTE_PRESENT)) {
            return -1; /* Page not present */
        }

        if (!(pte_flags & PTE_USER)) {
            return -1; /* Page not accessible to user mode (kernel memory) */
        }

        if (require_write && !(pte_flags & PTE_WRITABLE)) {
            return -1; /* Page is not writable */
        }
    }

    return 0;
}

void syscall_handler(struct interrupt_frame *frame) {
    uint64_t sys_num = frame->rax;
    /* Interrupt gate has IF clear: constant-time accounting only, no rendering. */
    struct thread *caller = thread_get_current();
    activity.total++;
    activity.last_number = sys_num;
    activity.last_tid = caller ? caller->tid : 0;
    activity.last_pid = caller && caller->process ? caller->process->pid : 0;
    switch (sys_num) {
        case SYS_EXIT: activity.exits++; break;
        case SYS_WRITE: activity.writes++; break;
        case SYS_GETPID: activity.getpids++; break;
        case SYS_YIELD: activity.yields++; break;
        case SYS_READFILE: activity.reads++; break;
        case SYS_DIRENT: activity.lists++; break;
        case SYS_CANVAS_RECT: activity.draws++; break;
        case SYS_SLEEP: activity.sleeps++; break;
        default: activity.unknown++; break;
    }

    switch (sys_num) {
        case SYS_READFILE: {
            /* rdi=name, rsi=destination, rdx=capacity, r10=byte offset. */
            char name[FS_NAME_BYTES];
            int result = copy_user_filename(frame->rdi, name);
            if (result) { frame->rax = result; return; }
            if (frame->rdx > SYS_FILE_MAX_BYTES) { frame->rax = SYSCALL_EINVAL; return; }
            if (user_buffer_validate((void *)frame->rsi, frame->rdx, 1)) {
                frame->rax = SYSCALL_EFAULT; return;
            }
            uint32_t index;
            result = fs_lookup(fs_root(), name, &index);
            if (result) { frame->rax = fs_error(result); return; }
            size_t actual;
            result = fs_read(fs_root(), index, frame->r10, (void *)frame->rsi, frame->rdx, &actual);
            frame->rax = result ? fs_error(result) : (int64_t)actual;
            return;
        }
        case SYS_DIRENT: {
            /* rdi=root index, rsi=writable syscall_dirent*. ENOENT ends listing. */
            if (user_buffer_validate((void *)frame->rsi, sizeof(struct syscall_dirent), 1)) {
                frame->rax = SYSCALL_EFAULT; return;
            }
            if (frame->rdi > UINT32_MAX) { frame->rax = SYSCALL_ENOENT; return; }
            struct fs_entry entry;
            int result = fs_list(fs_root(), frame->rdi, &entry);
            if (result) { frame->rax = fs_error(result); return; }
            struct syscall_dirent *out = (void *)frame->rsi;
            for (unsigned i = 0; i < sizeof(out->name); i++) out->name[i] = entry.name[i];
            out->size = entry.size;
            frame->rax = 0;
            return;
        }
        case SYS_CANVAS_RECT: {
            if (user_buffer_validate((void *)frame->rdi, sizeof(struct app_canvas_rect), 0)) {
                frame->rax = SYSCALL_EFAULT; return;
            }
            struct app_canvas_rect r = *(const struct app_canvas_rect *)frame->rdi;
            int result = gterminal_canvas_submit(&r);
            frame->rax = result == -1 ? SYSCALL_ENODEV : result == -2 ? SYSCALL_EINVAL :
                         result == -3 ? SYSCALL_EAGAIN : 0;
            return;
        }
        case SYS_SLEEP:
            if (frame->rdi > 200) { frame->rax = SYSCALL_EINVAL; return; }
            thread_sleep(frame->rdi);
            frame->rax = 0;
            return;
        case SYS_EXIT: {
            int status = (int)frame->rdi;
            struct thread *curr = thread_get_current();
            if (curr != NULL && curr->process != NULL) {
                process_exit(curr->process, status);
            } else {
                user_exit_to_kernel(status);
            }
            /* Does not return */
            break;
        }

        case SYS_WRITE: {
            int fd = (int)frame->rdi;
            const char *buf = (const char *)frame->rsi;
            size_t count = (size_t)frame->rdx;

            /* Check valid file descriptor (1 = stdout, 2 = stderr) */
            if (fd != 1 && fd != 2) {
                frame->rax = SYSCALL_EINVAL;
                return;
            }

            if (count > SYS_WRITE_MAX_BYTES) {
                frame->rax = SYSCALL_EINVAL;
                return;
            }

            if (count == 0) {
                frame->rax = 0;
                return;
            }

            /* Validate user pointer and mapping permissions */
            if (user_buffer_validate(buf, count, 0) != 0) {
                frame->rax = SYSCALL_EFAULT;
                return;
            }

            /* Output string safely to serial terminal */
            for (size_t i = 0; i < count; i++) {
                serial_putchar(buf[i]);
            }

            frame->rax = (int64_t)count;
            return;
        }

        case SYS_GETPID: {
            struct thread *curr = thread_get_current();
            if (curr != NULL && curr->process != NULL) {
                frame->rax = (int64_t)curr->process->pid;
            } else {
                frame->rax = 0;
            }
            return;
        }

        case SYS_YIELD: {
            thread_yield();
            frame->rax = 0;
            return;
        }

        default: {
            /* Unknown system call number: return defined error without corrupting state */
            frame->rax = SYSCALL_ENOSYS;
            return;
        }
    }
}

void syscall_init(void) {
    idt_register_handler(SYSCALL_VECTOR, syscall_handler);
}
