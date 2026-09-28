// Conservative SRAM model with fixed allocator and runtime overhead.
#include "sram_budget.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_calloc(size_t,size_t);
extern "C" void *__real_realloc(void *,size_t);
extern "C" void __real_free(void *);
namespace {
alignas(16) unsigned char arena[65536];
struct Block {size_t start,span; int next; bool used;};
Block blocks[256];
bool enabled;
size_t limit,reservation,used,peak;
unsigned failures;
int spare() {for(int i=1;i<256;++i) if(blocks[i].next==-2) return i; return -1;}
bool inside(void *p) {auto a=reinterpret_cast<uintptr_t>(p); auto b=reinterpret_cast<uintptr_t>(arena); return a>=b && a<b+sizeof(arena);}
int find(void *p) {for(int i=0;i>=0;i=blocks[i].next) if(blocks[i].used && arena+blocks[i].start+16==p) return i; return -1;}
}
void sram_budget_begin(size_t capacity,size_t reserved) {
    assert(!enabled && capacity<=sizeof(arena) && reserved<capacity && !(reserved%16));
    for(auto &b:blocks) b={0,0,-2,false};
    limit=capacity; reservation=reserved; used=peak=reserved; failures=0;
    blocks[0]={reserved,capacity-reserved,-1,false}; enabled=true;
}
size_t sram_budget_peak() {return peak;}
unsigned sram_budget_failures() {return failures;}
void sram_budget_end() {
    assert(enabled && used==reservation && !blocks[0].used && blocks[0].next==-1);
    std::printf("SRAM MODEL: capacity=%zu reserved=%zu peak=%zu remaining_at_peak=%zu failures=%u\n",
        limit,reservation,peak,limit-peak,failures);
    enabled=false;
}
extern "C" void *__wrap_malloc(size_t n) {
    if(!enabled) return __real_malloc(n);
    if(n>sizeof(arena)-32) {++failures; return nullptr;}
    size_t span=((n+15)&~size_t(15))+16;
    for(int i=0;i>=0;i=blocks[i].next) {
        auto &b=blocks[i];
        if(b.used || b.span<span) continue;
        if(b.span-span>=32) {
            int j=spare(); assert(j>=0);
            blocks[j]={b.start+span,b.span-span,b.next,false}; b.next=j; b.span=span;
        }
        b.used=true; used+=b.span; if(used>peak) peak=used;
        return arena+b.start+16;
    }
    ++failures; return nullptr;
}
extern "C" void *__wrap_calloc(size_t n,size_t s) {
    if(!enabled) return __real_calloc(n,s);
    if(s && n>std::numeric_limits<size_t>::max()/s) {++failures; return nullptr;}
    void *p=__wrap_malloc(n*s); if(p) std::memset(p,0,n*s); return p;
}
extern "C" void __wrap_free(void *p) {
    if(!p) return;
    if(!inside(p)) {__real_free(p); return;}
    assert(enabled); int i=find(p); assert(i>=0);
    blocks[i].used=false; used-=blocks[i].span;
    for(int k=0;k>=0;) {
        int next=blocks[k].next;
        if(next>=0 && !blocks[k].used && !blocks[next].used) {
            blocks[k].span+=blocks[next].span; blocks[k].next=blocks[next].next;
            blocks[next].next=-2;
        } else k=next;
    }
}
extern "C" void *__wrap_realloc(void *p,size_t n) {
    if(!p) return __wrap_malloc(n);
    if(!inside(p)) return __real_realloc(p,n);
    if(!n) {__wrap_free(p); return nullptr;}
    int i=find(p); assert(i>=0);
    void *q=__wrap_malloc(n); if(!q) return nullptr;
    size_t old=blocks[i].span-16; std::memcpy(q,p,n<old?n:old); __wrap_free(p); return q;
}
