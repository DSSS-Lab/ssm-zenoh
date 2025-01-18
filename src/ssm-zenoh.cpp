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
#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netdb.h>

#include "ssm-zenoh.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

using namespace std;

int msq_id = -1;				                /**< Message queue ID */
char ipv4_address[NI_MAXHOST] = "";
pid_t my_pid;
SSM_Zenoh_List *ssm_zenoh_top = 0;
char err_msg[20];
volatile int keep_running = 1;                 // Flag to control program termination

// Signal handler for graceful shutdown
void handle_sigint(int sig) {
    printf("\nSignal caught, shutting down...\n");
    keep_running = 0;
}

// Main function to list all IPv4 addresses
void list_ip_addresses() {
    struct ifaddrs *ifaddr, *ifa;

    // Retrieve network interfaces and addresses
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    printf("List of IPv4 addresses:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // Ignore loopback interface "lo"
        if (strcmp(ifa->ifa_name, "lo") == 0)
            continue;

        // Process only AF_INET (IPv4) addresses
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char host[NI_MAXHOST];

            // Convert the address to a readable format
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                printf("%s: %s\n", ifa->ifa_name, host);

                // Store the last found IPv4 address in the global variable
                strncpy(ipv4_address, host, NI_MAXHOST);
                            }
        }
    }

    freeifaddrs(ifaddr);
}

int ssm_zenoh_ini( void )
{
    /* Open message queue */
	if( ( msq_id = msgget( ( key_t ) MSQ_KEY, 0666 ) ) < 0 )
	{
		sprintf( err_msg, "msq open err" );
		return 0;
	}
    list_ip_addresses();

	/* 内部時刻同期変数への接続 */
	if( !opentimeSSM() )
	{
		sprintf( err_msg, "time open err" );
		return 0;
	}

	return 1;
}

SSM_Zenoh_List *add_ssm_zenoh_list( SSM_sid ssmId, char *name, int suid, size_t ssize, int hsize, ssmTimeT cycle )
{
    SSM_Zenoh_List *p, *q;

    p = ( SSM_Zenoh_List * ) malloc( sizeof ( SSM_Zenoh_List ) );
    if( !p )
    {
        fprintf( stderr, "ERROR  : cannot allock memory of local list\n" );
    }

    p->ssmId = ssmId;
    strcpy( p->name, name );
    p->suid = suid;
    p->ssize = ssize;
    p->hsize = hsize;
    p->next = 0;
    p->property = 0;
    p->property_size = 0;
    /* リストの最後にpを追加 */
    if( !ssm_zenoh_top )
    {
        ssm_zenoh_top = p;
    }
    else
    {
        q = ssm_zenoh_top;
        while( q->next )
        {
            q = q->next;
        }
        q->next = p;
    }
    return p;
}

SSM_Zenoh_List *search_ssm_zenoh_list( char *name, int suid )
{
    SSM_Zenoh_List *p, *pn, *pni, *pi;

    p = ssm_zenoh_top;

    pn = 0;
    pni = 0;
    pi = 0;
    while( p )
    {
        if( strcmp( p->name, name ) == 0 )
        {
            pn = p;
            if( p->suid == suid )
            {
                pni = p;
            }
        }
        if( p->suid == suid )
        {
            pi = p;
        }
        p = p->next;
    }

    if( pni )
        return pni;
    return 0;
}

SSM_Zenoh_List *get_nth_ssm_zenoh_list( int n )
{
    SSM_Zenoh_List *p;
    p = ssm_zenoh_top;

    while( p )
    {
        n--;
        if( n < 0 )
            return p;
        p = p->next;
    }

    p = 0;
    return p;
}

void free_ssm_zenoh_list( SSM_Zenoh_List * ssmp )
{
    if( ssmp )
    {
        if( ssmp->next )
            free_ssm_zenoh_list( ssmp->next );
        if( ssmp->ssmId )
        {
            releaseSSM( &ssmp->ssmId );
            printf( "%s detached\n", ssmp->name );
        }
    }
}

