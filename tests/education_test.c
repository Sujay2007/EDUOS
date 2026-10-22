#include "education_model.h"
#include "education.h"
#include "lessons.h"
#include "keyboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char pixels[1280*800*4+32];
void serial_write(const char *s) {(void)s;}
void serial_write_dec(uint64_t n) {(void)n;}
uint64_t timer_get_ticks(void) {return 0;}
static void key(unsigned char k) {assert(education_input(k,0));education_render();}
int main(void) {
    struct edu_schedule s;unsigned a[]={0,0,0},b[]={3,2,1};
    edu_schedule_init(&s,a,b,3,2);
    int expected[]={0,0,1,1,2,0};
    for(unsigned i=0;i<6;i++){assert(edu_schedule_step(&s));assert(s.timeline[i]==expected[i]);}
    assert(!edu_schedule_step(&s));assert(s.task[0].finish==6 && s.task[1].finish==4 && s.task[2].first==4);
    edu_schedule_init(&s,a,b,3,1);while(edu_schedule_step(&s)){}
    assert(s.task[2].finish==1 && s.task[1].finish==3 && s.task[0].finish==6);
    edu_schedule_init(&s,a,b,3,0);while(edu_schedule_step(&s)){}
    assert(s.task[0].finish==3 && s.task[1].finish==5 && s.task[2].finish==6);
    unsigned late[]={8},one[]={1};edu_schedule_init(&s,late,one,1,0);
    for(unsigned i=0;i<8;i++){assert(edu_schedule_step(&s));assert(s.timeline[i]==-1);}
    assert(edu_schedule_step(&s) && s.task[0].finish==9);
    struct edu_heap h;edu_heap_init(&h);assert(edu_heap_alloc(&h,0)<0);
    assert(edu_heap_alloc(&h,17)==0 && h.blocks[0].size==48);
    assert(edu_heap_alloc(&h,16)==1 && h.count==3);
    assert(edu_heap_free(&h,0)==0 && edu_heap_free(&h,0)<0);
    assert(edu_heap_free(&h,1)==0 && h.count==1 && h.blocks[0].size==256);
    assert(edu_heap_alloc(&h,240)==0 && edu_heap_alloc(&h,1)<0);
    assert(edu_page_index(0x400123,0)==0 && edu_page_index(0x400123,2)==2 && edu_page_index(0x400123,3)==0);
    assert(edu_canonical(0xffff800000000000ULL) && !edu_canonical(0x800000000000ULL));
    for(unsigned i=0;i<LESSON_COUNT;i++) {
        const struct edu_lesson *l=&edu_lessons[i];
        assert(strlen(l->problem)>100 && strlen(l->mechanism)>100 && strlen(l->implementation)>100);
        assert(l->source[0] && l->correct<3 && l->steps && l->feedback[0]);
    }
    struct edu_tree tree;edu_tree_init(&tree);assert(edu_tree_valid(&tree));
    int c=edu_process_create(&tree,1),d=edu_process_create(&tree,1);
    assert(c==2 && d==3 && tree.p[c].ppid==10 && tree.p[c].state==EDU_READY);
    assert(!edu_process_exit(&tree,c,7) && tree.p[c].state==EDU_ZOMBIE && !tree.p[c].pages);
    assert(edu_process_wait(&tree,1)==1 && !tree.p[c].state && tree.last_status==7 && tree.reaped==1);
    assert(!edu_process_exit(&tree,1,9) && tree.p[d].ppid==1 && tree.p[d].adopted && tree.p[d].state==EDU_READY);
    assert(edu_tree_valid(&tree));
    assert(edu_process_reap(&tree,1)==0 && !tree.p[1].state);
    assert(!edu_process_exit(&tree,d,42));assert(edu_process_wait(&tree,0)==1 && tree.last_status==42);
    assert(edu_process_exit(&tree,0,7)<0 && edu_process_wait(&tree,0)<0);
    edu_tree_init(&tree);c=edu_process_create(&tree,1);assert(!edu_process_wait(&tree,1));
    assert(tree.p[1].waiting && tree.p[1].state==EDU_BLOCKED);
    assert(!edu_process_exit(&tree,c,12) && !tree.p[c].state && tree.p[1].state==EDU_READY && tree.last_status==12);
    for(unsigned i=0;i<6;i++)assert(edu_process_create(&tree,1)>0);
    assert(edu_process_create(&tree,1)<0 && edu_tree_valid(&tree));
    edu_tree_init(&tree);c=edu_process_create(&tree,1);d=edu_process_create(&tree,c);
    assert(!edu_process_exit(&tree,c,7) && tree.p[d].ppid==1 && edu_tree_valid(&tree));
    assert(edu_process_state(&tree,c,EDU_RUNNING)<0);
    assert(!edu_process_exit(&tree,1,7) && tree.p[c].ppid==1); /* already-zombie adoption */
    assert(edu_tree_valid(&tree));
    struct edu_frames frames;edu_frames_init(&frames);
    for(unsigned i=0;i<13;i++)assert(edu_frame_alloc(&frames)>=2);
    assert(edu_frame_alloc(&frames)<0 && edu_frame_free(&frames,0)<0);
    assert(!edu_frame_free(&frames,3) && edu_frame_alloc(&frames)==3 && edu_frame_free(&frames,16)<0);
    struct edu_vm vm;edu_vm_init(&vm);uint64_t physical;unsigned stop;
    assert(!edu_vm_translate(&vm,0,0,&physical,&stop) && physical==0x9123 && stop==4);
    assert(edu_vm_translate(&vm,1,0,&physical,&stop)==-2 && stop==2 && !physical);
    vm.entry[1].present=1;assert(!edu_vm_translate(&vm,1,0,&physical,&stop) && physical==0xa123);
    vm.entry[0].present=0;assert(edu_vm_translate(&vm,0,0,&physical,&stop)==-2 && stop==3);
    assert(edu_vm_translate(&vm,2,1,&physical,&stop)==-3);
    assert(edu_vm_translate(&vm,3,0,&physical,&stop)==-1);
    edu_schedule_init(&s,a,b,3,3);s.task[0].priority=3;s.task[1].priority=2;s.task[2].priority=1;
    assert(edu_schedule_step(&s) && s.timeline[0]==2);while(edu_schedule_step(&s)){}
    assert(s.task[2].finish==1 && s.task[1].finish==3 && s.task[0].finish==6);
    edu_schedule_init(&s,a,b,3,2);s.quantum=1;
    int q1[]={0,1,2,0,1,0};for(unsigned i=0;i<6;i++){assert(edu_schedule_step(&s));assert(s.timeline[i]==q1[i]);}
    unsigned burst[]={2},now[]={0};edu_schedule_init(&s,now,burst,1,0);
    assert(!edu_schedule_block(&s,0,1));assert(edu_schedule_step(&s) && s.timeline[0]==-1);
    assert(!edu_schedule_block(&s,0,0));assert(edu_schedule_step(&s) && edu_schedule_step(&s));
    assert(s.task[0].finish==3 && s.task[0].first==1 && s.task[0].blocked_ticks==1);
    assert(s.task[0].finish-s.task[0].arrival-s.task[0].burst-s.task[0].blocked_ticks==0);
    unsigned widths[]={640,1024,1280},heights[]={400,768,800};
    for(unsigned d=0;d<3;d++) {
        unsigned size=widths[d]*heights[d]*4;
        memset(pixels,0xa5,sizeof(pixels));
        struct gfx_surface fb={.pixels=pixels+16,.width=widths[d],.height=heights[d],.pitch=widths[d]*4,.bytes_per_pixel=4,.sizes={8,8,8},.shifts={16,8,0}};
        education_attach(&fb);education_render();assert(education_active());assert(!education_input('x',1));
        for(unsigned i=0;i<15;i++) {
            key('\n');
            const unsigned char keys[]={'c','c','3','x','2','w','4','s','b','u','a','f','m','p','w',' ','g','q','n','k','d','1','2','1','2','f','r'};
            for(unsigned k=0;k<sizeof(keys);k++)key(keys[k]);
            key(KEY_END);key(KEY_HOME);key(KEY_PAGE_DOWN);key(KEY_PAGE_UP);
            key('e');key(KEY_END);key(KEY_HOME);key(KEY_ESC);
            key('i');key(KEY_ESC);key('?');key('1');key(KEY_ESC);
            key(KEY_ESC);key(KEY_DOWN);
        }
        key('\t');assert(!education_active());key('\t');assert(education_active());
        for(unsigned i=0;i<16;i++)assert(pixels[i]==0xa5 && pixels[16+size+i]==0xa5);
    }
    puts("Explorer PASS: arbitrary process trees, wait/reap/adoption/cleanup/capacity, four policies, quantum/block metrics, frames, mutable mappings, heap and all 15 explorer/inspection views with 640/1024/1280 clipping guards");
}
