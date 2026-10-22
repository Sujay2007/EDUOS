/* Include the UI only here to verify private model state without kernel test APIs. */
#include "../kernel/education.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char fbmem[1280*800*4+32], snapshot[1280*800*4];
static uint64_t clock_ticks;
void serial_write(const char *s){(void)s;}
void serial_write_dec(uint64_t n){(void)n;}
uint64_t timer_get_ticks(void){return clock_ticks;}
static void key(unsigned char k){assert(education_input(k,0));education_render();}
static uint64_t hash_bytes(uint64_t h,const void *p,unsigned n){const unsigned char *b=p;while(n--)h=(h^*b++)*1099511628211ULL;return h;}
#define HASH(v) h=hash_bytes(h,&(v),sizeof(v))
static uint64_t model_hash(void){uint64_t h=1469598103934665603ULL;
    HASH(concept);HASH(answer);HASH(tick_step);HASH(messages);HASH(note_used);
    HASH(tree);HASH(frames);HASH(vm);HASH(heap);HASH(schedule);
    HASH(proc_sel);HASH(frame_sel);HASH(vm_sel);HASH(heap_sel);HASH(task_sel);HASH(request);HASH(write_access);HASH(walk);
    HASH(task_count);HASH(policy);HASH(quantum);HASH(auto_run);HASH(arrivals);HASH(bursts);HASH(priorities);
    HASH(irq_mask);HASH(pending);HASH(irq_phase);HASH(irq_count);HASH(saved_rip);
    HASH(call_phase);HASH(call_kind);HASH(pointer_bad);HASH(call_count);HASH(call_bytes);HASH(call_result);
    HASH(file_sel);HASH(file_offset);HASH(file_length);HASH(file_bytes);HASH(file_buffer);
    HASH(race_mode);HASH(lock_order);HASH(owner);HASH(local);HASH(phase);HASH(wait_req);HASH(shared);HASH(last_actor);
    HASH(key_head);HASH(key_tail);HASH(key_count);HASH(key_drops);HASH(last_key);HASH(key_ring);
    HASH(pixel_x);HASH(pixel_y);HASH(pixel_color);HASH(pixels);return h;
}
int main(void){unsigned widths[]={640,1024,1280},heights[]={400,768,800};
    for(unsigned d=0;d<3;d++){
        unsigned size=widths[d]*heights[d]*4;memset(fbmem,0xa5,sizeof(fbmem));
        struct gfx_surface fb={.pixels=fbmem+16,.width=widths[d],.height=heights[d],.pitch=widths[d]*4,.bytes_per_pixel=4,.sizes={8,8,8},.shifts={16,8,0}};
        education_attach(&fb);education_render();
        for(unsigned i=0;i<EXPLORER_HELP_COUNT;i++){
            assert(explorer_help[i].start[0]&&explorer_help[i].details[0]&&explorer_help[i].steps[0]&&explorer_help[i].observe[0]&&explorer_help[i].why[0]&&explorer_help[i].change[0]&&explorer_help[i].kernel[0]);
            key('\n');key('r');key('c');key('a');key(' ');key(KEY_END);education_render();
            uint64_t before=model_hash();int old_scroll=scroll;unsigned old_overlay=overlay;
            memcpy(snapshot,fbmem+16,size);key('h');assert(overlay==HELP_OVERLAY&&scroll==0&&total_height>(int)canvas_height);
            key(KEY_PAGE_DOWN);assert(scroll>0);key(KEY_DOWN);key(KEY_UP);key(KEY_HOME);assert(!scroll);key(KEY_END);assert(scroll==total_height-(int)canvas_height);key(' ');
            const unsigned char ignored[]={'r','c','x','a','w','s','f','1','2','e','i','?'};
            for(unsigned j=0;j<sizeof(ignored);j++)key(ignored[j]);
            assert(model_hash()==before&&overlay==HELP_OVERLAY);
            key('\t');assert(!education_active());key('\t');assert(education_active()&&overlay==HELP_OVERLAY);
            key(KEY_ESC);assert(model_hash()==before&&overlay==old_overlay&&scroll==old_scroll);assert(!memcmp(snapshot,fbmem+16,size));
            key('e');key(KEY_END);education_render();old_scroll=scroll;memcpy(snapshot,fbmem+16,size);
            key('h');key(KEY_PAGE_DOWN);key('h');assert(overlay==1&&scroll==old_scroll&&model_hash()==before);assert(!memcmp(snapshot,fbmem+16,size));
            key(KEY_ESC);key('r');key(KEY_ESC);key(KEY_DOWN);
        }
        for(unsigned j=0;j<16;j++)assert(fbmem[j]==0xa5&&fbmem[16+size+j]==0xa5);
    }
    /* Animation is held in help and resumes at its remaining delay, no catch-up. */
    key('\n');key(KEY_ESC); /* Home selection wraps to Processes after the loop. */
    concept=1;home=0;overlay=0;reset_model();clock_ticks=10;key('x');
    uint64_t before=model_hash(),due=next_run;key('h');clock_ticks+=1000;education_render();assert(model_hash()==before);
    key('h');assert(auto_run&&model_hash()==before&&next_run==clock_ticks+(due-10));
    clock_ticks=next_run-1;education_render();assert(model_hash()==before);
    clock_ticks++;education_render();assert(model_hash()!=before&&schedule.time==1);
    puts("Context help PASS: all 15 mechanisms, isolated keys, independent scrolling, exact view/model restoration, Tools toggle, animation hold/resume and 640/1024/1280 clipping");
}
