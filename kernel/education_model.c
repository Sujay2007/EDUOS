#include "education_model.h"
void edu_schedule_init(struct edu_schedule *s, const unsigned *arrival, const unsigned *burst, unsigned count, unsigned algorithm) {
    s->count = count > EDU_TASKS ? EDU_TASKS : count;
    s->algorithm = algorithm % 4; s->time = s->slice = 0; s->quantum = 2; s->current = EDU_TASKS;
    for (unsigned i = 0; i < s->count; i++) {
        s->task[i].arrival = arrival[i] > 8 ? 8 : arrival[i];
        s->task[i].burst = !burst[i] ? 1 : burst[i] > 8 ? 8 : burst[i];
        s->task[i].priority=i+1; s->task[i].blocked=s->task[i].blocked_ticks=0;
        s->task[i].ran = s->task[i].finish = 0; s->task[i].first = UINT32_MAX;
    }
    for (unsigned i = 0; i < EDU_TIMELINE; i++) s->timeline[i] = -1;
}
static int ready(const struct edu_schedule *s, unsigned i) {
    return i < s->count && s->task[i].arrival <= s->time && s->task[i].ran < s->task[i].burst && !s->task[i].blocked;
}
int edu_schedule_step(struct edu_schedule *s) {
    if (s->time >= EDU_TIMELINE) return 0;
    unsigned unfinished = 0;
    for (unsigned i = 0; i < s->count; i++) unfinished += s->task[i].ran < s->task[i].burst;
    if (!unfinished) return 0;
    unsigned next = s->current;
    if (!ready(s, next) || (s->algorithm == 2 && s->slice >= s->quantum)) {
        unsigned previous = next; next = EDU_TASKS;
        for (unsigned k = 0; k < s->count; k++) {
            unsigned i = s->algorithm == 2 ? (previous == EDU_TASKS ? k : (previous + 1 + k) % s->count) : k;
            if (!ready(s, i)) continue;
            if (next == EDU_TASKS || (s->algorithm == 1 && s->task[i].burst < s->task[next].burst) ||
                (s->algorithm == 0 && s->task[i].arrival < s->task[next].arrival) ||
                (s->algorithm == 3 && s->task[i].priority < s->task[next].priority)) next = i;
            if (s->algorithm == 2) break;
        }
        s->slice = 0;
    }
    s->current = next;
    s->timeline[s->time] = next == EDU_TASKS ? -1 : (int)next;
    if (next < s->count) {
        struct edu_task *t = &s->task[next];
        if (!t->ran) t->first = s->time;
        t->ran++; s->slice++;
        if (t->ran == t->burst) t->finish = s->time + 1;
    }
    for(unsigned i=0;i<s->count;i++)
        if(s->task[i].blocked && s->task[i].arrival<=s->time && s->task[i].ran<s->task[i].burst) s->task[i].blocked_ticks++;
    s->time++; return 1;
}
void edu_heap_init(struct edu_heap *h) { h->count = 1; h->blocks[0] = (struct edu_block){0,256,0}; }
int edu_heap_alloc(struct edu_heap *h, unsigned bytes) {
    if (!bytes || bytes > 256) return -1;
    bytes = (bytes + 15) & ~15U;
    for (unsigned i = 0; i < h->count; i++) {
        struct edu_block *b = &h->blocks[i];
        if (b->used || b->size < bytes + 16) continue;
        unsigned total = bytes + 16;
        if (b->size >= total + 32 && h->count < 16) {
            for (unsigned j = h->count; j > i + 1; j--) h->blocks[j] = h->blocks[j-1];
            h->blocks[i+1] = (struct edu_block){b->start + total,b->size-total,0};
            h->count++; b->size = total;
        }
        b->used = 1; return (int)i;
    }
    return -1;
}
int edu_heap_free(struct edu_heap *h, unsigned index) {
    if (index >= h->count || !h->blocks[index].used) return -1;
    h->blocks[index].used = 0;
    for (unsigned i = 0; i + 1 < h->count;) {
        if (!h->blocks[i].used && !h->blocks[i+1].used) {
            h->blocks[i].size += h->blocks[i+1].size;
            for (unsigned j = i+1; j+1 < h->count; j++) h->blocks[j] = h->blocks[j+1];
            h->count--;
        } else i++;
    }
    return 0;
}
unsigned edu_page_index(uint64_t address, unsigned level) { return level < 4 ? (address >> (39 - level * 9)) & 511 : 0; }
int edu_canonical(uint64_t address) { return (address >> 47) == 0 || (address >> 47) == 0x1ffff; }

