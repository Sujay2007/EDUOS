#include "explorer_help.h"
const struct explorer_help explorer_help[EXPLORER_HELP_COUNT] = {
 {
  "Processes: create children and explore exit, waiting, adoption and collection. Close help, then R selects parent PID 10 in slot 2 with reaper PID 1 in slot 1.",
  "Numbers 1..8 select slots, NOT PIDs. C creates under the selected living parent; S runs it, B blocks, U makes it ready. X exits with fixed status 7. W acts as a parent waiting/collecting; Z collects a selected zombie as its parent.",
  "1. R, C, C: slots 3 and 4 become ready children PID 11 and 12.\n2. 3, X: PID 11 is a zombie, status 7, modeled pages 0.\n3. 2, W: parent collects status; slot 3 disappears.\n4. 2, X: PID 10 exits; living PID 12 is adopted by PID 1.\n5. 4, S: the adopted child can still run.",
  "Watch PID/PPID, state, modeled pages, zombie count and collected status. Z does not kill a living child. Reaping removes a terminated record; an orphan is a living child whose parent exited.",
  "Exit releases the model execution pages but retains a status record. Parent exit reparents its children. Collection removes the zombie and may wake its waiting parent. At most eight records fit; reaper PID 1 cannot exit.",
  "R, C, W blocks the parent before child exit. Then 3, X automatically collects the child and wakes the parent READY. Unlike exit-before-wait, no zombie record remains to collect manually.",
  "This is a Unix-like teaching model, not real process operations. Our kernel has isolated processes, exit status and explicit launcher cleanup, but no PPID, fork, waitpid, adoption or Unix zombie protocol."
 },
 {
  "CPU scheduling: compare how eligible tasks receive CPU time. Close help and R to restore three tasks, Round Robin, quantum 2 and time 0.",
  "1..4 selects an existing task; N adds task 4. +/- edits burst 1..8; A cycles arrival 0..8; P cycles priority 1..4 (1 highest). G cycles RR, priority, FCFS, SJF. Q cycles quantum 1..4. B/W blocks/wakes an arrived unfinished task. Space ticks; X animates/pauses. Edits restart time.",
  "1. R, N, Q: four tasks, RR quantum 3, time 0.\n2. Space eleven times: timeline is 11122233441; all tasks finish.\n3. G, then Space eleven times: priority gives 11112224433.\n4. G and run again for FCFS; G again for SJF.\n5. R restores defaults rather than merely rewinding.",
  "The ready strip excludes a continuing running task. Timeline cells are completed ticks. Turnaround = finish - arrival; waiting = turnaround - burst - blocked ticks; response = first dispatch - arrival. Pending results need completion.",
  "RR rotates at quantum expiry. FCFS chooses earliest arrival; SJF compares configured full bursts; priority chooses the lowest number. These last three are NONPREEMPTIVE until finish/block. There is no switch cost; history holds 48 ticks.",
  "R, B, Space makes tick 0 idle: task 1 is blocked and others have not arrived. W, Space runs it at time 1. Its blocked count and first-response delay are both 1. X runs one model tick per 20 real PIT ticks while visible.",
  "These tasks and policies are deterministic models. The real kernel uses round-robin threads with a five-tick quantum at 100 Hz; changing this model never changes the real scheduling policy."
 },
 {
  "Physical memory: allocate and free a 16-frame bitmap. R selects frame 2; frames 0, 1 and 15 are reserved, leaving thirteen usable frames.",
  "Left/Right selects a frame and wraps. A allocates the first free bit from the remembered scan position and selects it. F frees the selected allocated usable frame. Reserved or duplicate frees are refused. R resets the complete bitmap.",
  "1. R, A: frame 2 changes from free to allocated.\n2. A: frame 3 is allocated; neither allocation overlaps.\n3. Left, F: free frame 2 while frame 3 remains allocated.\n4. A: the scan reuses frame 2.\n5. R, then A thirteen times: all usable frames fill; another A fails.",
  "Pale is free, teal allocated, amber reserved. A bitmap 1 means unavailable for either reason. The displayed bits run from frame 0 on the left, not conventional most-significant-bit order.",
  "Allocation sets one clear bit and advances the cursor. Free validates ownership/reservation, clears the bit and may lower the cursor so the gap is found again. Allocation does not create a virtual mapping.",
  "R, Left, F tries to free reserved frame 1 and must fail. R, A, F, F shows why a duplicate free cannot manufacture another free frame.",
  "This grid is synthetic, not your RAM map. The real PFA uses Limine usable regions, reserves bitmap metadata and returns aligned 4 KiB physical frames. Tools mem shows actual totals."
 },
 {
  "Virtual memory: change a mapping and see where translation succeeds or stops. R selects address 1 with READ access; Space walks the selected teaching address.",
  "1..4 selects an address. M maps; U unmaps; P toggles leaf writable permission; W toggles requested READ/WRITE. These actions restart walk position. Selection retains READ/WRITE mode. Mapping retains the prior writable flag. R restores all entries.",
  "1. R, Space four times: 0x400123 translates to 0x9123.\n2. 2, Space twice: missing PD[3] stops 0x600123.\n3. M, Space four times: mapping now reaches 0xa123.\n4. U, Space twice: it is unmapped again.\n5. R, 3, W, Space three times: write to 0x401abc fails at its read-only PT leaf.",
  "Each level shows a 9-bit index and presence/access flags. The 12-bit offset is preserved: frame 0x9000 + offset 0x123 = 0x9123. Table cards stay drawn even if the walk stops; they are not live table reads.",
  "The model checks canonical form, missing entries and requested leaf access. Upper modeled entries allow user writes. A physical frame can exist without a reachable virtual mapping. Space cannot step past a failed check.",
  "After the read-only failure, P then Space four times permits the write and displays 0xbabc. Choice 4 is noncanonical 0x800000000000: Space fails before PML4 and M is refused.",
  "These are four selected addresses and three bounded teaching entries, not arbitrary memory dereferences. Real VMM code walks process roots, combines permissions, maps/unmaps pages and invalidates translations."
 },
 {
  "Heap allocation: request variable-sized payloads and inspect first-fit splitting, merging and failure. R starts a 256-byte free arena and a 16-byte payload request.",
  "+ or = increases payload by 16 up to 96; - decreases to 16. A allocates and selects its block. Left/Right selects current blocks without wrapping. F frees that block. Block numbers change after split/merge. R clears the arena and request.",
  "1. R, A, A: two allocated 32-byte extents include 16-byte payloads plus headers.\n2. Left, F: free the first block; the second stays allocated.\n3. Right, F: free the second; adjacent gaps coalesce to one 256-byte extent.\n4. +, +, A: request 48 payload bytes; the new extent is 64 and the remainder 192.",
  "Amber marks metadata, teal allocated extents and pale free gaps. Displayed extent sizes include header/slack, not just payload. A failed request leaves the layout intact.",
  "First fit chooses the earliest sufficient gap. Payloads align to 16; the teaching header is 16 bytes. Split only if a 32-byte minimum remainder fits; otherwise consume the whole gap. Free merges only adjacent free extents.",
  "R, + three times, A three times creates extents 80,80,96. Free first and third, then raise payload to 96 with + twice. A fails: total free is 176 but no gap fits 112 including header. Free the middle block, then retry successfully.",
  "This bounded arena never calls the real heap allocator. Real kmalloc/kfree uses a linked block list, 32-byte headers, first-fit splitting and coalescing; freed blocks normally stay in mapped heap pages."
 },
 {
  "Interrupts: distinguish a pending event, saved return state, handler execution, EOI and return. R gives unmasked model delivery with all counters zero.",
  "K queues a timer event, up to four pending. M toggles the model mask/IF display. Space progresses one event through entry/save/handler/acknowledgement/return. R clears pending, phases, handled count and saved RIP.",
  "1. R, K, M, Space: one event remains pending while masked.\n2. M, Space: accept it; pending becomes 0 and saved RIP is 0x4010.\n3. Space: general-register save stage.\n4. Space: handled count increases to 1.\n5. Space: EOI acknowledged. Space again returns to application stage.",
  "Injection alone does not execute a handler. Counting the event is not EOI, and EOI is not iretq return. The saved RIP remains visible after return. Masking delays new entry, not an already active handler.",
  "A bounded pending counter separates arrival from delivery. A phase counter illustrates preserving state, doing short work, acknowledging the controller and restoring execution. Actual register arrays are not copied by this model.",
  "R, K five times: only four pending events fit. Unmasked Space five times handles and returns from one. Extra Space while idle needs another pending event; unmasking alone does not run the whole handler.",
  "K/M never inject real IRQs or change CPU IF/PIC masks. Real assembly saves interrupt frames; PIT IRQ0 counts ticks and sends EOI, then dispatch permits scheduling. Rendering stays in the normal worker."
 },
 {
  "System calls: issue a modeled request across Ring 3/0 and observe validation before service execution. R selects WRITE with a valid pointer and zero counters.",
  "1 chooses WRITE, 2 READFILE, 3 GETPID (UI choices, not all real syscall numbers). P toggles bad-pointer scenario. C enters a call; Space validates, executes and returns. Active pointer/service arguments cannot be edited. R resets.",
  "1. R, P, C: invalid-buffer WRITE enters the kernel model.\n2. Space: EFAULT -2 appears, copied bytes 0, return pending.\n3. Space: Ring 3 resumes; completed calls becomes 1.\n4. P, C, Space: restore valid pointer and validate.\n5. Space, Space: fixed result 8, copied count 8, completed calls 2.",
  "Colored user/kernel regions show the active phase. Rejected calls skip service execution but still complete after return. Result RAX is a model value, not a sampled register.",
  "The bad-pointer flag represents an unmapped buffer crossing. Validation rejects it before any copy count increases. GETPID has no buffer argument, so its result is independent of that flag.",
  "With the pointer invalid, choose 3, C and Space three times: GETPID returns fixed PID 11 and copies no bytes. READFILE returns a fixed count 8; it is not connected to the Filesystems experiment.",
  "This does not execute int 0x80 or read live user memory. Real syscall code validates every page/range and dispatches services; actual embedded apps exercise that ABI in Tools."
 },
 {
  "Filesystems: vary a read offset and buffer capacity; compare real EOF with missing-name failure. R selects text hello.txt, offset 0 and capacity 8.",
  "1 text, 2 empty, 3 missing: selection clears offset/result but keeps capacity. O cycles offset 0,8,16,24,32,40,0. L cycles capacity 8,16,32. D performs the model read. O/L alone leave the previous result until D. R clears all settings/results.",
  "1. R, D: return 8 bytes, Hello fr.\n2. O, D: offset 8 returns om a bou.\n3. O, L, L, D: offset 16, capacity 32 returns only 16 remaining bytes.\n4. O, O, D: offset 32 is EOF, zero bytes.\n5. 2, D gives empty-file EOF; 3, D gives lookup failure instead.",
  "Read the destination bytes AND context: both missing and EOF show a zero count but have different causes. The directory label says 31 bytes; the backing string/read algorithm actually uses 32. No disk is changed here.",
  "A successful bounded read copies min(capacity, size-offset). Offset at/past EOF returns zero. The fixed missing choice fails before copying. The displayed sector formula is explanatory; the bytes come from a static string.",
  "R, O three times, L, D: offset 24 and capacity 16 returns just the final 8 bytes, not the label-derived 7. Try another O then D to reach EOF. Selecting a new file resets offset but not the chosen capacity.",
  "The experiment is a model, not ATA I/O. Real EDUFS validates metadata, looks up names and reads persistent sectors through block_device and ATA PIO. Tools cat/run view uses that real mounted disk."
 },
 {
  "Concurrency: choose task operations yourself to create a lost update, serialize updates, or form a resource cycle. R starts an unlocked counter at x=0.",
  "1/2 executes the chosen task next operation; it is not select-only. L toggles locking/global order AND restarts. D toggles counter/deadlock mode AND restarts but keeps the order setting. F in deadlock mode releases the last chosen actor resources. R clears everything.",
  "1. R, 1, 2: both tasks read shared 0 into local registers.\n2. 1, 2: each adds locally; shared x is still 0.\n3. 1, 2: both store 1; one increment is lost.\n4. R, L, 1, 1, 1, 2, 2, 2: complete locked transactions; final x is 2.",
  "Local registers, next READ/ADD/STORE phases and shared x are different state. A lock protects the whole read-modify-write sequence. DONE tasks need reset before another increment.",
  "Two unprotected transactions can use the same old value, then overwrite each other. A lock forces one transaction to finish before the peer reads. Global resource ordering prevents circular hold-and-wait.",
  "R, D, 1, 2, 1, 2 forms deadlock: T1 holds A and waits B; T2 holds B and waits A. Teal edges hold resources; red edges request them. F releases T2 resources. Old wait labels may persist until acquisition/restart; R,D,L starts the globally ordered alternative.",
  "These are chosen interleavings and model locks/resources, not real kernel tasks or mutexes. The single-CPU kernel uses short interrupt-masked updates; it has no general mutex/semaphore/condition-variable API."
 },
 {
  "Keyboard input: fill and drain a finite event ring. R empties eight model slots, resets head/tail/drop count and clears the last-consumed display.",
  "K injects one hardcoded capital A event. Space consumes one queued event. R resets. Arbitrary typed characters are not payloads, and this model ring is separate from the real PS/2/serial input ring.",
  "1. R, K eight times: all slots fill; count 8, head 0, tail 0.\n2. K again: one event is dropped; count stays 8.\n3. Space: consume slot 0; count 7, tail 1, last consumed A.\n4. K: refill the freed slot; head 1, count 8.\n5. Space eight times empties it; last consumed still shows historical A.",
  "Equal head and tail can mean full or empty because this model keeps a separate count. An empty consumer cannot underflow. A full producer cannot overwrite unread input or grow the queue.",
  "Enqueue advances head modulo eight; consume advances tail. Buffering separates fast arrival from later processing. The A/scancode explanation is illustrative; K directly assigns the model character rather than decoding a real A key.",
  "R, Space shows empty input. Then inject a burst with K before consuming: the ninth pending event drops. R clears both occupancy and the historical/drop indicators.",
  "This eight-slot model does not measure real keyboard loss. Real IRQ1 decodes scan codes into a 128-entry ring (127 usable), and the consumer blocks/wakes through thread input support."
 },
 {
  "Framebuffer: move a cursor and change retained logical pixels. R clears a 12x8 grid, selects (0,0), and restores teal color index 1.",
  "Arrows move one cell and clamp at x=0..11, y=0..7. G cycles teal/gold/red/blue. Space paints one pixel; F paints a clipped 3x2 rectangle from the cursor. R clears; there is no per-pixel eraser/background color key.",
  "1. R, Space: pixel (0,0) becomes teal.\n2. Right, Down, G: cursor (1,1), gold selected; moving did not paint.\n3. F: x=1..3 and y=1..2 become gold.\n4. Right ten times, Down six times: cursor reaches (11,7).\n5. F: only that corner pixel fits; other rectangle cells are clipped.",
  "The black top/left marker locates the cursor. Cursor X/Y and color numbers show movement even if the last-action text stays unchanged. Previously painted pixels keep their colors until overwritten or reset.",
  "Drawing writes bounded color indices in a retained 8-row, 12-column array. Rectangle loops stop at its edges. The real renderer magnifies the array without exposing arbitrary framebuffer addresses.",
  "R, G, F paints a gold rectangle at the top-left. Move before another F to overlap it, then change G and paint again: only covered pixels change. R returns every pixel to background.",
  "The grid is a teaching image, while the UI uses real clipped gfx_pixel/gfx_rect and font primitives. Actual framebuffer addressing uses pitch and pixel masks. This does not add a GPU/window manager or arbitrary RGB input."
 },
 {
  "OS architecture: follow one request through five system layers. This is a supporting walkthrough, not an application launch or file-read simulator. Close help, then R starts at Application request.",
  "Space advances one highlighted stage; B goes back; R returns to the first. E gives deeper explanation, I the actual implementation. There are no editable request arguments, live transactions, result buffers or failure injection controls.",
  "1. R: Application request is highlighted.\n2. Space: Trust boundary - validate before privileged work.\n3. Space: Kernel service - dispatch the request.\n4. Space: Filesystem abstraction - resolve the logical operation.\n5. Space: Device access - a driver would perform hardware I/O.\n6. B reverses one stage; R restores the first.",
  "Only the shared stage counter and its explanation change. Reaching Device access does not read a sector, launch an app or prove a syscall completed. The source view connects these conceptual layers to real files.",
  "Each layer narrows responsibility: applications request services, the kernel constrains authority, the filesystem handles names/extents and the driver handles the device. General OS theory and actual execution evidence are different.",
  "B from the last stage revisits Filesystem abstraction. For a REAL separate example, close help and use Tab, then run view hello.txt in Tools with a mounted disk. That command is not executed by this diagram.",
  "The actual monolithic kernel routes embedded app requests through syscall_handler, fs_lookup/fs_read, block_read and ata_read. This walkthrough never invokes that path."
 },
 {
  "Boot dependencies: inspect six initialization prerequisites. The OS is already booted; these controls neither reboot nor reorder its real startup. R begins with Firmware.",
  "Space advances; B reverses; R restarts. E explains dependencies and I identifies _start, linker and loader references. All four supporting walkthroughs share a stage counter, so use R after entering.",
  "1. R: Firmware chooses a boot route.\n2. Space: Limine / ELF loads the kernel and supplies responses.\n3. Space: Physical allocator tracks usable backing frames.\n4. Space: Paging and heap depend on frames/mappings.\n5. Space: Interrupts / scheduling establish execution support.\n6. Space: Explorer can use initialized input, graphics and a worker.",
  "A stage highlight is explanatory, not a boot log or test result. The compact labels group many actual initialization calls. Extra Space at the end cannot advance; B and R are safe model navigation.",
  "An allocator needs usable-memory information. Page tables/heap need backing frames. Interrupt descriptors, a timer and saved contexts support preemption and wakeup. Missing dependencies would break real use, but cannot be removed with these keys.",
  "At Paging and heap, B revisits Physical allocator: explain why heap expansion cannot precede usable frames. Read I to compare the grouped diagram with the full real initialization order; do not interpret the diagram as measured boot timing.",
  "Real _start initializes serial/memory/CPU infrastructure, scheduler/processes and tests/storage before graphics and the Explorer worker. Firmware and Limine are outside kernel entry. The model performs no fresh initialization."
 },
 {
  "CPU execution: inspect a fixed arithmetic/call/stack illustration. R starts at MOV RAX,2, already showing RAX=2 and RBX=0. It is not an instruction emulator or live register debugger.",
  "Space advances one of six stages, B goes back, R restarts. Scroll to see RAX/RBX and stack descriptions. There is no editable program, memory/flags view, numeric RIP/RSP or actual instruction execution control.",
  "1. R: MOV RAX,2; RAX 2, RBX 0, empty stack description.\n2. Space: ADD RAX,3; RAX becomes 5.\n3. Space: CALL helper; stack describes a return address.\n4. Space: PUSH RAX; value 5 is above that address.\n5. Space: POP RBX; RBX becomes 5, return address remains.\n6. Space: RET; description says return restored, stack empty.",
  "The displayed values are formulas of stage: RAX is 2 only at stage 0, otherwise 5; RBX is 5 from POP onward. Stack contents are explanatory text, not stored simulated memory. No real thread register changes.",
  "Calls need a return location distinct from pushed operands. Resuming execution requires compatible registers, stack and address space. This fixed example explains those relationships without a general CPU simulator.",
  "B from RET returns to POP with RBX still 5 and the return address described. R restores the initial MOV state. If no stage is highlighted after another walkthrough, R clears the shared step.",
  "Actual interrupt stubs preserve interrupt_frame registers; scheduler_on_interrupt selects a frame to resume; user_enter_context constructs Ring 3 entry. The supporting trace does not execute those paths."
 },
 {
  "Failure investigation: follow six evidence questions for a missing-process screenshot. This is a reasoning walkthrough, not live diagnosis. R starts at Observe launch.",
  "Space advances; B reverses; R restarts. E explains debugging reasoning and I links real instrumentation/tests. There is no failure injection, log import, process selector, screenshot comparator or test runner in this canvas.",
  "1. R: Observe launch - was work actually created?\n2. Space: Check lifetime - is it still alive?\n3. Space: Collect snapshot - does sampled data include it?\n4. Space: Finish rendering - has queued output reached pixels?\n5. Space: Inspect pixels - do they show the expected identity?\n6. Space: Assert evidence - keep the actual behavioral check.",
  "Only a prompt stage is highlighted. The final card does NOT run an assertion or establish PASS. A serial marker and a screenshot file alone are insufficient evidence that a specific process was displayed.",
  "Separate launch, lifetime, collection and drawing to distinguish real kernel failure from harness timing. Bounded waits should synchronize with the expected observable condition rather than deleting the assertion.",
  "B from the final card revisits pixel inspection. Close help and use Tab for real live/livecheck commands or host tests when you need actual evidence. Those are separate operations, not a hidden mode of this walkthrough.",
  "Real live_collect, live_runtime_check, gterminal_flush and QEMU tests inspect actual kernel/graphics behavior. This trace only teaches which evidence to seek; it never runs diagnostics."
 },
};
