#include "education.h"
#include "education_model.h"
#include "lessons.h"
#include "keyboard.h"
#include "serial.h"
#include "pit.h"
#define PAPER 0xf3f0e8U
#define INK 0x203449U
#define MUTED 0x536878U
#define NAVY 0x142338U
#define TEAL 0x087e83U
#define GOLD 0xa96816U
#define SOFT 0xe0e8e7U
#define RED 0xb6423aU
static struct gfx_surface *screen, body;
static volatile unsigned tools, head, tail;
static volatile unsigned char events[64];
/* Source-content indices are separate from the direct-entry explorer order. */
static const unsigned concepts[]={6,7,3,4,5,8,9,11,10,12,13,0,1,2,14};
static const char *const names[]={"Processes","CPU scheduling","Physical memory","Virtual memory","Heap allocation","Interrupts","System calls","Filesystems","Concurrency","Keyboard input","Framebuffer","OS architecture","Boot dependencies","CPU execution","Failure investigation"};
static const char *const summaries[]={"Create children; exit, wait, adopt and reap.","Run tasks; change policy, quantum and arrivals.","Allocate and free frames; inspect bitmap bits.","Map, unmap and translate selected addresses.","Request blocks; split gaps and coalesce frees.","Mask, inject, acknowledge and return.","Validate requests across a privilege boundary.","Look up files; vary offsets and bounded reads.","Choose interleavings; create or prevent deadlock.","Queue key events; consume them or overflow.","Move a cursor; draw pixels and rectangles.","Trace a request across system layers.","Explore which subsystem depends on which.","Change registers and inspect a call stack.","Separate launch, sampling and rendering stages."};
static unsigned concept, overlay, answer, tick_step;
static int home=1, dirty=1, scroll, total_height, cursor_y;
static unsigned main_x, scale=2, canvas_height;
static char messages[15][256];
static unsigned note_used;
static struct edu_tree tree;
static struct edu_frames frames;
static struct edu_vm vm;
static struct edu_heap heap;
static struct edu_schedule schedule;
static unsigned proc_sel=1,frame_sel=2,vm_sel,heap_sel,task_sel,request=16,write_access,walk;
static unsigned task_count=3,policy=2,quantum=2,auto_run;
static unsigned arrivals[4]={0,1,2,0},bursts[4]={4,3,2,2},priorities[4]={2,1,3,2};
static uint64_t next_run;
static unsigned irq_mask,pending,irq_phase,irq_count,saved_rip;
static unsigned call_phase,call_kind,pointer_bad,call_count,call_bytes;
static int call_result;
static unsigned file_sel,file_offset,file_length=8,file_bytes;
static char file_buffer[33];
static unsigned race_mode,lock_order,owner[2],local[2],phase[2],wait_req[2],shared,last_actor;
static unsigned key_head,key_tail,key_count,key_drops,last_key;
static unsigned char key_ring[8];
static unsigned pixel_x,pixel_y,pixel_color;
static unsigned char pixels[8][12];
static const uint32_t palette[]={SOFT,TEAL,GOLD,RED,0x50749a};
static void text(struct gfx_surface *s,int x,int y,const char *t,uint32_t fg,uint32_t bg,unsigned z) {
    while(*t) {gfx_glyph(s,x,y,*t++,fg,bg,z);x+=6*z;}
}
static void num(struct gfx_surface *s,int x,int y,unsigned value,uint32_t fg,uint32_t bg,unsigned z) {
    char b[12],r[12];unsigned n=0;
    do {r[n++]='0'+value%10;value/=10;}while(value);
    for(unsigned i=0;i<n;i++)b[i]=r[n-i-1];b[n]=0;text(s,x,y,b,fg,bg,z);
}
static void hex(struct gfx_surface *s,int x,int y,uint64_t value,uint32_t fg,unsigned digits) {
    char b[19];const char *d="0123456789abcdef";b[0]='0';b[1]='x';
    for(unsigned i=0;i<digits;i++)b[2+i]=d[(value>>((digits-i-1)*4))&15];b[2+digits]=0;
    text(s,x,y,b,fg,PAPER,1);
}
static void paragraph(const char *t,uint32_t fg) {
    unsigned cols=body.width/(6*scale);
    while(*t) {
        while(*t==' ')t++;
        unsigned n=0,space=0;
        while(t[n]&&t[n]!='\n'&&n<cols){if(t[n]==' ')space=n;n++;}
        if(t[n]&&t[n]!='\n'&&space)n=space;
        for(unsigned i=0;i<n;i++)gfx_glyph(&body,i*6*scale,cursor_y-scroll,t[i],fg,PAPER,scale);
        cursor_y+=11*scale;t+=n;if(*t=='\n')t++;
    }
    cursor_y+=8;
}
static void heading(const char *s){paragraph(s,TEAL);}
static void note(const char *s) {
    note_used=0;while(*s&&note_used<255)messages[concept][note_used++]=*s++;messages[concept][note_used]=0;
}
static void append(const char *s) {while(*s&&note_used<255)messages[concept][note_used++]=*s++;messages[concept][note_used]=0;}
static void append_num(unsigned value) {
    char r[12];unsigned n=0;do{r[n++]='0'+value%10;value/=10;}while(value);
    while(n&&note_used<255)messages[concept][note_used++]=r[--n];messages[concept][note_used]=0;
}
static struct gfx_surface region(unsigned x,unsigned y,unsigned w,unsigned h) {
    struct gfx_surface s=*screen;s.pixels+=y*s.pitch+x*s.bytes_per_pixel;s.width=w;s.height=h;return s;
}
static void schedule_restart(void) {
    edu_schedule_init(&schedule,arrivals,bursts,task_count,policy);schedule.quantum=quantum;
    for(unsigned i=0;i<task_count;i++)schedule.task[i].priority=priorities[i];auto_run=0;
}
static void reset_model(void) {
    scroll=0;answer=0;tick_step=0;
    if(concept==0){edu_tree_init(&tree);proc_sel=1;note("Select a process, then create children. PIDs identify processes; PPIDs identify their parents. No scenario order is required. This is a Unix-like teaching model, not our kernel lifecycle.");}
    else if(concept==1){task_count=3;policy=2;quantum=2;task_sel=0;for(unsigned i=0;i<4;i++){arrivals[i]=i<3?i:0;bursts[i]=i<3?4-i:2;priorities[i]=i==0?2:i==1?1:i==2?3:2;}schedule_restart();note("Space runs one tick; X runs/pauses. Edit a selected task or compare policies. Quantum expiry rotates RR; FCFS, SJF and priority are nonpreemptive in this model. Lower priority number wins.");}
    else if(concept==2){edu_frames_init(&frames);frame_sel=2;note("Thirteen usable frames start free; three amber frames are reserved. A scans from a remembered position and sets the first available bit. Select a frame to free with F.");}
    else if(concept==3){edu_vm_init(&vm);vm_sel=walk=write_access=0;note("Select address 1..4. M maps, U unmaps, P toggles leaf write permission, W switches read/write. Space walks the current tables; mapping is separate from physical allocation.");}
    else if(concept==4){edu_heap_init(&heap);heap_sel=0;request=16;note("The 256-byte arena starts as one free extent. +/- changes payload size. A uses first fit; F frees the selected block. Headers use 16 teaching bytes, versus 32 in our real heap.");}
    else if(concept==5){irq_mask=pending=irq_phase=irq_count=saved_rip=0;note("K injects a timer IRQ into a bounded pending counter. M toggles interrupt masking. Space advances entry, saving, handling, EOI and return. No real interrupt settings are changed.");}
    else if(concept==6){call_phase=call_kind=pointer_bad=call_count=call_result=call_bytes=0;note("C requests a service from Ring 3. Space validates the buffer, executes the service and returns. P toggles an invalid pointer. A rejected request never copies bytes.");}
    else if(concept==7){file_sel=file_offset=file_bytes=0;file_length=8;file_buffer[0]=0;note("This small read-only file model separates lookup, offset and length. Select a file, adjust the offset, then D reads bounded bytes. Empty files and EOF return zero; Tools offers the real EDUFS disk.");}
    else if(concept==8){race_mode=lock_order=shared=last_actor=0;for(unsigned i=0;i<2;i++){owner[i]=local[i]=phase[i]=wait_req[i]=0;}note("Press 1 or 2 to execute that task's next read/add/store operation. Choose the interleaving yourself. L enables a lock; D opens the two-resource deadlock experiment.");}
    else if(concept==9){key_head=key_tail=key_count=key_drops=last_key=0;note("K injects a modeled A key (make scan code 0x1e). Space consumes one queued event. Eight slots model buffering; a ninth pending key is refused and increments the drop counter.");}
    else if(concept==10){pixel_x=pixel_y=0;pixel_color=1;for(unsigned y=0;y<8;y++)for(unsigned x=0;x<12;x++)pixels[y][x]=0;note("Move with arrows. Space paints one pixel; F paints a clipped 3x2 rectangle. G changes color. Drawing changes retained model pixels; the UI safely renders this magnified framebuffer.");}
    else note("This supporting walkthrough is optional. Space advances, B reverses, R resets. The main mechanism explorers offer direct operations; E opens the deeper explanation and I names the real kernel implementation.");
}
void education_attach(struct gfx_surface *s) {
    screen=s;tools=head=tail=0;home=1;overlay=0;
    for(concept=0;concept<15;concept++)reset_model();concept=0;dirty=1;
}
int education_active(void){return screen&&!tools;}
int education_input(unsigned char key,int serial) {
    if(!screen||serial)return 0;
    if(key=='\t')tools=!tools;else if(tools)return 0;
    unsigned next=(head+1)%64;if(next!=tail){events[head]=key;head=next;}return 1;
}
static void process_action(unsigned char k) {
    if(k>='1'&&k<='8'){proc_sel=k-'1';note("Selected slot ");append_num(proc_sel+1);append(". Slot numbers are controls; the PID and PPID in the tree identify the process.");return;}
    struct edu_process *p=&tree.p[proc_sel];
    if(k=='c') {int slot=edu_process_create(&tree,proc_sel);if(slot<0)note("Creation refused: select a living parent and leave an unused slot. A zombie cannot create children; the model holds at most eight process records.");else{note("Created child PID ");append_num(tree.p[slot].pid);append(" with parent PID ");append_num(p->pid);append(". It is READY with three modeled pages. Selection stays on the parent so C can create another sibling.");}}
    else if(k=='x'){unsigned pid=p->pid;int was_waiting=0;for(unsigned i=0;i<8;i++)if(tree.p[i].pid==p->ppid)was_waiting=tree.p[i].waiting;
        if(edu_process_exit(&tree,proc_sel,7))note("Exit refused: an empty/terminated slot cannot exit again, and the designated reaper cannot be terminated.");
        else{note("PID ");append_num(pid);append(was_waiting?" exited status 7; its waiting parent collected the status immediately and became READY. The child was reaped.":" exited status 7. Execution and three modeled pages are released; a ZOMBIE record retains status until its parent collects it.");append(" Living children were adopted by PID 1; adoption does not terminate them.");}}
    else if(k=='w'){int result=edu_process_wait(&tree,proc_sel);note(result<0?"Wait refused: select a living parent with children.":result?"Collected exit status 7 and reaped one terminated child. Its record is removed; already released execution pages are not freed twice.":"Parent is BLOCKED waiting for a child. A subsequent child exit will deliver status, reap the record and make this parent READY.");}
    else if(k=='z'){int parent=edu_process_reap(&tree,proc_sel);if(parent<0)note("Collection refused: select a ZOMBIE child. Its current parent is the status collector; living children cannot be reaped.");else{proc_sel=parent;note("The child's parent collected status 7. The child is REAPED: its table record is gone, and the parent is selected. Reaping is not killing a running child.");}}
    else if(k=='s'||k=='b'||k=='u'){unsigned state=k=='s'?EDU_RUNNING:k=='b'?EDU_BLOCKED:EDU_READY;int result=edu_process_state(&tree,proc_sel,state);note(result?"State change refused: a zombie/unused slot cannot execute, block or wake.":k=='s'?"Dispatched the selected process. The previous running process becomes READY; only one modeled CPU can run at once.":k=='b'?"Selected process is BLOCKED. It keeps its memory and identity but cannot execute until awakened.":"Selected process is READY. Wake permits scheduling; it does not imply immediate execution.");}
}
static void scheduling_action(unsigned char k) {
    if(k>='1'&&k<='4'&&(unsigned)(k-'1')<task_count){task_sel=k-'1';note("Selected task ");append_num(task_sel+1);append(". Its arrival, burst and priority can be edited; configuration changes restart time for a fair policy comparison.");return;}
    int edit=1;
    if(k=='g')policy=(policy+1)%4;
    else if(k=='q')quantum=quantum%4+1;
    else if(k=='a')arrivals[task_sel]=(arrivals[task_sel]+1)%9;
    else if(k=='+'||k=='='){if(bursts[task_sel]<8)bursts[task_sel]++;}
    else if(k=='-'){if(bursts[task_sel]>1)bursts[task_sel]--;}
    else if(k=='p')priorities[task_sel]=priorities[task_sel]%4+1;
    else if(k=='n'){if(task_count<4)task_sel=task_count++;else{note("Task capacity reached: four tasks. Reset to the initial three-task experiment with R.");return;}}
    else edit=0;
    if(edit){schedule_restart();note("Configuration changed; time restarted. Policy ");append_num(policy+1);append(" uses the same task parameters. Priority 1 is highest; SJF chooses the shortest full burst; ties use task order. RR quantum is ");append_num(quantum);append(" ticks.");return;}
    if(k=='b'||k=='w'){int result=edu_schedule_block(&schedule,task_sel,k=='b');note(result?"Block/wake refused: the task has not arrived or has already completed.":k=='b'?"Task blocked: it leaves the runnable set and retains its remaining burst. Advance time to observe other tasks or idle CPU ticks.":"Task awakened: it rejoins eligible work. Waiting excludes blocked ticks; response remains first dispatch minus arrival.");}
    if(k=='x'){auto_run=!auto_run;next_run=timer_get_ticks()+20;note(auto_run?"Running one modeled tick every 20 real PIT ticks. X pauses; all graphics still execute in the worker, never in an interrupt handler.":"Animation paused. Space advances exactly one tick; edit tasks or inspect the timeline at your own pace.");}
    if(k==' '){if(!edu_schedule_step(&schedule)){auto_run=0;note("Run complete or 48-tick history limit reached. Change a policy to compare the same tasks, or R resets the initial experiment.");}else{int t=schedule.timeline[schedule.time-1];note("Tick ");append_num(schedule.time-1);if(t<0)append(" was idle: no arrived, unblocked, unfinished task was eligible.");else{append(" ran task ");append_num(t+1);append(policy==0?": FCFS preserves the earliest arrived eligible job until finish/block.":policy==1?": SJF chose the shortest eligible burst and keeps it until finish/block.":policy==2?": RR continues its quantum or rotates to the next eligible task at expiry.":": priority chose the lowest numeric eligible priority and keeps it until finish/block.");}append(" Timeline cells show completed ticks, not live kernel execution.");}}
}

