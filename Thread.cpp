#include <algorithm>
#include <pthread.h>
#include <cstdlib>
#include <bits/sigthread.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <cstdlib>


#define MAX_THREAD_NUM 100 /* maximal number of threads */
#define STACK_SIZE 4096  /* stack size per thread (in bytes) */

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
            : "=g" (ret)
            : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif

class Thread{
public:
    enum State{
        BLOCKED,
        READY,
        RUNNING,
        TERMINATED
    };

    int tid;
    int numOfquantums;
    char * stack;
    sigjmp_buf env;
    State state;

    Thread(int id, void(*entryPoint)(void)){
        if(id != 0) {
            stack = new char[STACK_SIZE];
        }
        else {
            stack = nullptr;
        }
        numOfquantums = 0;
        tid  = id;
        state = READY;
        address_t sp;
        sp = (unsigned long)stack + STACK_SIZE - sizeof(address_t);
        address_t pc;
        pc  = (unsigned long)entryPoint;
        sigsetjmp(env, 1);
        (env->__jmpbuf)[JB_SP] = translate_address(sp);
        (env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&env->__saved_mask);

    }

};
