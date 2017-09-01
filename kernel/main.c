#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "fs.h"
#include "string.h"
#include "dir.h"

void dir_list(struct dir *p_dir);

int main(void) {
    put_str("I am kernel\n");
    init_all();
    intr_enable();
    struct stat obj_stat; 
    sys_stat("/", &obj_stat); 
    printk("/'s info\n i_no:%d\n size:%d\n filetype:%s\n", obj_stat.st_ino, obj_stat.st_size, obj_stat.st_filetype == 2 ? "directory" : "regular");
    sys_stat("/dir1", &obj_stat); 
    printk("/dir1's info\n i_no:%d\n size:%d\n filetype:%s\n", obj_stat.st_ino, obj_stat.st_size, obj_stat.st_filetype == 2 ? "directory" : "regular");
    while(1);
    return 0;
}

void dir_list(struct dir *p_dir) {
    if (p_dir) {
        printk("content:\n");
        char* type = NULL;
        struct dir_entry* dir_e = NULL;
        while ((dir_e = sys_readdir(p_dir))) {
            if (dir_e->f_type == FT_REGULAR) {
                type = "regular";
            } else {
                type = "directory";
            }
            printk(" %s %s\n", type, dir_e->filename);
        }
        sys_rewinddir(p_dir);
    }
}
