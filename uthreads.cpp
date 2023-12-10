#include <iostream>
#include "uthreads.h"
#include "Thread.cpp"

#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/time.h>

#include <vector>
#include <map>
#include <algorithm>

#define BLOCK_SIGNALS sigprocmask(SIG_BLOCK, &sig_set,NULL)
#define ALLOW_SIGNALS sigprocmask(SIG_UNBLOCK, &sig_set,NULL)


Thread *all_threads[MAX_THREAD_NUM];
std::vector<Thread*> ready_threads;
std::vector<Thread*> blocked_threads;
std::map<Thread*, int> sleeping_threads;

Thread *run_thread;

int total_quantums = 0;
sigset_t sig_set;


#define JB_SP 6
#define JB_PC 7





/**
 * Goes through all the thread that in sleep mode and if the thread done with the time sleeping
 * we removed the thread from sleeping_thread vector also if the thread are not blocked we move the thread to
 * the queue of the ready threads.
 */
void waking_up_checker(){
    std::vector<Thread*> to_remove;
    for (auto& kv_pair : sleeping_threads) {
        kv_pair.second--;
        if (kv_pair.second <= 0){ ///
            to_remove.push_back(kv_pair.first);
        }
    }
    for (Thread* t : to_remove) {
        sleeping_threads.erase(t);
        if (find(blocked_threads.begin(), blocked_threads.end(), t) == blocked_threads.end()){
            ready_threads.push_back(t); // not locked by mutex - can be ready_threads
            t->state = Thread::READY;
        }
    }
}

void scheduler(int sig)
{
    waking_up_checker();

    if (run_thread->state != Thread::TERMINATED && run_thread->state != Thread::BLOCKED){
        int ret_val = sigsetjmp(run_thread->env, 1);
        if(ret_val==1){
            return;
        }
        if(!ready_threads.empty()){
            ready_threads.push_back(run_thread);
            run_thread->state = Thread::READY;
        }
    }

    if (ready_threads.empty()) {

        run_thread->state = Thread::RUNNING;
        run_thread->numOfquantums++;
        total_quantums++;
    } else {

        Thread* first = ready_threads.front();
        auto it = std::remove(ready_threads.begin(), ready_threads.end(), first);
        if (it == ready_threads.end()) {
            std::cerr << "thread library error: try remove from the queue none existing thread" << std::endl;
            exit(1);
        }
        ready_threads.erase(it, ready_threads.end());

        if (run_thread->state == Thread::TERMINATED) {
            all_threads[run_thread->tid] = nullptr;
            delete run_thread;
        }

        first->state = Thread::RUNNING;
        run_thread = first;

        total_quantums++;
        run_thread->numOfquantums++;
        siglongjmp(run_thread->env, 1);
    }
}










int uthread_init(int quantum_usecs){
    if (quantum_usecs<=0){
        std::cerr <<"thread library error: quantum_usecs is none positive"<< std::endl;
        return -1;
    }

    struct sigaction sa = {nullptr};
    struct itimerval timer{};

    sa.sa_handler = &scheduler;
    if (sigaction(SIGVTALRM, &sa,nullptr) < 0) {
        std::cerr <<"system error: sigaction error." << std::endl;
        exit(1);
    }
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = quantum_usecs;

    if (setitimer (ITIMER_VIRTUAL, &timer, nullptr) == -1) {
        std::cerr <<"system error: setitimer error." << std::endl;
        exit(1);
    }

    sigemptyset(&sig_set);
    sigaddset(&sig_set,SIGVTALRM);
    auto *main_thread = new Thread(0,nullptr);
    run_thread = main_thread;
    all_threads[0] = main_thread;
    main_thread->numOfquantums++;
    sigsetjmp(main_thread->env,1);
    total_quantums++;
    return 0;
}
int uthread_spawn(void (*f)()) {
    BLOCK_SIGNALS;
    int i;
    for (i = 0; i < MAX_THREAD_NUM; i++) {
        if (all_threads[i] == nullptr) {
            break;
        }
    }

    if (i == MAX_THREAD_NUM) {
        ALLOW_SIGNALS;
        std::cerr << "thread library error: No available slots in the array of the all threads" << std::endl;
        return -1;
    }


    auto* new_thread = new Thread(i, f);
    all_threads[i] = new_thread;
    ready_threads.push_back(new_thread);

    ALLOW_SIGNALS;
    return i;
}

