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

SSM_Zenoh_List *add_ssm_zenoh_list( SSM_sid ssmId, char *name, int suid, size_t ssize, int hsize, ssmTimeT cycle, int extern_node )
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
    p->cycle = cycle;
    p->extern_node = extern_node;
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

// Callback function called when a semaphore is signaled
void semaphore_callback(zenoh_context* z_context, SSM_Zenoh_List* shm_info) {
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
    if (readSSM(shm_info->ssmId, data, ytime, shm_info->tid) == 0) {
        printf("Failed read SSM\n");
    }

    // Create a Zenoh payload from the current shared memory content
    z_owned_bytes_t b_time, b_data;
    z_bytes_copy_from_buf(&b_time, (uint8_t*) ytime, sizeof(ssmTimeT));
    z_bytes_copy_from_buf(&b_data, (uint8_t*) data, sizeof(data));

    z_owned_bytes_writer_t writer;
    z_bytes_writer_empty(&writer);
    if (z_bytes_writer_append(z_loan_mut(writer), z_move(b_time)) < 0) {
        printf("Failed z_bytes_writer_append\n");
        return;
    }
    if (z_bytes_writer_append(z_loan_mut(writer), z_move(b_data)) < 0) {
        printf("Failed z_bytes_writer_append\n");
        return;
    }

    z_owned_bytes_t payload;
    z_bytes_writer_finish(z_move(writer), &payload);

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
    SSM_Zenoh_List *slist = (SSM_Zenoh_List*) arg;
    semaphore_arg* sem_arg = (semaphore_arg*)arg;
    zenoh_context* z_context = sem_arg->z_context;

    SSM_sid ssm_sid = openSSM(sem_arg->name, sem_arg->suid, SSM_READ);
    if (ssm_sid == 0) {
        printf("Failed to open SSM ID\n");
        return NULL;
    }

    char zenoh_key[5 + SSM_SNAME_MAX + sizeof(int)];
    if (snprintf(zenoh_key, sizeof(zenoh_key), "data/%s/%d", sem_arg->name, sem_arg->suid) < 0) {
        printf("Failed to format Zenoh key for Shared Memory ID: %d\n", sem_arg->suid);
        return NULL;
    }

    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, zenoh_key) < 0) {
        printf("Failed to create Zenoh key expression from key: %s\n", zenoh_key);
        return NULL;
    }

    z_owned_publisher_t pub;
    if (z_declare_publisher(z_loan(z_context->session), &pub, z_loan(ke), NULL) < 0) {
        printf("Unable to declare Publisher for key expression: %s\n", zenoh_key);
        return NULL;
    }

    z_context->pub = pub;
    slist->ssmId = ssm_sid;
    slist->tid = getTID_top(ssm_sid);
    if (getSSM_info(sem_arg->name, sem_arg->suid, &slist->ssize, &slist->hsize, &slist->cycle, &slist->property_size) < 0) {
        printf("ERROR: SSM read error.\n");
        return NULL;
    }

    while (keep_running) {
        if (sem_arg->active == 0) {
            printf("test\n");
            break;
        }
        slist->tid++;
        printf("Waiting TID_TOP: %d\n", slist->tid);
        waitTID( ssm_sid, slist->tid );
        sem_arg->callback(z_context, slist);
    }

    z_drop(z_move(pub));
    free(sem_arg); // Free allocated memory
    return NULL;
}

