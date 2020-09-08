#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

/* macros */
#define SIZE_16M   (16*1024*1024)
const unsigned char dead_code[]={0xde,0xad,0xc0,0xde};



/* variables */
bool kernel_front;
const char *filename;

unsigned int rootfs_size;
unsigned int file_size;
unsigned int boundary_of_kernel_rootfs;
unsigned int rootfs_data_addr, rootfs_data_addr_end;
unsigned int valid_blks_offset;
unsigned int rootfs_end;

char whole_file[SIZE_16M];
char valid_blks[SIZE_16M];



void collect_valid_blocks(void) {
    const unsigned char magic[] = {0x19, 0x85, 0x20, 0x03};
    const unsigned char nouse_block[]={
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    };
    int last_bingo_addr = 0;
    valid_blks_offset = 0;
    
    for ( int addr = rootfs_data_addr ; addr < rootfs_end ; addr += 0x10000 ) {
        if( (!memcmp(whole_file+addr, magic, 4))  && memcmp(whole_file+addr+16, nouse_block, 32)) {
            /* valid block */
            printf("valid: %#x\n", addr);
            if( (addr != last_bingo_addr+0x10000) && last_bingo_addr) {
                printf("warning: not consecutive valid block(%#x->%#x)\n",last_bingo_addr,addr);
            }
            memcpy(valid_blks+valid_blks_offset, whole_file+addr , 0x10000);
            valid_blks_offset += 0x10000;
            last_bingo_addr = addr;
        }
    }
}

int write_upgrade_image(const char *filename, const void *ptr, int size) {
    char output_name[256];
    strcpy(output_name, filename);
    strcpy( strchr(output_name, '.'), "-upgrade.bin" );

    FILE *fp=fopen(output_name, "w");
    fwrite(ptr, 1, size, fp);
    fclose(fp);

    return 0;
}

int kernel_front_case(void) {
    const unsigned char empty_block[] = {0x19 ,0x85 ,0x20 ,0x03 ,0x00 ,0x00 ,0x00 ,0x0c ,0xf0 ,0x60 ,0xdc ,0x98 ,0xff ,0xff ,0xff ,0xff};
    collect_valid_blocks();

    memcpy(whole_file+rootfs_data_addr, valid_blks, valid_blks_offset);
    /* 预留128k空余给sysupgrade.tgz */
    memset(whole_file+rootfs_data_addr+valid_blks_offset, 0xff, 0x20000);
    memcpy(whole_file+rootfs_data_addr+valid_blks_offset, empty_block, sizeof empty_block);
    memcpy(whole_file+rootfs_data_addr+valid_blks_offset+0x10000, empty_block, sizeof empty_block);
    printf("deadcode addr = %#x\n", rootfs_data_addr+valid_blks_offset+0x20000);
    memcpy(whole_file+rootfs_data_addr+valid_blks_offset+0x20000, dead_code, 4);

    write_upgrade_image(filename, whole_file,rootfs_data_addr+valid_blks_offset+0x20000+4);

    return 0;
}


int kernel_back_case(void) {
    collect_valid_blocks();

    memcpy(whole_file+rootfs_data_addr, valid_blks, valid_blks_offset);
    printf("deadcode addr = %#x\n", rootfs_data_addr+valid_blks_offset);
    memcpy(whole_file+rootfs_data_addr+valid_blks_offset, dead_code, 4);
    memset(whole_file+rootfs_data_addr+valid_blks_offset+4, 0xff, 
           rootfs_size-(rootfs_data_addr+valid_blks_offset+4));
    write_upgrade_image(filename, whole_file, file_size);

    return 0;
}


int main(int argc, const char **argv) {
    if(argc < 2) {
        printf("usage: %s <firmware> [cfg]\n", argv[0]);
        return 0;
    }
    
    char line[1024];
    filename = argv[1];

    /**
     * read: boundary + rootfs_data_addr 
     */
    if( argc < 3 ) {
        cout << "kernel front of rootfs ? (y/n):" ;
        fgets(line, sizeof line, stdin);
        if( 'y' == *line) {
            kernel_front = true;
        } else {
            kernel_front = false;
        }
        if( !kernel_front ) {
            cout << "boundary of kernel and rootfs(in fact, end of rootfs_data, hex format): ";
            scanf("%x", &boundary_of_kernel_rootfs);
        }

        cout << "address of rootfs_data(hex format): ";
        scanf("%x", &rootfs_data_addr);

        if(kernel_front) {
            cout << "end of rootfs_data(hex format): ";
            scanf("%x", &rootfs_data_addr_end);
        }
    } else {
        char cfg_line[64];
        FILE *cfg_fp = fopen(argv[2], "r");
        if( NULL == cfg_fp ) {
            perror("open cfg-file");
            return EXIT_FAILURE;
        }
        fgets(cfg_line, sizeof cfg_line, cfg_fp);
        kernel_front = ( 'y' == *cfg_line );
        fgets(cfg_line, sizeof cfg_line, cfg_fp);
        sscanf(cfg_line, "%x", &boundary_of_kernel_rootfs);
        fgets(cfg_line, sizeof cfg_line, cfg_fp);
        sscanf(cfg_line, "%x", &rootfs_data_addr);

        fclose(cfg_fp);
    }

    printf("--------------------------------------\n");
    printf("kernel front of rootfs: %s\n", kernel_front ? "yes" : "no");
    printf("boundary_of_kernel_rootfs = %#x\n", boundary_of_kernel_rootfs);
    printf("rootfs_data_addr = %#x\n", rootfs_data_addr);

    /* begin handling */
    FILE *fp = fopen(filename, "r");
    if( NULL == fp ) {
        perror("open firmware");
        return EXIT_FAILURE;
    }
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);
    fread(whole_file, 1, file_size, fp);

    if( kernel_front ) {
        rootfs_end = rootfs_data_addr_end;
        return kernel_front_case();
    } else {
        rootfs_size = boundary_of_kernel_rootfs;
        rootfs_end = rootfs_size;
        return kernel_back_case();
    }
    return 0;
}


