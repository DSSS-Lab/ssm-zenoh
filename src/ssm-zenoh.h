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

typedef struct {
    int tid;
    SSM_sid ssm_sid;
    int suid;                       // shm id
    char name[SSM_SNAME_MAX];       // shm name
    size_t ssize;
    int hsize;
    double cycle;
    size_t property_size;
} shm_zenoh_info;

// Structure for semaphore monitoring
typedef struct {
    int suid;                       // shm id
    char name[SSM_SNAME_MAX];       // shm name
    void (*callback)(zenoh_context* z_context, shm_zenoh_info* shm_info);     // Callback function to handle signals
    volatile int* active;           // Flag to indicate if the thread is active
    zenoh_context* z_context;       // Pointer to the Zenoh context
} semaphore_arg;

typedef struct ssm_zenoh_list *SSM_Zenoh_ListPtr;
typedef struct ssm_zenoh_list
{
    char ipv4_zenoh_address[NI_MAXHOST];
    char name[SSM_SNAME_MAX];
    int suid;
    SSM_sid ssmId;
    size_t ssize;
    int hsize;
    SSM_Zenoh_ListPtr next;
    char *property;
    int property_size;
} SSM_Zenoh_List;

/* ---- function prototypes ---- */
void handle_sigint(int sig);
void list_ip_addresses();
int ssm_zenoh_ini( void );
SSM_Zenoh_List *add_ssm_zenoh_list( SSM_sid ssmId, char *name, int suid, size_t ssize, int hsize, ssmTimeT cycle );
SSM_Zenoh_List *search_ssm_zenoh_list( char *name, int suid );
SSM_Zenoh_List *get_nth_ssm_zenoh_list( int n );
void free_ssm_zenoh_list( SSM_Zenoh_List * ssmp );
void semaphore_callback(int shm_id);
void* semaphore_monitor(void* arg);
void* message_queue_monitor(void* arg);


#endif