// Function to monitor the message queue
void* message_queue_monitor(void* arg) {
    SSM_Zenoh_List *slist;
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

            if (msg.cmd_type == MC_CREATE) {
                if (thread_map[msg.suid] != 0) {
                    printf("Semaphore ID %d is already being monitored.\n", msg.suid);
                    continue;
                }
                // Create a thread to monitor the semaphore
                semaphore_arg* sem_arg = (semaphore_arg *)(malloc(sizeof(semaphore_arg)));
                sem_arg->suid = msg.suid;
                strncpy( sem_arg->name, msg.name, SSM_SNAME_MAX );
                sem_arg->callback = semaphore_callback;
                sem_arg->active = (volatile int*)malloc(sizeof(int));
                *(sem_arg->active) = 1;
                sem_arg->z_context = z_context;
                slist = search_ssm_zenoh_list( sem_arg->name, sem_arg->suid );
                if( !slist ) {
                    sem_arg->slist = add_ssm_zenoh_list( NULL, sem_arg->name, sem_arg->suid, 0, 0, 0, 0 );
                }

                if (pthread_create(&thread, NULL, semaphore_monitor, sem_arg) != 0) {
                    perror("Error creating thread");
                    free((int*)sem_arg->active);
                    free(sem_arg);
                } else {
                    pthread_detach(thread); // Detach the thread to run in the background
                    thread_map[msg.suid] = thread;
                    active_flags[msg.suid] = *(sem_arg->active);
                }
            } else if (msg.cmd_type == MC_STREAM_PROPERTY_SET) {
                slist = search_ssm_zenoh_list( msg.name, msg.suid );

                if (!slist) {
                    printf("shm not created yet");
                } else if (slist->extern_node == 1) {
                    continue;
                }

                char property[msg.ssize];
                if (get_propertySSM(msg.name, msg.suid, property) == 0) {
                    printf("Failed to get property from shared memory\n");
                    continue;
                }

                char zenoh_property_key[9 + SSM_SNAME_MAX + sizeof(int)];
                if (snprintf(zenoh_property_key, sizeof(zenoh_property_key), "property/%s/%d", msg.name, msg.suid) < 0) {
                    printf("Failed to format Zenoh key for Property: %d\n", msg.suid);
                    continue;
                }

                z_view_keyexpr_t property_ke;
                if (z_view_keyexpr_from_str(&property_ke, zenoh_property_key) < 0) {
                    printf("Failed to create Zenoh key expression from key: %s\n", zenoh_property_key);
                    continue;
                }

                z_put_options_t property_options;
                z_put_options_default(&property_options);
                z_owned_bytes_t attachment;
                if (z_bytes_copy_from_str(&attachment, ipv4_address) < 0) {
                    printf("Failed to create Zenoh options from attachment: %s\n", ipv4_address);
                    continue;
                }
                property_options.attachment = z_move(attachment);

                // Create a Zenoh payload from the current shared memory content
                z_owned_bytes_t b_property_size, b_property;
                z_bytes_copy_from_buf(&b_property_size, (uint8_t*) &msg.ssize, sizeof(size_t));
                z_bytes_copy_from_buf(&b_property, (uint8_t*) property, sizeof(property));

                z_owned_bytes_writer_t property_writer;
                z_bytes_writer_empty(&property_writer);
                if (z_bytes_writer_append(z_loan_mut(property_writer), z_move(b_property_size)) < 0) {
                    printf("Failed z_bytes_writer_append\n");
                    continue;
                }
                if (z_bytes_writer_append(z_loan_mut(property_writer), z_move(b_property)) < 0) {
                    printf("Failed z_bytes_writer_append\n");
                    continue;
                }

                z_owned_bytes_t payload;
                z_bytes_writer_finish(z_move(property_writer), &payload);

                if (z_put(z_loan(z_context->session), z_loan(property_ke), z_move(payload), &property_options) < 0) {
                    printf("Failed put property\n");
                    continue;
                }
                printf("Stream property set successfully.\n");
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

void print_slice_data(z_view_slice_t *slice) {
    for (size_t i = 0; i < z_slice_len(z_view_slice_loan(slice)); i++) {
        printf("0x%02x ", z_slice_data(z_view_slice_loan(slice))[i]);
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

    z_view_string_t key_string;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key_string);

    char ssm_zenoh_name[SSM_SNAME_MAX];
    int ssm_zenoh_suid = 0;
    if (sscanf(z_string_data(z_loan(key_string)), "data/%[^/]/%d", ssm_zenoh_name, &ssm_zenoh_suid) != 2) {
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
        slist = add_ssm_zenoh_list( ssm_zenoh_ssm_sid, ssm_zenoh_name, ssm_zenoh_suid, ssm_zenoh_size, ssm_zenoh_num, ssm_zenoh_cycle, 1 );
    }

    uint8_t time[sizeof(ssmTimeT)];
    uint8_t data[slist->ssize];

    z_bytes_reader_t reader = z_bytes_get_reader(z_sample_payload(sample));
    z_bytes_reader_read(&reader, time, sizeof(ssmTimeT));
    z_bytes_reader_read(&reader, data, sizeof(data));

    ssmTimeT time_value;
    memcpy(&time_value, &time, sizeof(ssmTimeT));

    SSM_tid tid = writeSSM( slist->ssmId, data, time_value );
    if (tid < 0) {
        printf("Failed to write to shared memory\n");
        return;
    }

    printf("\n");
    z_drop(z_move(attachment_string));
}

void property_handler(z_loaned_sample_t* sample, void* arg) {
    SSM_Zenoh_List *slist;
    const z_loaned_bytes_t* property_attachment = z_sample_attachment(sample);

    // checks if attachment exists
    if (property_attachment == NULL) {
        printf("Failed to get attachment for property\n");
        return;
    }

    z_owned_string_t attachment_property_string;
    z_bytes_to_string(property_attachment, &attachment_property_string);
    const char* ipv4_zenoh_address = z_string_data(z_loan(attachment_property_string));

    if (strcmp(ipv4_zenoh_address, ipv4_address) == 0) {
        printf("Own Property\n");
        return;
    }

    z_view_string_t key_string;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key_string);

    char ssm_zenoh_property_name[SSM_SNAME_MAX];
    int ssm_zenoh_property_suid = 0;
    if (sscanf(z_string_data(z_loan(key_string)), "property/%[^/]/%d", ssm_zenoh_property_name, &ssm_zenoh_property_suid) != 2) {
        printf("Failed to get topic for shared memory\n");
        return;
    }

    slist = search_ssm_zenoh_list( ssm_zenoh_property_name, ssm_zenoh_property_suid );

    if( !slist ) {
        printf("There is no shared memory for the property");
        return;
    }

    uint8_t property_size[sizeof(size_t)];

    z_bytes_reader_t reader = z_bytes_get_reader(z_sample_payload(sample));
    z_bytes_reader_read(&reader, property_size, sizeof(size_t));
    size_t property_size_value;
    memcpy(&property_size_value, &property_size, sizeof(size_t));

    uint8_t property[property_size_value];
    z_bytes_reader_read(&reader, property, sizeof(property));

    if (set_propertySSM(ssm_zenoh_property_name, ssm_zenoh_property_suid, property, property_size_value) == 0) {
        printf("Failed to set property\n");
        return;
    }

    printf("Successfully write property\n");
    z_drop(z_move(attachment_property_string));
}


// Function to monitor Zenoh messages
void* zenoh_message_monitor(void* arg) {
    zc_init_log_from_env_or("error");
    zenoh_context* z_context = (zenoh_context*)arg;

    char data_keyexpr[] = "data/**";
    z_view_keyexpr_t data_ke;
    z_view_keyexpr_from_str(&data_ke, data_keyexpr);

    z_owned_closure_sample_t data_callback;
    z_closure(&data_callback, data_handler, NULL, NULL);
    printf("Declaring Subscriber on '%s'...\n", data_keyexpr);
    z_owned_subscriber_t data_sub;
    if (z_declare_subscriber(z_loan(z_context->session), &data_sub, z_loan(data_ke), z_move(data_callback), NULL)) {
        printf("Unable to declare subscriber.\n");
        exit(-1);
    }

    char property_keyexpr[] = "property/**";
    z_view_keyexpr_t property_ke;
    z_view_keyexpr_from_str(&property_ke, property_keyexpr);

    z_owned_closure_sample_t property_callback;
    z_closure(&property_callback, property_handler, NULL, NULL);
    printf("Declaring Subscriber on '%s'...\n", property_keyexpr);
    z_owned_subscriber_t property_sub;
    if (z_declare_subscriber(z_loan(z_context->session), &property_sub, z_loan(property_ke), z_move(property_callback), NULL)) {
        printf("Unable to declare subscriber.\n");
        exit(-1);
    }

    while (keep_running) {
        printf("Monitoring Zenoh messages...\n");
        z_sleep_s(1);
    }

    z_drop(z_move(data_sub));
    // z_drop(z_move(property_sub));
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
