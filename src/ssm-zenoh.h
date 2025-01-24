#ifndef __SSM_ZENOH_H__
#define __SSM_ZENOH_H__

//#include "ssm.h"
#include "libssm.h"
#include "ssm-time.h"

#include "../external/zenoh-c/include/zenoh.h"

/* ---- typedefs ---- */
typedef struct {
    z_owned_session_t session;
    z_owned_publisher_t pub;
} zenoh_context;

typedef struct ssm_zenoh_list *SSM_Zenoh_ListPtr;
typedef struct ssm_zenoh_list
{
    char ipv4_zenoh_address[NI_MAXHOST];
    char name[SSM_SNAME_MAX];
    int suid;
    SSM_tid tid;
    size_t ssize;
    int hsize;
    size_t property_size;
    int extern_node;
    int active;
    ssmTimeT cycle;
    SSM_sid ssmId;
    SSM_Zenoh_ListPtr next;
    pthread_t thread;
} SSM_Zenoh_List;

// Structure for shared memory monitoring
typedef struct {
    int suid;                       // shm id
    char name[SSM_SNAME_MAX];       // shm name
    void (*callback)(zenoh_context* z_context, SSM_Zenoh_List* shm_info);     // Callback function to handle signals
    zenoh_context* z_context;       // Pointer to the Zenoh context
    SSM_Zenoh_List* slist;
} shared_memory_arg;

/* ---- function prototypes ---- */
void handle_sigint(int sig);
void list_ip_addresses();
int ssm_zenoh_ini( void );
SSM_Zenoh_List *add_ssm_zenoh_list( char* ipv4_address, char* name, int suid, size_t ssize, int hsize, ssmTimeT cycle, int extern_node, int active );
SSM_Zenoh_List *search_ssm_zenoh_list( char *name, int suid );
SSM_Zenoh_List *get_nth_ssm_zenoh_list( int n );
void free_ssm_zenoh_list( SSM_Zenoh_List * ssmp );

void shared_memory_callback(int shm_id);
void* shared_memory_monitor(void* arg);
void property_msg_handler(ssm_msg msg, zenoh_context* z_context, SSM_Zenoh_List *slist);
void* message_queue_monitor(void* arg);

void data_handler(z_loaned_sample_t* data_sample, void* arg);
void property_handler(SSM_Zenoh_List *slist);
void* zenoh_message_monitor(void* arg);



#endif