int uthread_terminate(int tid){
    BLOCK_SIGNALS;
    if(tid == 0){
        ready_threads.clear();
        blocked_threads.clear();
        delete [] *all_threads;
        exit(EXIT_SUCCESS);
    }

    if(all_threads[tid]== nullptr){
        std::cerr << "thread library error: thread does not exists" << std::endl;
        ALLOW_SIGNALS;
        return -1;
    }

    if(tid == (int)run_thread->tid){
        ALLOW_SIGNALS;
        run_thread->state = Thread::TERMINATED;
        scheduler(0);
        return 0;
    }

    Thread *to_delete = all_threads[tid];
    if(to_delete->state == Thread::READY) {
        ready_threads.erase(std::remove_if(ready_threads.begin(), ready_threads.end(),
                                           [&](Thread* t){ return t == to_delete; }), ready_threads.end());
    }
    else if (to_delete->state == Thread::BLOCKED && (find(blocked_threads.begin(),
                                                          blocked_threads.end(), to_delete) != blocked_threads.end())){
        blocked_threads.erase(std::remove_if(blocked_threads.begin(), blocked_threads.end(),
                                             [&](Thread* t){ return t == to_delete; }), blocked_threads.end());
    }
    else if(to_delete->state == Thread::BLOCKED && (sleeping_threads.find(to_delete) != sleeping_threads.end())){
        sleeping_threads.erase(to_delete);}

    all_threads[tid] = nullptr;
    delete to_delete;
    ALLOW_SIGNALS;
    return 0;
}

/**
 * we handel the sleep mode of thread almost same as blocking thread with 1 difference -
 * we dont push the thread to the blocked_threads vector.
 * @param tid the id of the thread to block or make sleep.
 * @param flag indicator if we want to block or sleep, 1 to block 0 to make the thread sleep.
 * @return 0 upon success -1  otherwise.
 */
int uthread_block_or_sleep_block(int tid, int flag) {
    BLOCK_SIGNALS;
    if (tid == 0) {
        std::cerr << "thread library error: try to block main thread" << std::endl;
        ALLOW_SIGNALS;
        return -1;
    }
    Thread* thread_tid = all_threads[tid];
    if (tid == (int)run_thread->tid) {
        int ret_val = sigsetjmp(run_thread->env, 1);
        if (ret_val == 1) {
            ALLOW_SIGNALS;
            return 0;
        }
        run_thread->state = Thread::BLOCKED;
        if (flag == 0) {
            blocked_threads.push_back(run_thread);
        }
        scheduler(0);
    } else if (thread_tid != nullptr) {
        if (thread_tid->state == Thread::READY) {
            ready_threads.erase(std::find(ready_threads.rbegin(), ready_threads.rend(),
                                          thread_tid).base() - 1);
            blocked_threads.push_back(thread_tid); // adding to blocked_threads
        } else if (thread_tid->state == Thread::BLOCKED && flag == 0) {
            if (std::find(blocked_threads.begin(), blocked_threads.end(),
                          thread_tid) == blocked_threads.end()) {
                blocked_threads.push_back(thread_tid);
            }
        }
        thread_tid->state = Thread::BLOCKED;
        ALLOW_SIGNALS;
        return 0;
    }
    std::cerr << "thread library error: there is no thread with id = "<< tid << std::endl;
    ALLOW_SIGNALS;
    return -1;
}

int uthread_block(int tid){
    return uthread_block_or_sleep_block(tid,0);
}

/**
 * call uthread_block_or_sleep_block with flag 1
 * @param tid thread to sleep.
 * @return 0 upon success -1  otherwise.
 */
int uthread_block_by_sleep(int tid){
    return uthread_block_or_sleep_block(tid,1);
}
int uthread_sleep(int num_quantums){
    if (run_thread != nullptr){
        if (run_thread->tid == 0){
            return -1;
        }
        if (sleeping_threads.find(run_thread) == sleeping_threads.end()) {
            sleeping_threads.insert({run_thread, num_quantums + 1});
            uthread_block_by_sleep(run_thread->tid);
        }
    }
    return 0;
}

int uthread_resume(int tid){
    BLOCK_SIGNALS;
    Thread *thread_tid = all_threads[tid];

    if(thread_tid != nullptr){
        if (thread_tid->state == Thread::RUNNING || thread_tid->state == Thread::READY){
            ALLOW_SIGNALS;
            return 0;
        }
        else if(thread_tid->state == Thread::BLOCKED && (find(blocked_threads.begin(), blocked_threads.end(),
                                              thread_tid) == blocked_threads.end())){
            ALLOW_SIGNALS;
            return 0;
        }

        if(find(blocked_threads.begin(), blocked_threads.end(), thread_tid) != blocked_threads.end()){
            blocked_threads.erase(std::remove(blocked_threads.begin(), blocked_threads.end(), thread_tid),
                                  blocked_threads.end());
        }

        if(sleeping_threads.find(thread_tid) != sleeping_threads.end()){
            ALLOW_SIGNALS;
            return 0;
        }
        ready_threads.push_back(thread_tid);
        thread_tid->state = Thread::READY;
        ALLOW_SIGNALS;
        return 0;
    }
    ALLOW_SIGNALS;
    std::cerr << "thread library error: there is no thread with id = "<< tid << std::endl;
    return -1;
}


int uthread_get_tid(){
    return run_thread->tid;
}
int uthread_get_total_quantums(){
    return total_quantums;
}
int uthread_get_quantums(int tid){
    if (all_threads[tid] != nullptr){
        return all_threads[tid]->numOfquantums;
    }
    std::cerr << "thread library error: thread" << tid << "the thread has no quantums or does not exists" << std::endl;
    return -1;
}