#ifndef __SSM_ZENOH_H__
#define __SSM_ZENOH_H__

#include "ssm.h"
#include "libssm.h"
#include "ssm-time.h"

/* ---- typedefs ---- */
// Structure for semaphore monitoring
typedef struct {
    int suid;                       // shm id
    char name[SSM_SNAME_MAX];       // shm name
    void (*callback)(int suid);   // Callback function to handle signals
    volatile int* active;           // Flag to indicate if the thread is active
} semaphore_arg;

/* ---- function prototypes ---- */
void handle_sigint(int sig);
void semaphore_callback(int shm_id);
void* semaphore_monitor(void* arg);
void* message_queue_monitor(void* arg);


#endif