static void memory_action(unsigned char k) {
    if(concept==2){
        if(k==KEY_LEFT)frame_sel=(frame_sel+15)%16;else if(k==KEY_RIGHT)frame_sel=(frame_sel+1)%16;
        else if(k=='a'){int frame=edu_frame_alloc(&frames);if(frame<0)note("Allocation failed: all usable bitmap bits are set. Reserved frames stay unavailable. Free an allocated frame before requesting another.");else{frame_sel=frame;note("Allocated frame ");append_num(frame);append(": the first clear bit found from the remembered cursor changed 0 -> 1. It now has an owner; allocation alone does not create a virtual mapping.");}}
        else if(k=='f'){int result=edu_frame_free(&frames,frame_sel);note(result?"Free refused: the selected frame is reserved or already free. Clearing it would violate allocator invariants.":"Freed the selected frame: its bitmap bit changed 1 -> 0. The scan cursor can reuse this gap on the next allocation; reserved bits did not change.");}
    }else if(concept==3){
        if(k>='1'&&k<='4'){vm_sel=k-'1';walk=0;note("Address selected. Space follows its actual index bits in the teaching tables. M/U modifies only its model mapping, never real process page tables.");}
        else if(k=='w'){write_access=!write_access;walk=0;note(write_access?"Request is now a WRITE. Present alone is insufficient: effective write permission must also allow access.":"Request is now a READ. A read-only leaf still permits this access if present.");}
        else if(k=='m'||k=='u'||k=='p'){walk=0;if(vm_sel==3)note("Mapping refused: the selected address is noncanonical in this four-level model. No table entry is created.");else{struct edu_pte *e=&vm.entry[vm_sel];if(k=='p')e->writable=!e->writable;else e->present=k=='m';note(k=='m'?"Mapping installed: the selected virtual page now names its model physical frame. The walker can follow the previously missing level.":k=='u'?"Mapping removed: the frame identity still exists, but this virtual address is no longer reachable. Space will identify the missing entry.":"Leaf writable permission toggled. Effective permission is the intersection of all levels; other modeled levels permit user writes.");}}
        else if(k==' '){unsigned stop;uint64_t physical;int result=edu_vm_translate(&vm,vm_sel,write_access,&physical,&stop);if(walk<stop)walk++;note(result==-1?"STOP before PML4: bits 63..48 do not sign-extend bit 47. This address is noncanonical; no table is walked.":result==-2&&walk>=stop?(stop==2?"STOP at PD[3]: present=0. The model cannot reach a PT or a physical byte. M installs the missing mapping.":"STOP at PT: present=0. Allocation and translation are different; no physical address is produced."):result==-3&&walk>=stop?"STOP at PT write permissions: the leaf is present but read-only. A real write would page fault before modifying data.":walk==4?"Translation succeeded: the leaf frame plus the unchanged 12-bit offset yields the physical byte shown below. No real memory was accessed.":"Walk advanced one level: extract its 9-bit index, check the entry, then follow the next table. No arbitrary address is dereferenced.");}
    }else{
        if(k==KEY_LEFT&&heap_sel)heap_sel--;else if(k==KEY_RIGHT&&heap_sel+1<heap.count)heap_sel++;
        else if(k=='+'||k=='='){if(request<96)request+=16;note("Payload size changed. A will search from the first block, including a 16-byte teaching header and alignment.");}
        else if(k=='-'){if(request>16)request-=16;note("Payload size decreased. Existing allocations are unchanged until explicitly freed.");}
        else if(k=='a'){int i=edu_heap_alloc(&heap,request);if(i<0)note("Allocation refused: no single free extent fits payload plus header. Total free bytes can exceed a request while fragmentation prevents a contiguous allocation.");else{heap_sel=i;note("First fit selected block ");append_num(i+1);append(". It is allocated; a sufficient remainder splits into a new free block. Amber marks metadata, so payload capacity is smaller than extent size.");}}
        else if(k=='f'){int result=edu_heap_free(&heap,heap_sel);if(heap_sel>=heap.count)heap_sel=heap.count-1;note(result?"Free refused: select an allocated block. Double-free does not create extra capacity.":"Block freed. Adjacent free neighbors coalesced, restoring larger contiguous extents. Real kfree similarly reuses mapped heap pages rather than automatically returning frames.");}
    }
}
static const char file_data[]="Hello from a bounded file model!";
static void peripheral_action(unsigned char k) {
    if(concept==5){
        if(k=='m'){irq_mask=!irq_mask;note(irq_mask?"Interrupts masked in the model. Pending events remain queued; entry cannot begin until M unmasks.":"Interrupts unmasked in the model. A pending event may enter on the next Space.");}
        else if(k=='k'){if(pending<4){pending++;note("Timer event queued. Pending count increased; injection alone does not acknowledge or execute the handler.");}else note("Pending-event capacity reached (4). The new modeled IRQ is refused; no unbounded queue is created.");}
        else if(k==' '){if(!irq_phase){if(irq_mask)note("Entry delayed by interrupt masking. Pending count is preserved; toggle M to permit delivery.");else if(!pending)note("No pending event: the CPU keeps executing. K injects one.");else{pending--;irq_phase=1;saved_rip=0x4010;note("CPU accepted the IRQ and saved return RIP/flags. User entry would also use TSS rsp0 for the privileged stack.");}}
            else if(irq_phase==1){irq_phase=2;note("Assembly saved general registers. The saved frame must survive so interrupted execution can resume.");}
            else if(irq_phase==2){irq_phase=3;irq_count++;note("Handler executed: the modeled tick counter increased. Expensive graphics work is deferred; the IRQ has not yet been acknowledged.");}
            else if(irq_phase==3){irq_phase=4;note("EOI sent to the modeled PIC. A scheduling decision may choose another saved frame; this experiment resumes the same context.");}
            else{irq_phase=0;note("iretq restored the saved state. RIP resumes at 0x4010; pending IRQs remain available for later delivery.");}}
    }else if(concept==6){
        if(call_phase&&(k=='p'||(k>='1'&&k<='3'))){note("Arguments are captured for the active call. Finish its return before editing pointer/service, or R resets.");return;}
        if(k=='p'){pointer_bad=!pointer_bad;note(pointer_bad?"Pointer made invalid: the requested buffer crosses an unmapped page. Validation must reject it before copying.":"Pointer restored to a valid writable user range. Service bounds still apply.");}
        else if(k>='1'&&k<='3'){call_kind=k-'1';note("Service selected: modeled write, readfile or getpid. C enters the ABI; Space advances validation, execution and return.");}
        else if(k=='c'){if(call_phase)note("A call is already active. Finish its return before issuing another, or reset.");else{call_phase=1;call_result=call_bytes=0;note("User registers contain the service and arguments. int 0x80 entered Ring 0 through a DPL-3 gate; the kernel stack now holds the return frame.");}}
        else if(k==' '){if(call_phase==1){if(pointer_bad&&call_kind!=2){call_result=-2;call_phase=3;note("EFAULT (-2): every touched page must be valid. The request was rejected before service execution; copied bytes remain zero.");}else{call_phase=2;note("Arguments validated: bounds and required page permissions permit the service. A function call within user mode would not cross this privilege boundary.");}}
            else if(call_phase==2){call_result=call_kind==2?11:8;call_bytes=call_kind==2?0:8;call_phase=3;note("Service executed. Only the bounded validated buffer was used; result RAX contains a byte count or PID. This is modeled data, not a live syscall.");}
            else if(call_phase==3){call_phase=0;call_count++;note("Returned to Ring 3 with RAX result. All other required registers and the user stack are restored. Completed-call count increased.");}
            else note("No active call. C issues a new request; P changes the buffer validity.");}
    }else if(concept==7){
        if(k>='1'&&k<='3'){file_sel=k-'1';file_offset=file_bytes=0;file_buffer[0]=0;note("Filename selected. 1 is text, 2 is empty, 3 is missing. Lookup must succeed before an extent is read.");}
        else if(k=='o'){file_offset=file_offset>=40?0:file_offset+8;note("Offset advanced by 8 bytes (wraps after 40). The file does not grow; reads at or beyond its EOF return zero.");}
        else if(k=='l'){file_length=file_length==32?8:file_length*2;note("Read capacity changed to ");append_num(file_length);append(" bytes. Bounded read returns the minimum of capacity and remaining logical file bytes.");}
        else if(k=='d'){file_bytes=0;unsigned size=file_sel==0?sizeof(file_data)-1:0;if(file_sel!=2&&file_offset<size){file_bytes=size-file_offset;if(file_bytes>file_length)file_bytes=file_length;for(unsigned i=0;i<file_bytes;i++)file_buffer[i]=file_data[file_offset+i];}file_buffer[file_bytes]=0;note(file_sel==2?"Lookup failed: no directory record exists. No sector or buffer copy occurred.":!file_bytes?"EOF: zero bytes returned. An empty file or offset beyond size must not read the next file's extent.":"Lookup succeeded; directory metadata identifies the extent. The bounded read copied ");if(file_bytes){append_num(file_bytes);append(" bytes, preserving the file's EOF. The buffer below contains exactly that prefix.");}}
    }else if(concept==8){
        if(k=='d'){race_mode=!race_mode;owner[0]=owner[1]=phase[0]=phase[1]=wait_req[0]=wait_req[1]=local[0]=local[1]=shared=0;note(race_mode?"Two-resource experiment: task 1 requests A then B; task 2 requests B then A. Choose 1/2 to acquire each resource. L enforces one global order; F releases the selected task's resources as recovery.":"Counter experiment reset. Each task has read, add and store operations. Choose their order with 1/2. L enables mutual exclusion across the complete transaction.");}
        else if(k=='l'){lock_order=!lock_order;owner[0]=owner[1]=phase[0]=phase[1]=wait_req[0]=wait_req[1]=local[0]=local[1]=shared=0;note(lock_order?"Locking/order enabled and experiment restarted. Counter updates serialize; resource acquisition uses A before B in both tasks, breaking circular wait.":"Protection disabled and experiment restarted. You can now arrange a lost update or opposing resource requests.");}
        else if(k=='f'&&race_mode){for(unsigned i=0;i<2;i++)if(owner[i]==last_actor+1)owner[i]=0;phase[last_actor]=0;note("Selected task released its resources and restarted. This modeled recovery breaks the cycle; real recovery also needs rollback to preserve data invariants.");}
        else if(k=='1'||k=='2'){unsigned t=k-'1';last_actor=t;
            if(!race_mode){if(phase[t]>=3){note("Task already finished its one increment. Reset to try another interleaving.");return;}if(!phase[t]){if(lock_order&&owner[0]&&owner[0]!=t+1){note("Task blocked on the lock held by its peer. Advance the owner until store/unlock, then retry this task.");return;}if(lock_order)owner[0]=t+1;local[t]=shared;phase[t]++;note("Task read shared x into its local register. Another unprotected task can still read the same old value.");}else if(phase[t]==1){local[t]++;phase[t]++;note("Task incremented only its local register; shared x is unchanged until store.");}else{shared=local[t];phase[t]++;if(lock_order)owner[0]=0;note("Task stored its local value to shared x and released its lock if held. If both tasks read the same old x, the final store loses one increment.");}}
            else{if(phase[t]>=2){for(unsigned i=0;i<2;i++)if(owner[i]==t+1)owner[i]=0;phase[t]=3;note("Task completed and released both resources. Its peer may now acquire them.");return;}unsigned resource=lock_order?phase[t]:t==0?phase[t]:1-phase[t];if(!owner[resource]||owner[resource]==t+1){owner[resource]=t+1;wait_req[t]=0;phase[t]++;note("Resource acquired exclusively. The task retains it while requesting the next resource; hold-and-wait can form a cycle.");}else{wait_req[t]=1;note("Task waits: its requested resource is held by its peer. ");if(!lock_order&&owner[0]==1&&owner[1]==2&&wait_req[0]&&wait_req[1])append("DEADLOCK: T1 holds A, waits B; T2 holds B, waits A. L prevents this through global ordering; F models recovery.");}}}
    }else if(concept==9){
        if(k=='k'){if(key_count<8){key_ring[key_head]='A';key_head=(key_head+1)%8;key_count++;note("A make event decoded and enqueued. The consumer has not used it yet; the interrupt handler does not draw.");}else{key_drops++;note("Input ring FULL: event refused and drop count incremented. Bounded buffering prevents memory growth but cannot absorb an unlimited burst.");}}
        else if(k==' '){if(key_count){last_key=key_ring[key_tail];key_tail=(key_tail+1)%8;key_count--;note("Consumer removed one A event and updated the model output. Wakeup and later scheduling separate interrupt arrival from visible feedback.");}else note("Input ring empty: a real consumer can block instead of spinning. K injects an event.");}
    }else if(concept==10){
        if(k==KEY_LEFT&&pixel_x)pixel_x--;else if(k==KEY_RIGHT&&pixel_x<11)pixel_x++;else if(k==KEY_UP&&pixel_y)pixel_y--;else if(k==KEY_DOWN&&pixel_y<7)pixel_y++;
        else if(k=='g'){pixel_color=pixel_color%4+1;note("Drawing color changed; existing pixels retain their colors until overwritten or reset.");}
        else if(k==' '||k=='f'){unsigned w=k==' '?1:3,h=k==' '?1:2;for(unsigned y=pixel_y;y<pixel_y+h&&y<8;y++)for(unsigned x=pixel_x;x<pixel_x+w&&x<12;x++)pixels[y][x]=pixel_color;note(k==' '?"One retained pixel changed. Its actual framebuffer location depends on origin, scale and pitch; the magnified view shows its logical coordinate.":"A 3x2 rectangle changed retained pixels, clipped at the grid boundary. No write can escape the modeled framebuffer dimensions.");}
    }else{if(k==' '&&tick_step+1<edu_lessons[concepts[concept]].steps)tick_step++;if(k=='b'&&tick_step)tick_step--;note("Supporting walkthrough stage ");append_num(tick_step+1);append(". E explains the mechanism and I shows source references. These ancillary traces remain step-based; direct experiments are available in the main explorers.");}
}
static void event(unsigned char k) {
    if(k>='A'&&k<='Z')k+='a'-'A';dirty=1;
    if(k=='\t')return;
    if(k==KEY_ESC){if(overlay){overlay=0;scroll=0;}else{home=1;auto_run=0;scroll=0;}return;}
    if(home){if(k==KEY_DOWN||k==KEY_RIGHT)concept=(concept+1)%15;else if(k==KEY_UP||k==KEY_LEFT)concept=(concept+14)%15;else if(k=='\n'||k==' '){home=0;scroll=overlay=0;}return;}
    if(k=='e'||k=='i'||k=='?'){if(k=='?')answer=0;overlay=k=='e'?(overlay==1?0:1):k=='i'?(overlay==2?0:2):3;scroll=0;auto_run=0;return;}
    if(k==KEY_PAGE_DOWN){scroll+=canvas_height/2;return;}if(k==KEY_PAGE_UP){scroll-=canvas_height/2;if(scroll<0)scroll=0;return;}
    if(k==KEY_HOME){scroll=0;return;}if(k==KEY_END){scroll=total_height;return;}
    if(overlay){if(overlay==3&&k>='1'&&k<='3')answer=k-'0';return;}
    if(k=='r'){reset_model();return;}
    if(concept==0)process_action(k);else if(concept==1)scheduling_action(k);else if(concept<=4)memory_action(k);else peripheral_action(k);
}