void print_bits(unsigned char byte) {
    for (int i = 7; i >= 0; i--) {
        printf("%d", (byte >> i) & 1);
    }
}

void print_char_bits(const char* str) {
    while (*str) {
        print_bits((unsigned char)*str);
        printf(" ");
        str++;
    }
    printf("\n");
}

typedef struct
{
    int num;
} intSsm_k;

void printStruct(const char *data) {
    // Cast das char* zurück zur Struct
    const intSsm_k *s = (const intSsm_k *)data;
    printf("num: %d\n", s->num);
}

// Callback function called when a semaphore is signaled
void semaphore_callback(zenoh_context* z_context, shm_zenoh_info* shm_info) {
    z_publisher_put_options_t options;
    z_publisher_put_options_default(&options);

    z_owned_bytes_t attachment;
    char attachment_str[sizeof(ipv4_address) + sizeof(shm_info->tid) + sizeof(shm_info->ssize) + sizeof(shm_info->hsize) + sizeof(shm_info->cycle)];
    snprintf(attachment_str, sizeof(attachment_str), "%s;%d;%lu;%d;%f", ipv4_address, shm_info->tid, shm_info->ssize, shm_info->hsize, shm_info->cycle);
    if (z_bytes_copy_from_str(&attachment, attachment_str) < 0) {
        printf("Failed to create Zenoh options from attachment: %s\n", ipv4_address);
        return;
    }
    options.attachment = z_move(attachment);

    void *data = malloc(shm_info->ssize);
    ssmTimeT *ytime = (ssmTimeT *)(malloc(sizeof(ssmTimeT)));

    if (readSSM(shm_info->ssm_sid, data, ytime, shm_info->tid) == 0) {
        printf("Failed read SSM\n");
    }

    // Create a Zenoh payload from the current shared memory content
    z_owned_bytes_t payload;
    z_owned_bytes_writer_t writer;
    z_bytes_writer_empty(&writer);
    z_bytes_writer_write_all(z_loan_mut(writer), (uint8_t*) data, sizeof(data));
    z_bytes_writer_finish(z_move(writer), &payload);

    printf("pub data size: %lu\n", sizeof((char*)data));
    printf("pub data tid %d: ", shm_info->tid);
    print_char_bits((char*)data);
    printStruct((char*)data);

    if (z_publisher_put(z_loan(z_context->pub), z_move(payload), &options) < 0) {
        printf("Failed to publish data for key\n");
        free(data);
        free(ytime);
    } else {
        printf("Data published successfully\n");
        free(data);
        free(ytime);
    }
}

// Function for semaphore monitoring thread
void* semaphore_monitor(void* arg) {
    semaphore_arg* sem_arg = (semaphore_arg*)arg;
    zenoh_context* z_context = sem_arg->z_context;
    shm_zenoh_info* shm_info = static_cast<shm_zenoh_info *>(malloc(sizeof(shm_zenoh_info)));;

    SSM_sid ssm_sid = openSSM(sem_arg->name, sem_arg->suid, SSM_READ);
    if (ssm_sid == 0) {
        printf("Failed to open SSM ID\n");
        return NULL;
    }

    strncpy( shm_info->name, sem_arg->name, SSM_SNAME_MAX );
    shm_info->suid = sem_arg->suid;
    shm_info->ssm_sid = ssm_sid;

    char zenoh_key[SSM_SNAME_MAX + sizeof(int)];
    if (snprintf(zenoh_key, sizeof(zenoh_key), "%s/%d", sem_arg->name, sem_arg->suid) < 0) {
        printf("Failed to format Zenoh key for Shared Memory ID: %d\n", sem_arg->suid);
        return NULL;
    }

    z_owned_publisher_t pub;
    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, zenoh_key) < 0) {
        printf("Failed to create Zenoh key expression from key: %s\n", zenoh_key);
        return NULL;
    }

    if (z_declare_publisher(z_loan(z_context->session), &pub, z_loan(ke), NULL) < 0) {
        printf("Unable to declare Publisher for key expression: %s\n", zenoh_key);
        return NULL;
    }

    z_context->pub = pub;
    shm_info->tid = getTID_top(ssm_sid);
    if (getSSM_info(sem_arg->name, sem_arg->suid, &shm_info->ssize, &shm_info->hsize, &shm_info->cycle, &shm_info->property_size) < 0) {
        printf("ERROR: SSM read error.\n");
        return NULL;
    }

    while (keep_running) {
        shm_info->tid++;
        printf("Waiting TID_TOP: %d\n", shm_info->tid);
        waitTID( ssm_sid, shm_info->tid );
        sem_arg->callback(z_context, shm_info);
    }

    z_drop(z_move(pub));
    free(sem_arg); // Free allocated memory
    free(shm_info); // Free allocated memory
    return NULL;
}

