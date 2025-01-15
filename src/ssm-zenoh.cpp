#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list>
#include <algorithm>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <signal.h>
#include <errno.h>

#include "ssm-zenoh.h"
#include "../external/zenoh-c/include/zenoh.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

using namespace std;

int msq_id = -1;				                /**< Message queue ID */
int is_check_msgque = 1;						/* メッセージキューがすでに存在しているかを確認しない */
pid_t my_pid;									/* 自分のプロセスID */
char err_msg[20];
volatile int keep_running = 1;                 // Flag to control program termination

// Signal handler for graceful shutdown
void handle_sigint(int sig) {
    printf("\nSignal caught, shutting down...\n");
    keep_running = 0;
}

int ssm_ini( void )
{
    /* Open message queue */
	if( ( msq_id = msgget( ( key_t ) MSQ_KEY, 0666 ) ) < 0 )
	{
		sprintf( err_msg, "msq open err" );
		return 0;
	}
	my_pid = getpid(  );

	/* 内部時刻同期変数への接続 */
	if( !opentimeSSM() )
	{
		sprintf( err_msg, "time open err" );
		return 0;
	}

	return 1;
}



// Callback function called when a semaphore is signaled
void semaphore_callback(int suid) {
    printf("Callback triggered for Shared Memory ID: %d\n", suid);
}

// Function for semaphore monitoring thread
void* semaphore_monitor(void* arg) {
    semaphore_arg* sem_arg = (semaphore_arg*)arg;
    ssm_header *shm_p;

    if ((shm_p = shm_open_ssm(sem_arg->suid)) == 0) {
        return NULL;
    }
    int tid_zenoh_top = shm_p->tid_top;

    while (keep_running) {
        tid_zenoh_top++;
        printf("Waiting TID_TOP: %d\n", tid_zenoh_top);
        shm_cond_wait(shm_p, tid_zenoh_top);
        sem_arg->callback(sem_arg->suid);
    }

    free(sem_arg); // Free allocated memory
    return NULL;
}

// Function to monitor the message queue
void* message_queue_monitor(void* arg) {
    ssm_msg msg;
    pthread_t thread;
    static pthread_t thread_map[1024] = {0};
    static volatile int active_flags[1024] = {0};

    while (keep_running) {
        // Receive a message from the queue
        if (msgrcv(msq_id, &msg, SSM_MSG_SIZE, ZENOH_MSQ_KEY, IPC_NOWAIT) >= 0) {
            printf("New SHM ID received: %d\n", msg.suid);

            if (thread_map[msg.suid] != 0) {
                printf("Semaphore ID %d is already being monitored.\n", msg.suid);
                continue;
            }

            if (msg.cmd_type == MC_CREATE) {
                // Create a thread to monitor the semaphore
                semaphore_arg* sem_arg = static_cast<semaphore_arg *>(malloc(sizeof(semaphore_arg)));
                sem_arg->suid = msg.suid;
                strncpy( sem_arg->name, msg.name, SSM_SNAME_MAX );
                sem_arg->callback = semaphore_callback;
                sem_arg->active = (volatile int*)malloc(sizeof(int));
                *(sem_arg->active) = 1;

                if (pthread_create(&thread, NULL, semaphore_monitor, sem_arg) != 0) {
                    perror("Error creating thread");
                    free((int*)sem_arg->active);
                    free(sem_arg);
                } else {
                    pthread_detach(thread); // Detach the thread to run in the background
                    thread_map[msg.suid] = thread;
                    active_flags[msg.suid] = *(sem_arg->active);
                }
            } else if (msg.cmd_type == MC_TERMINATE) {
                // Stop the corresponding thread
                if (thread_map[msg.suid] != 0) {
                    active_flags[msg.suid] = 0; // Signal the thread to stop
                    thread_map[msg.suid] = 0;
                    printf("Thread monitoring semaphore %d stopped.\n", msg.suid);
                } else {
                    printf("No active thread for semaphore %d to stop.\n", msg.suid);
                }
            }
        } else {
            perror("Error reading from message queue");
            //break;
        }

        //usleep(100000); // Sleep briefly to avoid busy-waiting
        sleep(1);
    }
    return NULL;
}


int main(int argc, char **argv) {
    // Register signal handler for graceful shutdown
    signal(SIGINT, handle_sigint);

    if( !ssm_ini(  ) )
		return -1;

    pthread_t msg_thread;
    if (pthread_create(&msg_thread, NULL, message_queue_monitor, NULL) != 0) {
        perror("Error creating message queue thread");
        exit(1);
    }

    // Wait for the message queue thread to finish
    pthread_join(msg_thread, NULL);

    return 1;
}