static void chrome(void) {
    gfx_clear(screen,PAPER);gfx_rect(screen,0,0,screen->width,48,NAVY);
    text(screen,20,14,"INTERACTIVE OS EXPLORER",0xffffff,NAVY,2);
    text(screen,screen->width-150,20,"Tab: Tools / Explorer",0x7dd8ce,NAVY,1);
    gfx_rect(screen,0,screen->height-30,screen->width,30,NAVY);
    text(screen,16,screen->height-19,home?"Arrows: choose | Enter: experiment | Tab: shell + live inspector":"Esc: back | R: reset | E: deeper explanation | I: source | PgUp/Dn: canvas | Tab: Tools",0xdce8f5,NAVY,1);
}
static void home_page(void) {
    text(screen,24,66,"Try a mechanism. Change it. See why.",INK,PAPER,screen->width>=900?2:1);
    text(screen,24,100,"Open any experiment directly. No lesson sequence, score or completion requirement.",MUTED,PAPER,1);
    unsigned columns=screen->width>=900?3:2,rows=(screen->height-170)/94,shown=columns*rows;
    if(!shown)shown=columns;
    unsigned first=concept>=shown?(concept/columns-rows+1)*columns:0;
    unsigned width=(screen->width-48)/columns;
    for(unsigned i=first;i<15&&i<first+shown;i++) {
        unsigned x=24+((i-first)%columns)*width,y=138+((i-first)/columns)*94;
        uint32_t bg=i==concept?TEAL:SOFT,fg=i==concept?0xffffff:INK;
        gfx_rect(screen,x,y,width-12,82,bg);
        text(screen,x+10,y+12,names[i],fg,bg,width>=360?2:1);
        /* Compact wrapping keeps cards usable at 640 pixels. */
        const char *t=summaries[i];unsigned cols=(width-32)/6,row=0;
        while(*t&&row<3){unsigned n=0,space=0;while(t[n]&&n<cols){if(t[n]==' ')space=n;n++;}if(t[n]&&space)n=space;
            for(unsigned k=0;k<n;k++)gfx_glyph(screen,x+10+k*6,y+39+row*11,t[k],fg,bg,1);
            t+=n;while(*t==' ')t++;row++;}
    }
}
static unsigned depth(unsigned slot) {
    unsigned d=0,pid=tree.p[slot].ppid;
    for(unsigned n=0;pid&&n<8;n++) {int found=-1;for(unsigned i=0;i<8;i++)if(tree.p[i].state&&tree.p[i].pid==pid){found=i;break;}if(found<0)break;d++;pid=tree.p[found].ppid;}
    return d;
}
static void process_canvas(void) {
    unsigned pages=0,zombies=0;
    for(unsigned i=0;i<8;i++){pages+=tree.p[i].pages;zombies+=tree.p[i].state==EDU_ZOMBIE;}
    text(&body,0,0-scroll,"Model pages:",MUTED,PAPER,1);num(&body,90,0-scroll,pages,TEAL,PAPER,1);
    text(&body,150,0-scroll,"Zombies:",MUTED,PAPER,1);num(&body,210,0-scroll,zombies,RED,PAPER,1);
    text(&body,265,0-scroll,"Collected:",MUTED,PAPER,1);num(&body,335,0-scroll,tree.reaped,TEAL,PAPER,1);
    text(&body,390,0-scroll,"Last status:",MUTED,PAPER,1);num(&body,470,0-scroll,tree.last_status,TEAL,PAPER,1);
    for(unsigned i=1;i<8;i++)if(tree.p[i].state){unsigned parent=0;for(unsigned j=0;j<8;j++)if(tree.p[j].state&&tree.p[j].pid==tree.p[i].ppid)parent=j;
        int x=20+depth(i)*24,y=34+i*43-scroll,py=34+parent*43-scroll;
        gfx_rect(&body,x-12,(py<y?py:y)+20,2,(py<y?y-py:py-y)+1,TEAL);gfx_rect(&body,x-12,y+20,12,2,TEAL);}
    for(unsigned i=0;i<8;i++) {
        struct edu_process *p=&tree.p[i];int x=20+depth(i)*24,y=34+i*43-scroll;
        if(!p->state){num(&body,0,y+10,i+1,MUTED,PAPER,1);text(&body,28,y+10,"unused / record available",MUTED,PAPER,1);continue;}
        uint32_t bg=p->state==EDU_ZOMBIE?0xf3d8cb:p->state==EDU_RUNNING?TEAL:SOFT,fg=p->state==EDU_RUNNING?0xffffff:INK;
        if(i==proc_sel)gfx_rect(&body,x-3,y-3,body.width-x,42,GOLD);
        gfx_rect(&body,x,y,body.width-x-4,36,bg);
        num(&body,0,y+10,i+1,INK,PAPER,1);text(&body,x+7,y+5,"PID",fg,bg,1);num(&body,x+34,y+5,p->pid,fg,bg,2);
        const char *state=p->state==EDU_READY?"READY":p->state==EDU_RUNNING?"RUNNING":p->state==EDU_BLOCKED?(p->waiting?"BLOCKED / wait":"BLOCKED"):"ZOMBIE / terminated";
        text(&body,x+114,y+5,state,fg,bg,body.width>=800?2:1);
        text(&body,x+7,y+24,"PPID",fg,bg,1);num(&body,x+40,y+24,p->ppid,fg,bg,1);
        text(&body,x+90,y+24,"pages",fg,bg,1);num(&body,x+132,y+24,p->pages,fg,bg,1);
        if(p->state==EDU_ZOMBIE){text(&body,x+168,y+24,"exit",RED,bg,1);num(&body,x+203,y+24,p->status,RED,bg,1);}
        else if(p->adopted)text(&body,x+168,y+24,"ADOPTED / living orphan",GOLD,bg,1);
    }
    cursor_y=390;
}
static int continuing(void) {
    unsigned i=schedule.current;
    return i<task_count&&!schedule.task[i].blocked&&schedule.task[i].ran<schedule.task[i].burst&&(policy!=2||schedule.slice<quantum);
}
static void scheduling_canvas(void) {
    const char *const policies[]={"FCFS","Shortest job first","Round robin","Priority (nonpreemptive)"};
    heading(policies[policy]);int y=cursor_y-scroll;
    text(&body,0,y,"time",INK,PAPER,1);num(&body,34,y,schedule.time,TEAL,PAPER,2);
    text(&body,110,y,"quantum",INK,PAPER,1);num(&body,165,y,quantum,TEAL,PAPER,2);
    text(&body,245,y,auto_run?"RUNNING ANIMATION":"PAUSED / Space advances",TEAL,PAPER,1);cursor_y+=34;
    y=cursor_y-scroll;text(&body,0,y,"READY QUEUE / eligible order:",INK,PAPER,1);
    unsigned order[4],n=0;
    for(unsigned k=0;k<task_count;k++) {unsigned i=policy==2&&schedule.current<task_count?(schedule.current+1+k)%task_count:k;struct edu_task *t=&schedule.task[i];
        if(t->arrival<=schedule.time&&t->ran<t->burst&&!t->blocked&&!(continuing()&&schedule.current==i))order[n++]=i;}
    if(policy!=2)for(unsigned i=0;i<n;i++)for(unsigned j=i+1;j<n;j++){unsigned a=order[i],b=order[j];unsigned va=policy==0?arrivals[a]:policy==1?bursts[a]:priorities[a],vb=policy==0?arrivals[b]:policy==1?bursts[b]:priorities[b];if(vb<va){order[i]=b;order[j]=a;}}
    for(unsigned i=0;i<n;i++)num(&body,206+i*30,y,order[i]+1,TEAL,PAPER,2);
    if(!n)text(&body,206,y,"empty",MUTED,PAPER,1);cursor_y+=30;
    y=cursor_y-scroll;
    unsigned cell=body.width/24;if(cell>28)cell=28;
    for(unsigned i=0;i<48;i++){int t=schedule.timeline[i];uint32_t color=t<0?SOFT:palette[t%4+1];unsigned xx=(i%24)*cell;int yy=y+(i/24)*30;
        gfx_rect(&body,xx,yy,cell-2,26,color);if(i<schedule.time){if(t>=0)num(&body,xx+3,yy+7,t+1,0xffffff,color,1);else text(&body,xx+3,yy+7,"-",INK,color,1);}}
    cursor_y+=76;
    static const unsigned x[]={0,38,150,185,220,255,310,355,395,435,475};
    static const char *const labels[]={"Task","State","Arr","Burst","Pri","Ran","Finish","Wait","Turn","Resp","Block"};
    for(unsigned i=0;i<11;i++)text(&body,x[i],cursor_y-scroll,labels[i],TEAL,PAPER,1);
    cursor_y+=21;
    for(unsigned i=0;i<task_count;i++){struct edu_task *t=&schedule.task[i];y=cursor_y-scroll;
        if(i==task_sel)gfx_rect(&body,0,y-3,body.width,24,SOFT);
        uint32_t bg=i==task_sel?SOFT:PAPER;
        const char *state=t->finish?"DONE":t->arrival>schedule.time?"NEW":t->blocked?"BLOCKED":continuing()&&i==schedule.current?"RUNNING":"READY";
        num(&body,x[0],y,i+1,INK,bg,1);text(&body,x[1],y,state,t->blocked?GOLD:TEAL,bg,1);
        num(&body,x[2],y,t->arrival,INK,bg,1);num(&body,x[3],y,t->burst,INK,bg,1);num(&body,x[4],y,t->priority,INK,bg,1);num(&body,x[5],y,t->ran,INK,bg,1);
        if(t->finish){unsigned turn=t->finish-t->arrival;num(&body,x[6],y,t->finish,TEAL,bg,1);num(&body,x[7],y,turn-t->burst-t->blocked_ticks,TEAL,bg,1);num(&body,x[8],y,turn,TEAL,bg,1);}else text(&body,x[6],y,"pending",MUTED,bg,1);
        if(t->first!=UINT32_MAX)num(&body,x[9],y,t->first-t->arrival,TEAL,bg,1);
        num(&body,x[10],y,t->blocked_ticks,GOLD,bg,1);cursor_y+=28;}
    scale=1;paragraph("Turn = finish - arrival. Wait = turn - burst - blocked ticks. Response = first dispatch - arrival. Pending metrics wait for completion. No switch cost is modeled.",MUTED);scale=2;
}
static void memory_canvas(void) {
    if(concept==2){
        unsigned cell=body.width/8;if(cell>110)cell=110;
        for(unsigned i=0;i<16;i++){int x=(i%8)*cell,y=(i/8)*82-scroll;unsigned mask=1U<<i;uint32_t color=frames.reserved&mask?GOLD:frames.used&mask?TEAL:SOFT;
            if(i==frame_sel)gfx_rect(&body,x,y,cell-5,74,INK);gfx_rect(&body,x+3,y+3,cell-11,68,color);
            num(&body,x+10,y+9,i,(frames.used&mask)?0xffffff:INK,color,2);text(&body,x+10,y+40,frames.reserved&mask?"RESERVED":frames.used&mask?"ALLOCATED":"FREE",(frames.used&mask)?0xffffff:INK,color,1);}
        cursor_y=180;heading("Bitmap / 1 means unavailable");int y=cursor_y-scroll;
        for(unsigned i=0;i<16;i++)num(&body,i*20,y,(frames.used>>i)&1,TEAL,PAPER,2);
        cursor_y+=35;scale=1;paragraph("Amber bits stay set. Allocation sets a free bit; free clears only a usable allocated bit. The remembered scan position avoids restarting every search at frame zero.",MUTED);scale=2;
    }else if(concept==3){
        int y=-scroll;const uint64_t address=edu_vm_address(vm_sel);unsigned stop;uint64_t physical;int result=edu_vm_translate(&vm,vm_sel,write_access,&physical,&stop);
        text(&body,0,y,"Virtual",INK,PAPER,1);hex(&body,60,y,address,TEAL,12);text(&body,200,y,write_access?"WRITE":"READ",write_access?GOLD:TEAL,PAPER,1);
        text(&body,290,y,"offset",INK,PAPER,1);hex(&body,340,y,address&4095,TEAL,3);cursor_y=32;
        static const char *const levels[]={"PML4","PDPT","PD","PT"};unsigned w=body.width/4;
        for(unsigned i=0;i<4;i++){int x=i*w,yy=cursor_y-scroll;uint32_t bg=walk==i?TEAL:SOFT;if((unsigned)i==stop&&walk>=stop&&result)bg=0xf3d8cb;
            gfx_rect(&body,x,yy,w-8,126,bg);text(&body,x+8,yy+10,levels[i],bg==TEAL?0xffffff:INK,bg,2);
            text(&body,x+8,yy+43,"index",INK,bg,1);num(&body,x+48,yy+43,edu_page_index(address,i),bg==TEAL?0xffffff:INK,bg,1);
            int present=vm_sel<3&&(i<2||vm.entry[vm_sel].present||(i==2&&vm_sel!=1));
            text(&body,x+8,yy+65,present?"P=1 U=1":"P=0",INK,bg,1);
            text(&body,x+8,yy+84,i==3&&vm_sel<3&&!vm.entry[vm_sel].writable?"WRITE DENIED":"RW permitted",INK,bg,1);}
        cursor_y+=150;
        if(result&&walk>=stop){heading(result==-1?"NONCANONICAL / no walk":result==-2?"UNMAPPED / no physical byte":"PERMISSION FAULT / write refused");}
        else if(walk>=4){heading("TRANSLATED physical byte");hex(&body,0,cursor_y-scroll,physical,TEAL,8);cursor_y+=28;}
        else heading("Space follows the next table level");
        scale=1;paragraph("1: 0x400123 mapped | 2: 0x600123 initially missing PD | 3: 0x401abc initially read-only | 4: noncanonical. Upper modeled entries allow user writes; the leaf determines final write permission.",MUTED);scale=2;
    }else{
        text(&body,0,0-scroll,"Payload request:",INK,PAPER,1);num(&body,110,0-scroll,request,TEAL,PAPER,2);text(&body,180,0-scroll,"bytes / alignment 16",MUTED,PAPER,1);
        int y=40-scroll;for(unsigned i=0;i<heap.count;i++){struct edu_block *b=&heap.blocks[i];unsigned x=b->start*body.width/256,w=b->size*body.width/256;
            if(i==heap_sel)gfx_rect(&body,x,y,w-2,100,INK);gfx_rect(&body,x+2,y+2,w-6,96,b->used?TEAL:SOFT);
            if(b->used)gfx_rect(&body,x+2,y+2,16*body.width/256-4,16,GOLD);num(&body,x+5,y+32,i+1,b->used?0xffffff:INK,b->used?TEAL:SOFT,1);num(&body,x+5,y+60,b->size,b->used?0xffffff:INK,b->used?TEAL:SOFT,1);}
        cursor_y=160;heading("Blocks / extent includes header");
        for(unsigned i=0;i<heap.count;i++){y=cursor_y-scroll;num(&body,0,y,i+1,INK,PAPER,1);text(&body,30,y,heap.blocks[i].used?"allocated":"free",heap.blocks[i].used?TEAL:MUTED,PAPER,1);text(&body,120,y,"start",INK,PAPER,1);num(&body,160,y,heap.blocks[i].start,INK,PAPER,1);text(&body,230,y,"extent",INK,PAPER,1);num(&body,280,y,heap.blocks[i].size,INK,PAPER,1);cursor_y+=19;}
    }
}