int edu_schedule_block(struct edu_schedule *s,unsigned task,int blocked) {
    if(task>=s->count || s->task[task].arrival>s->time || s->task[task].ran==s->task[task].burst) return -1;
    s->task[task].blocked=blocked!=0;return 0;
}
void edu_tree_init(struct edu_tree *t) {
    for(unsigned i=0;i<EDU_PROCESSES;i++) t->p[i]=(struct edu_process){0};
    t->p[0]=(struct edu_process){.pid=1,.state=EDU_READY,.pages=1};
    t->p[1]=(struct edu_process){.pid=10,.ppid=1,.state=EDU_RUNNING,.pages=3};
    t->next_pid=11;t->last_status=t->reaped=0;
}
static int find_pid(const struct edu_tree *t,unsigned pid) {
    for(unsigned i=0;i<EDU_PROCESSES;i++) if(t->p[i].state && t->p[i].pid==pid)return i;
    return -1;
}
int edu_process_create(struct edu_tree *t,unsigned parent) {
    if(parent>=EDU_PROCESSES || !t->p[parent].state || t->p[parent].state==EDU_ZOMBIE) return -1;
    for(unsigned i=1;i<EDU_PROCESSES;i++) if(!t->p[i].state) {
        if(t->next_pid==UINT32_MAX) return -1;
        t->p[i]=(struct edu_process){.pid=t->next_pid++,.ppid=t->p[parent].pid,.state=EDU_READY,.pages=3};return i;
    }
    return -1;
}
int edu_process_reap(struct edu_tree *t,unsigned slot) {
    if(!slot || slot>=EDU_PROCESSES || t->p[slot].state!=EDU_ZOMBIE) return -1;
    int parent=find_pid(t,t->p[slot].ppid);
    if(parent<0) return -1;
    t->last_status=t->p[slot].status;t->reaped++;
    t->p[slot]=(struct edu_process){0};
    if(t->p[parent].waiting) {t->p[parent].waiting=0;t->p[parent].state=EDU_READY;}
    return parent;
}
int edu_process_exit(struct edu_tree *t,unsigned slot,unsigned status) {
    if(!slot || slot>=EDU_PROCESSES || !t->p[slot].state || t->p[slot].state==EDU_ZOMBIE)return -1;
    unsigned pid=t->p[slot].pid,ppid=t->p[slot].ppid;
    t->p[slot].state=EDU_ZOMBIE;t->p[slot].status=status;t->p[slot].pages=0;t->p[slot].waiting=0;
    for(unsigned i=1;i<EDU_PROCESSES;i++) if(t->p[i].state && t->p[i].ppid==pid) {
        t->p[i].ppid=1;t->p[i].adopted=t->p[i].state!=EDU_ZOMBIE;
    }
    int parent=find_pid(t,ppid);
    if(parent>=0 && t->p[parent].waiting) edu_process_reap(t,slot);
    return 0;
}
int edu_process_wait(struct edu_tree *t,unsigned parent) {
    if(parent>=EDU_PROCESSES || !t->p[parent].state || t->p[parent].state==EDU_ZOMBIE)return -1;
    unsigned children=0;
    for(unsigned i=1;i<EDU_PROCESSES;i++) if(t->p[i].state && t->p[i].ppid==t->p[parent].pid) {
        children++;
        if(t->p[i].state==EDU_ZOMBIE) {edu_process_reap(t,i);return 1;}
    }
    if(!children)return -1;
    t->p[parent].waiting=1;t->p[parent].state=EDU_BLOCKED;return 0;
}
int edu_process_state(struct edu_tree *t,unsigned slot,unsigned state) {
    if(slot>=EDU_PROCESSES || !t->p[slot].state || t->p[slot].state==EDU_ZOMBIE || state<EDU_READY || state>EDU_BLOCKED)return -1;
    if(state==EDU_RUNNING)for(unsigned i=0;i<EDU_PROCESSES;i++)if(t->p[i].state==EDU_RUNNING)t->p[i].state=EDU_READY;
    t->p[slot].state=state;t->p[slot].waiting=0;return 0;
}
int edu_tree_valid(const struct edu_tree *t) {
    if(t->p[0].pid!=1 || !t->p[0].state || t->p[0].state==EDU_ZOMBIE)return 0;
    unsigned running=0;
    for(unsigned i=0;i<EDU_PROCESSES;i++) if(t->p[i].state) {
        if(t->p[i].state>EDU_ZOMBIE || (t->p[i].state==EDU_ZOMBIE && t->p[i].pages))return 0;
        running+=t->p[i].state==EDU_RUNNING;
        for(unsigned j=i+1;j<EDU_PROCESSES;j++)if(t->p[j].state && t->p[j].pid==t->p[i].pid)return 0;
        unsigned p=i;
        for(unsigned n=0;n<EDU_PROCESSES && p;n++) {
            int next=find_pid(t,t->p[p].ppid);if(next<0 || (unsigned)next==i)return 0;p=next;
            if(n==EDU_PROCESSES-1 && p)return 0;
        }
    }
    return running<=1;
}
void edu_frames_init(struct edu_frames *f) {f->reserved=0x8003;f->used=f->reserved;f->cursor=0;}
int edu_frame_alloc(struct edu_frames *f) {
    for(unsigned n=0;n<16;n++) {unsigned i=(f->cursor+n)%16;if(!(f->used&(1U<<i))) {f->used|=1U<<i;f->cursor=(i+1)%16;return i;}}
    return -1;
}
int edu_frame_free(struct edu_frames *f,unsigned index) {
    if(index>=16 || (f->reserved&(1U<<index)) || !(f->used&(1U<<index)))return -1;
    f->used&=~(1U<<index);if(index<f->cursor)f->cursor=index;return 0;
}
void edu_vm_init(struct edu_vm *v) {
    v->entry[0]=(struct edu_pte){1,1,9};v->entry[1]=(struct edu_pte){0,1,10};v->entry[2]=(struct edu_pte){1,0,11};
}
uint64_t edu_vm_address(unsigned choice) {return choice==0?0x400123:choice==1?0x600123:choice==2?0x401abc:0x800000000000ULL;}
int edu_vm_translate(const struct edu_vm *v,unsigned choice,int write,uint64_t *physical,unsigned *stop) {
    *physical=0;
    if(choice>=3) {*stop=0;return -1;}
    if(!v->entry[choice].present) {*stop=choice==1?2:3;return -2;}
    if(write && !v->entry[choice].writable){*stop=3;return -3;}
    *stop=4;*physical=(uint64_t)v->entry[choice].frame*4096+(edu_vm_address(choice)&4095);return 0;
}
