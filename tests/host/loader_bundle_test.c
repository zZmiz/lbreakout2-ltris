

#include "loader_reference/program_name.h"
#include "loader_reference/storage.h"
#include "loader_reference/uf2_format.h"
#include "loader_reference/boot_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
static FILE *input;
bool storage_open(const char *p) {input=fopen(p,"rb"); return input!=NULL;}
bool storage_seek(uint32_t off) {return fseek(input,off,SEEK_SET)==0;}
bool storage_read(void *p,uint32_t n,uint32_t *got) {*got=fread(p,1,n,input);return !ferror(input);}
void storage_close(void) {fclose(input);input=NULL;}

int main(int argc,char **argv) {
    assert(argc==2);
    char name[64];
    assert(program_name_from_uf2_file(argv[1],name,sizeof(name)));
    assert(!strcmp(name,"LGames"));
    void *image=mmap((void *)(uintptr_t)APP_BASE_ADDR,APP_PARTITION_SIZE,
                     PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(image!=MAP_FAILED);
    memset(image,255,APP_PARTITION_SIZE);
    FILE *f=fopen(argv[1],"rb"); assert(f);
    uf2_block_t b;
    unsigned blocks=0;
    while(fread(&b,1,sizeof(b),f)==sizeof(b)) {
        assert(b.magic_start0==UF2_MAGIC_START0 && b.magic_start1==UF2_MAGIC_START1 && b.magic_end==UF2_MAGIC_END);
        assert(b.flags==UF2_FLAG_FAMILY_ID_PRESENT && b.file_size_or_family==UF2_FAMILY_RP2350_ARM_S);
        assert(b.payload_size==256 && b.target_addr==APP_BASE_ADDR+blocks*256);
        assert(b.target_addr+b.payload_size<=APP_END_ADDR && b.block_no==blocks);
        memcpy((void *)(uintptr_t)b.target_addr,b.data,b.payload_size);
        ++blocks;
    }
    assert(feof(f) && blocks==b.num_blocks); fclose(f);
    const char *names[]={"LGames","LTris","LBreakout2"};
    for(unsigned i=0;i<3;++i) {
        uint32_t base=APP_BASE_ADDR+i*0x100000;
        assert(program_name_from_xip(base,0x100000,name,sizeof(name)));
        assert(!strcmp(name,names[i]));
        const uint32_t *vt=(const uint32_t *)(uintptr_t)base;
        assert(vt[0]>=SRAM_BASE && vt[0]<=SRAM_END_ADDR && !(vt[0]&7));
        assert(vt[1]>=base && vt[1]<base+0x100000 && (vt[1]&1));
        printf("LOADER SLOT PASS: %s at %08x\n",name,base);
    }
    munmap(image,APP_PARTITION_SIZE);
    printf("BUNDLE PASS: real launcher disk/XIP parser, %u contiguous in-partition blocks\n",blocks);
    return 0;
}