static void line(int x,int y,int xx,int yy,uint32_t color) {
    int dx=xx>x?xx-x:x-xx,sx=x<xx?1:-1,dy=yy>y?y-yy:yy-y,sy=y<yy?1:-1,error=dx+dy;
    for(;;){gfx_pixel(&body,x,y,color);if(x==xx&&y==yy)break;int e=2*error;if(e>=dy){error+=dy;x+=sx;}if(e<=dx){error+=dx;y+=sy;}}
}
static void value_row(const char *label,unsigned value) {
    int y=cursor_y-scroll;text(&body,0,y,label,INK,PAPER,1);num(&body,210,y,value,TEAL,PAPER,2);cursor_y+=32;
}
static void peripheral_canvas(void) {
    if(concept==5){
        value_row("Pending IRQs",pending);value_row("Handled timer events",irq_count);
        text(&body,0,cursor_y-scroll,irq_mask?"CPU IF=0 / MASKED":"CPU IF=1 / delivery enabled",irq_mask?GOLD:TEAL,PAPER,2);cursor_y+=34;
        text(&body,0,cursor_y-scroll,"Saved return RIP",INK,PAPER,1);hex(&body,150,cursor_y-scroll,saved_rip,TEAL,8);cursor_y+=35;
        const char *const phases[]={"Executing application","CPU frame saved","General registers saved","Handler counted event","EOI acknowledged"};
        heading(phases[irq_phase]);scale=1;paragraph("K changes pending state; handler execution changes the count; return restores saved state. Masking delays entry, not an already running handler. IRQ acknowledgement and return are different operations.",MUTED);scale=2;
    }else if(concept==6){
        const char *const services[]={"WRITE / 8 bytes","READFILE / 8 bytes","GETPID / PID 11"};heading(services[call_kind]);
        int y=cursor_y-scroll;unsigned w=body.width/2-12;
        gfx_rect(&body,0,y,w,90,call_phase?SOFT:TEAL);gfx_rect(&body,w+24,y,w,90,call_phase?TEAL:SOFT);
        text(&body,12,y+16,"USER / Ring 3",call_phase?INK:0xffffff,call_phase?SOFT:TEAL,2);
        text(&body,w+36,y+16,"KERNEL / Ring 0",call_phase?0xffffff:INK,call_phase?TEAL:SOFT,2);
        text(&body,12,y+56,pointer_bad?"pointer INVALID":"pointer valid",call_phase?INK:0xffffff,call_phase?SOFT:TEAL,1);
        text(&body,w+36,y+56,call_phase==1?"validate pending":call_phase==2?"service permitted":call_phase==3?"return pending":"no active call",call_phase?0xffffff:INK,call_phase?TEAL:SOFT,1);
        cursor_y+=112;value_row("Completed calls",call_count);value_row("Copied bytes",call_bytes);
        text(&body,0,cursor_y-scroll,"RAX result",INK,PAPER,1);if(call_result<0)text(&body,210,cursor_y-scroll,"-2 / EFAULT",RED,PAPER,2);else num(&body,210,cursor_y-scroll,call_result,TEAL,PAPER,2);cursor_y+=34;
    }else if(concept==7){
        heading("Directory records / model disk");
        const char *const files[]={"hello.txt / 31 bytes / start sector 3","empty.txt / 0 bytes / no extent","missing.txt / no directory record"};
        for(unsigned i=0;i<3;i++){int y=cursor_y-scroll;if(i==file_sel)gfx_rect(&body,0,y-3,body.width,26,SOFT);text(&body,8,y,files[i],i==file_sel?TEAL:INK,i==file_sel?SOFT:PAPER,1);cursor_y+=32;}
        value_row("Offset",file_offset);value_row("Buffer capacity",file_length);value_row("Returned bytes",file_bytes);
        heading("Destination buffer");paragraph(file_bytes?file_buffer:"(empty: no bytes copied)",INK);
        scale=1;paragraph("Metadata -> sector = extent + offset/512; byte within sector = offset%512. The model uses one extent and a 32-byte destination; real EDUFS/ATA remains available through cat and run view in Tools.",MUTED);scale=2;
    }else if(concept==8){
        if(!race_mode){value_row("Shared x",shared);for(unsigned i=0;i<2;i++){int y=cursor_y-scroll;uint32_t bg=i==last_actor?SOFT:PAPER;gfx_rect(&body,0,y-4,body.width,55,bg);text(&body,8,y,"Task",INK,bg,2);num(&body,74,y,i+1,TEAL,bg,2);
                text(&body,122,y,phase[i]==0?"next: READ":phase[i]==1?"next: ADD":phase[i]==2?"next: STORE":"DONE",INK,bg,2);text(&body,8,y+30,"local register",INK,bg,1);num(&body,122,y+30,local[i],TEAL,bg,1);cursor_y+=65;}
            value_row("Lock owner (0 = free)",owner[0]);scale=1;paragraph(lock_order?"Lock covers READ + ADD + STORE. A peer cannot read the old value while the owner is in the critical section.":"Try 1,2,1,2,1,2: both read 0; both add locally; both store 1. Then R and try 1,1,1,2,2,2 to get 2 without a lock.",MUTED);scale=2;
        }else{
            heading(lock_order?"Global resource order A then B":"Opposing resource order / cycle possible");
            int y=cursor_y-scroll;unsigned x=body.width/2;
            for(unsigned i=0;i<2;i++){int yy=y+i*120;gfx_rect(&body,0,yy,130,70,SOFT);text(&body,10,yy+16,i?"Task 2":"Task 1",INK,SOFT,2);text(&body,10,yy+48,phase[i]==3?"finished":wait_req[i]?"waiting":"ready to act",INK,SOFT,1);
                gfx_rect(&body,x,yy,160,70,SOFT);text(&body,x+10,yy+10,i?"Resource B":"Resource A",INK,SOFT,1);text(&body,x+10,yy+38,"owner",INK,SOFT,1);num(&body,x+55,yy+35,owner[i],TEAL,SOFT,2);}
            for(unsigned resource=0;resource<2;resource++)if(owner[resource])line(x,y+resource*120+35,130,y+(owner[resource]-1)*120+35,TEAL);
            for(unsigned t=0;t<2;t++)if(wait_req[t]&&phase[t]<2){unsigned target=lock_order?phase[t]:t?1-phase[t]:phase[t];line(130,y+t*120+50,x,y+target*120+50,RED);}
            cursor_y+=220;scale=1;paragraph("Teal: resource held by task. Red: task requests held resource. A full cycle means neither can progress. L restarts with consistent order; F releases the selected task's resources as modeled recovery.",MUTED);scale=2;
        }
    }else if(concept==9){
        unsigned w=body.width/8;int y=-scroll;
        for(unsigned i=0;i<8;i++){unsigned occupied=0;for(unsigned j=0;j<key_count;j++)if((key_tail+j)%8==i)occupied=1;gfx_rect(&body,i*w,y,w-5,78,occupied?TEAL:SOFT);num(&body,i*w+8,y+8,i,occupied?0xffffff:INK,occupied?TEAL:SOFT,1);text(&body,i*w+8,y+38,occupied?"A":"-",occupied?0xffffff:INK,occupied?TEAL:SOFT,2);}
        cursor_y=100;value_row("Queued keys",key_count);value_row("Dropped events",key_drops);value_row("Head / next insertion",key_head);value_row("Tail / next consumption",key_tail);
        text(&body,0,cursor_y-scroll,last_key?"Last consumed: A":"No key consumed",TEAL,PAPER,2);cursor_y+=32;
    }else if(concept==10){
        unsigned cell=body.width/12;if(cell>38)cell=38;
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<12;x++){int xx=x*cell,yy=y*cell-scroll;gfx_rect(&body,xx,yy,cell-2,cell-2,palette[pixels[y][x]]);if(x==pixel_x&&y==pixel_y){gfx_rect(&body,xx,yy,cell-2,2,INK);gfx_rect(&body,xx,yy,2,cell-2,INK);}}
        cursor_y=8*cell+16;value_row("Cursor X",pixel_x);value_row("Cursor Y",pixel_y);value_row("Color index",pixel_color);
    }else{
        const char *const foundation[]={"Application request","Trust boundary","Kernel service","Filesystem abstraction","Device access"};
        const char *const boot[]={"Firmware","Limine / ELF","Physical allocator","Paging and heap","Interrupts / scheduling","Explorer"};
        const char *const cpu[]={"MOV RAX,2","ADD RAX,3","CALL helper","PUSH RAX","POP RBX","RET"};
        const char *const debug[]={"Observe launch","Check lifetime","Collect snapshot","Finish rendering","Inspect pixels","Assert evidence"};
        const char *const *stages=concept==11?foundation:concept==12?boot:concept==13?cpu:debug;unsigned n=concept==11?5:6;
        for(unsigned i=0;i<n;i++){int y=cursor_y-scroll;uint32_t bg=i==tick_step?TEAL:SOFT;gfx_rect(&body,0,y,body.width,42,bg);num(&body,10,y+10,i+1,i==tick_step?0xffffff:INK,bg,1);text(&body,38,y+10,stages[i],i==tick_step?0xffffff:INK,bg,2);cursor_y+=52;}
        if(concept==13){value_row("RAX",tick_step?5:2);value_row("RBX",tick_step>=4?5:0);scale=1;paragraph(tick_step<2?"Stack empty before call.":tick_step==2?"Stack: saved return address.":tick_step==3?"Stack: value 5 above return address.":tick_step==4?"POP restored value into RBX; return address remains.":"RET restored the instruction pointer; stack empty.",MUTED);scale=2;}
    }
}
static const char *controls(void) {
    switch(concept){
        case 0:return "1..8 select slot | C child | X exit(7) | S run | B block | U wake | W parent wait | Z collect zombie | R reset";
        case 1:return "1..4 select | N add | +/- burst | A arrival | P priority | G policy | Q quantum | B block | W wake | Space tick | X run/pause | R reset";
        case 2:return "Left/Right select frame | A allocate | F free selected | R reset";
        case 3:return "1..4 address | M map | U unmap | P writable permission | W read/write | Space walk | R reset";
        case 4:return "Left/Right select block | +/- payload bytes | A allocate | F free selected | R reset";
        case 5:return "K inject IRQ | M mask/unmask | Space entry/save/handle/EOI/return | R reset";
        case 6:return "1 write / 2 readfile / 3 getpid | P pointer validity | C call | Space validate/execute/return | R reset";
        case 7:return "1 text / 2 empty / 3 missing | O offset +8 | L capacity 8/16/32 | D read | R reset";
        case 8:return "1/2 advance chosen task | L locking / global order (restart) | D counter/deadlock (restart) | F recover selected owner | R reset";
        case 9:return "K enqueue modeled A key | Space consume | R reset";
        case 10:return "Arrows move cursor | G color | Space pixel | F rectangle | R reset";
        default:return "Space advance supporting trace | B back | R reset | Main experiments are available from Esc";
    }
}
static void explanation(void) {
    const struct edu_lesson *l=&edu_lessons[concepts[concept]];
    if(overlay==1){heading("Algorithm / data structures");paragraph(l->mechanism,INK);heading("Why it exists");paragraph(l->problem,INK);heading("Worked example");paragraph(l->example,INK);heading("Edges and limitations");paragraph(l->edge,GOLD);heading("Alternative approaches");paragraph(l->alternative,INK);}
    else if(overlay==2){heading("SOURCE-VERIFIED / real kernel");paragraph(l->implementation,INK);heading("Implementation anchors");paragraph(l->source,MUTED);paragraph("This optional view describes actual mechanisms. Explorer tasks, frame bits and process trees are teaching models, not live state. Tab opens Tools for the real shell and sampled kernel inspector.",TEAL);}
    else{heading("Optional reasoning prompt / no score or progress gate");paragraph(l->question,INK);for(unsigned i=0;i<3;i++){char b[]="1.";b[0]+=i;paragraph(b,TEAL);paragraph(l->answers[i],INK);}if(answer){heading(answer==l->correct+1?"Reasoning confirmed":"Consider the mechanism");paragraph(l->feedback,INK);}}
}
static void explorer_page(void) {
    main_x=screen->width>=1000?180:20;
    if(main_x>20){gfx_rect(screen,0,48,156,screen->height-78,NAVY);for(unsigned i=0;i<15;i++){unsigned y=72+i*24;if(y+10>screen->height-40)break;text(screen,12,y,names[i],i==concept?0x7dd8ce:0xdce8f5,NAVY,1);}}
    text(screen,main_x,64,names[concept],INK,PAPER,2);
    text(screen,main_x,94,overlay?"OPTIONAL INSPECTION / Esc returns to the unchanged experiment":"TEACHING MODEL / safe isolated state; not live kernel data",TEAL,PAPER,1);
    unsigned large=screen->width>=1000&&screen->height>=650;
    unsigned canvas_top=116,context_top=screen->height-(large?232:188),control_top=screen->height-98;
    body=region(main_x,canvas_top,screen->width-main_x-24,context_top-canvas_top-12);
    canvas_height=body.height;
    scale=2;cursor_y=0;
    if(overlay)explanation();else if(concept==0)process_canvas();else if(concept==1)scheduling_canvas();else if(concept<=4)memory_canvas();else peripheral_canvas();
    total_height=cursor_y;int limit=total_height>(int)body.height?total_height-body.height:0;
    if(scroll>limit){scroll=limit;dirty=1;}
    int saved_scroll=scroll;
    body=region(main_x,context_top,screen->width-main_x-24,large?120:80);cursor_y=scroll=0;scale=1;
    heading("WHAT CHANGED / WHAT IT MEANS");scale=large?2:1;paragraph(messages[concept],INK);
    body=region(main_x,control_top,screen->width-main_x-24,62);cursor_y=0;scale=1;
    heading("CONTROLS (active only in the experiment; Esc closes an explanation)");paragraph(controls(),MUTED);
    scroll=saved_scroll;
}
void education_render(void) {
    if(!screen)return;
    while(tail!=head){unsigned char k=events[tail];tail=(tail+1)%64;event(k);}
    if(!tools&&!home&&!overlay&&concept==1&&auto_run&&timer_get_ticks()>=next_run){next_run=timer_get_ticks()+20;scheduling_action(' ');dirty=1;}
    if(tools||!dirty)return;
    dirty=0;chrome();if(home)home_page();else explorer_page();
    serial_write("[EXPLORER] concept=");serial_write_dec(concept);serial_write(" overlay=");serial_write_dec(overlay);serial_write(home?" home\r\n":" experiment\r\n");
}