// Function to monitor the message queue
void* message_queue_monitor(void* arg) {
    zc_init_log_from_env_or("error");
    zenoh_context* z_context = (zenoh_context*)arg;
    ssm_msg msg;
    pthread_t thread;
    static pthread_t thread_map[1024] = {0};
    static volatile int active_flags[1024] = {0};

    while (keep_running) {
        // Receive a message from the queue
        if (msgrcv(msq_id, &msg, SSM_MSG_SIZE, ZENOH_MSQ_KEY, IPC_NOWAIT) >= 0) {
            if (msg.res_type == my_pid) {
                continue;
            }

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
                sem_arg->z_context = z_context;

                if (pthread_create(&thread, NULL, semaphore_monitor, sem_arg) != 0) {
                    perror("Error creating thread");
                    free((int*)sem_arg->active);
                    free(sem_arg);
                } else {
                    pthread_detach(thread); // Detach the thread to run in the background
                    thread_map[msg.suid] = thread;
                    active_flags[msg.suid] = *(sem_arg->active);
                }
            } else if (msg.cmd_type == MC_DESTROY) {
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

const char* kind_to_str(z_sample_kind_t kind) {
    switch (kind) {
        case Z_SAMPLE_KIND_PUT:
            return "PUT";
        case Z_SAMPLE_KIND_DELETE:
            return "DELETE";
        default:
            return "UNKNOWN";
    }
}

void data_handler(z_loaned_sample_t* sample, void* arg) {
    SSM_Zenoh_List *slist;
    const z_loaned_bytes_t* attachment = z_sample_attachment(sample);

    // checks if attachment exists
    if (attachment == NULL) {
        printf("Failed to get attachment from shared memory\n");
        return;
    }

    int ssm_zenoh_tid_top, ssm_zenoh_num;
    char ipv4_zenoh_address[NI_MAXHOST];
    size_t ssm_zenoh_size;
    double ssm_zenoh_cycle;

    z_owned_string_t attachment_string;
    z_bytes_to_string(attachment, &attachment_string);

    if (sscanf(z_string_data(z_loan(attachment_string)), "%[^;];%d;%d;%d;%lf", ipv4_zenoh_address, &ssm_zenoh_tid_top, &ssm_zenoh_size, &ssm_zenoh_num, &ssm_zenoh_cycle) != 5) {
        printf("Failed to get attachment for shared memory\n");
        z_drop(z_move(attachment_string));
        return;
    }

    if (strcmp(ipv4_zenoh_address, ipv4_address) == 0) {
        printf("Own Shared memory\n");
        return;
    }

    // printf("Zenoh TID_TOP: %d\n", ssm_zenoh_tid_top);
    // printf("Zenoh size: %d\n", ssm_zenoh_size);
    // printf("Zenoh num: %d\n", ssm_zenoh_num);
    // printf("Zenoh cycle: %lf\n", ssm_zenoh_cycle);

    z_view_string_t key_string;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key_string);

    char ssm_zenoh_name[SSM_SNAME_MAX];
    int ssm_zenoh_suid = 0;
    if (sscanf(z_string_data(z_loan(key_string)), "%[^/]/%d", ssm_zenoh_name, &ssm_zenoh_suid) != 2) {
        printf("Failed to get topic for shared memory\n");
        return;
    }

    slist = search_ssm_zenoh_list( ssm_zenoh_name, ssm_zenoh_suid );

    if( !slist ) {
        SSM_sid ssm_zenoh_ssm_sid = openSSM(ssm_zenoh_name, ssm_zenoh_suid, SSM_WRITE);
        if (ssm_zenoh_ssm_sid == 0) {
            ssm_zenoh_ssm_sid = createSSMP(ssm_zenoh_name, ssm_zenoh_suid, ssm_zenoh_size, ssm_zenoh_num, ssm_zenoh_cycle);
            printf("Topic name: %s\n", ssm_zenoh_name);
            printf("suid = %d\n", ssm_zenoh_suid);
        }
        slist = add_ssm_zenoh_list( ssm_zenoh_ssm_sid, ssm_zenoh_name, ssm_zenoh_suid, ssm_zenoh_size, ssm_zenoh_num, ssm_zenoh_cycle );
    }

    // ToDo: send Time data aswell
    ssmTimeT time = gettimeSSM(  );
    uint8_t data[ssm_zenoh_size];

    z_bytes_reader_t reader = z_bytes_get_reader(z_sample_payload(sample));
    z_bytes_reader_read(&reader, data, sizeof(data));

    printf("sub data size: %lu\n", sizeof(data));
    printf("sub data tid %d: ", ssm_zenoh_tid_top);
    print_char_bits((char*)data);
    printStruct((char*)data);

    printf("test\n");
    //return;

    SSM_tid tid = writeSSM( slist->ssmId, (char*)output_u64, time );
    if (tid < 0) {
        printf("Failed to write to shared memory\n");
        return;
    }

    // printf(">> [Subscriber] Received %s ('%.*s': '%.*s')", kind_to_str(z_sample_kind(sample)),
    //        (int)z_string_len(z_loan(key_string)), z_string_data(z_loan(key_string)),
    //        (int)z_string_len(z_loan(payload_string)), z_string_data(z_loan(payload_string)));

    printf("\n");
    z_drop(z_move(attachment_string));
}

// Function to monitor Zenoh messages
void* zenoh_message_monitor(void* arg) {
    zc_init_log_from_env_or("error");
    zenoh_context* z_context = (zenoh_context*)arg;

    char keyexpr[] = "**";
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str(&ke, keyexpr);

    z_owned_closure_sample_t callback;
    z_closure(&callback, data_handler, NULL, NULL);
    printf("Declaring Subscriber on '%s'...\n", keyexpr);
    z_owned_subscriber_t sub;
    if (z_declare_subscriber(z_loan(z_context->session), &sub, z_loan(ke), z_move(callback), NULL)) {
        printf("Unable to declare subscriber.\n");
        exit(-1);
    }

    while (keep_running) {
        printf("Monitoring Zenoh messages...\n");
        z_sleep_s(1);
    }

    z_drop(z_move(sub));
    return NULL;
}

int main(int argc, char **argv) {
    // Register signal handler for graceful shutdown
    signal(SIGINT, handle_sigint);

    my_pid = getpid();

    if( !ssm_zenoh_ini(  ) || !initSSM() )
		return -1;

    // Configure Zenoh session
    z_owned_config_t config;
    z_config_default(&config);
    z_owned_session_t session;
    if (z_open(&session, z_move(config), NULL) < 0) {
        printf("Unable to open Zenoh session!\n");
        exit(1);
    }

    zenoh_context z_context;
    z_context.session = session;

    // Start the message queue monitoring thread
    pthread_t msg_thread;
    if (pthread_create(&msg_thread, NULL, message_queue_monitor, &z_context) != 0) {
        perror("Error creating message queue thread");
        exit(1);
    }

    // Start the Zenoh message monitoring thread
    pthread_t zenoh_thread;
    if (pthread_create(&zenoh_thread, NULL, zenoh_message_monitor, &z_context) != 0) {
        perror("Error creating Zenoh message thread");
        exit(1);
    }

    // Wait for the threads to finish
    pthread_join(msg_thread, NULL);
    pthread_join(zenoh_thread, NULL);

    z_drop(z_move(session));
    endSSM();

    return 1;
}
