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
#include <getopt.h>
#include <limits.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netdb.h>

#include "ssm-zenoh.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

using namespace std;

int verbosity_mode = 1;
const char* zenoh_config_path = NULL;
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

    if( verbosity_mode >= 2 ) {
        printf("List of IPv4 addresses:\n");
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // Ignore loopback interface "lo", "lo0"
        if (strcmp(ifa->ifa_name, "lo") == 0 || strcmp(ifa->ifa_name, "lo0") == 0)
            continue;

        // Process only AF_INET (IPv4) addresses
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char host[NI_MAXHOST];

            // Convert the address to a readable format
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                if( verbosity_mode >= 2 ) {
                    printf("%s: %s\n", ifa->ifa_name, host);
                }

                // Store the last found IPv4 address in the global variable
                strncpy(ipv4_address, host, NI_MAXHOST);
                            }
        }
    }

    freeifaddrs(ifaddr);
}

int ssm_zenoh_ini( void )
{
    my_pid = getpid();
    /* Open message queue */
	if( ( msq_id = msgget( ( key_t ) MSQ_KEY, 0666 ) ) < 0 )
	{
	    if( verbosity_mode >= 1 ) {
	        sprintf( err_msg, "msq open err" );
	    }
		return 0;
	}
    list_ip_addresses();

	/* 内部時刻同期変数への接続 */
	if( !opentimeSSM() )
	{
	    if( verbosity_mode >= 1 ) {
	        sprintf( err_msg, "time open err" );
	    }
		return 0;
	}

	return 1;
}

SSM_Zenoh_List *add_ssm_zenoh_list( SSM_sid ssmId, char *name, int suid, size_t ssize, int hsize, ssmTimeT cycle, int extern_node )
{
    SSM_Zenoh_List *p, *q;

    p = ( SSM_Zenoh_List * ) malloc( sizeof ( SSM_Zenoh_List ) );
    if( !p ) {
        if( verbosity_mode >= 1 ) {
            fprintf( stderr, "ERROR  : cannot allock memory of local list\n" );
        }
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
            if( verbosity_mode >= 1 ) {
                printf( "%s detached\n", ssmp->name );
            }
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

// Callback function called when a shared memory is signaled
void shared_memory_callback(zenoh_context* z_context, SSM_Zenoh_List* shm_info) {
    z_publisher_put_options_t options;
    z_publisher_put_options_default(&options);

    z_owned_bytes_t pub_attachment;
    char pub_attachment_str[sizeof(ipv4_address) + sizeof(shm_info->tid) + sizeof(shm_info->ssize) + sizeof(shm_info->hsize) + sizeof(shm_info->cycle)];
    snprintf(pub_attachment_str, sizeof(pub_attachment_str), "%s;%d;%lu;%d;%f", ipv4_address, shm_info->tid, shm_info->ssize, shm_info->hsize, shm_info->cycle);
    if (z_bytes_copy_from_str(&pub_attachment, pub_attachment_str) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to create Zenoh options from attachment: %s\n", pub_attachment_str);
        }
        return;
    }
    options.attachment = z_move(pub_attachment);

    char data[shm_info->ssize];
    ssmTimeT ytime = 0;
    if (readSSM(shm_info->ssmId, data, &ytime, shm_info->tid) == 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed read SSM\n");
        }
    }

    // Create a Zenoh payload from the current shared memory content
    z_owned_bytes_t b_time, b_data;
    z_bytes_copy_from_buf(&b_time, (uint8_t*) &ytime, sizeof(ssmTimeT));
    z_bytes_copy_from_buf(&b_data, (uint8_t*) data, sizeof(data));

    z_owned_bytes_writer_t pub_writer;
    z_bytes_writer_empty(&pub_writer);
    if (z_bytes_writer_append(z_loan_mut(pub_writer), z_move(b_time)) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed z_bytes_writer_append\n");
        }
        return;
    }
    if (z_bytes_writer_append(z_loan_mut(pub_writer), z_move(b_data)) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed z_bytes_writer_append\n");
        }
        return;
    }

    z_owned_bytes_t pub_payload;
    z_bytes_writer_finish(z_move(pub_writer), &pub_payload);

    if (z_publisher_put(z_loan(z_context->pub), z_move(pub_payload), &options) < 0) {
        if( verbosity_mode >= 2 ) {
            printf("Failed to publish data for key\n");
        }
    } else {
        if( verbosity_mode >= 2 ) {
            printf("Data published successfully\n");
        }
    }
    z_drop(z_move(pub_payload));
    z_drop(z_move(pub_writer));
    z_drop(z_move(b_time));
    z_drop(z_move(b_data));
    z_drop(z_move(pub_attachment));
}

// Function for shared memory monitoring thread
void* shared_memory_monitor(void* arg) {
    SSM_Zenoh_List *slist = (SSM_Zenoh_List*) arg;
    shared_memory_arg* sem_arg = (shared_memory_arg*)arg;
    zenoh_context* z_context = sem_arg->z_context;

    SSM_sid ssm_sid = openSSM(sem_arg->name, sem_arg->suid, SSM_READ);
    if (ssm_sid == 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to open SSM: %s, %d\n", sem_arg->name, sem_arg->suid);
        }
        return NULL;
    }

    char zenoh_key[5 + sizeof(sem_arg->name) + sizeof(sem_arg->suid)];
    if (snprintf(zenoh_key, sizeof(zenoh_key), "data/%s/%d", sem_arg->name, sem_arg->suid) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to format Zenoh key for Shared Memory ID: %d\n", sem_arg->suid);
        }
        return NULL;
    }

    z_view_keyexpr_t pub_key;
    if (z_view_keyexpr_from_str(&pub_key, zenoh_key) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to create Zenoh key expression from key: %s\n", zenoh_key);
        }
        return NULL;
    }

    z_owned_publisher_t pub;
    if (z_declare_publisher(z_loan(z_context->session), &pub, z_loan(pub_key), NULL) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Unable to declare Publisher for key expression: %s\n", zenoh_key);
        }
        return NULL;
    }

    z_context->pub = pub;
    slist->ssmId = ssm_sid;
    slist->tid = getTID_top(ssm_sid);
    if (getSSM_info(sem_arg->name, sem_arg->suid, &slist->ssize, &slist->hsize, &slist->cycle, &slist->property_size) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("ERROR: Failed to read information for shared memory.\n");
        }
        return NULL;
    }

    while (keep_running) {
        if (sem_arg->active == 0) {
            break;
        }
        slist->tid++;
        if( verbosity_mode >= 2 ) {
            printf("Waiting TID_TOP: %d\n", slist->tid);
        }
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

            if( verbosity_mode >= 2 ) {
                printf("New SHM ID received: %d\n", msg.suid);
            }

            if (msg.cmd_type == MC_CREATE) {
                if (thread_map[msg.suid] != 0) {
                    if( verbosity_mode >= 2 ) {
                        printf("Shared Memory ID %d is already being monitored.\n", msg.suid);
                    }
                    continue;
                }
                // Create a thread to monitor the shared_memory
                shared_memory_arg* sem_arg = (shared_memory_arg *)(malloc(sizeof(shared_memory_arg)));
                sem_arg->suid = msg.suid;
                strncpy( sem_arg->name, msg.name, SSM_SNAME_MAX );
                sem_arg->callback = shared_memory_callback;
                sem_arg->active = (volatile int*)malloc(sizeof(int));
                *(sem_arg->active) = 1;
                sem_arg->z_context = z_context;
                slist = search_ssm_zenoh_list( sem_arg->name, sem_arg->suid );
                if( !slist ) {
                    sem_arg->slist = add_ssm_zenoh_list( NULL, sem_arg->name, sem_arg->suid, 0, 0, 0, 0 );
                }

                if (pthread_create(&thread, NULL, shared_memory_monitor, sem_arg) != 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Could not create thread");
                    }
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
                    if( verbosity_mode >= 2 ) {
                        printf("shm not created yet");
                    }
                } else if (slist->extern_node == 1) {
                    continue;
                }

                char property[msg.ssize];
                if (get_propertySSM(msg.name, msg.suid, property) == 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Failed to get property for shared memory\n");
                    }
                    continue;
                }

                char zenoh_property_key[5 + sizeof(msg.name) + sizeof(msg.suid)];
                if (snprintf(zenoh_property_key, sizeof(zenoh_property_key), "prop/%s/%d", msg.name, msg.suid) < 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Failed to format Zenoh key for property: %s, %d\n", msg.name, msg.suid);
                    }
                    continue;
                }

                z_view_keyexpr_t property_pub_key;
                if (z_view_keyexpr_from_str(&property_pub_key, zenoh_property_key) < 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Failed to create Zenoh key expression from key: %s\n", zenoh_property_key);
                    }
                    continue;
                }

                z_put_options_t property_options;
                z_put_options_default(&property_options);
                z_owned_bytes_t pub_property_attachment;
                if (z_bytes_copy_from_str(&pub_property_attachment, ipv4_address) < 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Failed to create Zenoh options from attachment: %s\n", ipv4_address);
                    }
                    continue;
                }
                property_options.attachment = z_move(pub_property_attachment);

                // Create a Zenoh payload from the current shared memory content
                z_owned_bytes_t b_property_size, b_property;
                z_bytes_copy_from_buf(&b_property_size, (uint8_t*) &msg.ssize, sizeof(size_t));
                z_bytes_copy_from_buf(&b_property, (uint8_t*) property, sizeof(property));

                z_owned_bytes_writer_t property_writer;
                z_bytes_writer_empty(&property_writer);
                if (z_bytes_writer_append(z_loan_mut(property_writer), z_move(b_property_size)) < 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Failed z_bytes_writer_append\n");
                    }
                    continue;
                }
                if (z_bytes_writer_append(z_loan_mut(property_writer), z_move(b_property)) < 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Failed z_bytes_writer_append\n");
                    }
                    continue;
                }

                z_owned_bytes_t pub_property_payload;
                z_bytes_writer_finish(z_move(property_writer), &pub_property_payload);

                if (z_put(z_loan(z_context->session), z_loan(property_pub_key), z_move(pub_property_payload), &property_options) < 0) {
                    if( verbosity_mode >= 1 ) {
                        printf("Error: Failed put property\n");
                    }
                } else {
                    if( verbosity_mode >= 2 ) {
                        printf("Stream property set successfully.\n");
                    }
                }
                z_drop(z_move(pub_property_payload));
                z_drop(z_move(b_property_size));
                z_drop(z_move(b_property));
                z_drop(z_move(property_writer));
                z_drop(z_move(pub_property_attachment));
            } else if (msg.cmd_type == MC_DESTROY) {
                // Stop the corresponding thread
                if (thread_map[msg.suid] != 0) {
                    active_flags[msg.suid] = 0; // Signal the thread to stop
                    thread_map[msg.suid] = 0;
                    if( verbosity_mode >= 2 ) {
                        printf("Thread monitoring shared memory %d stopped.\n", msg.suid);
                    }
                } else {
                    if( verbosity_mode >= 2 ) {
                        printf("No active thread for shared memory %d to stop.\n", msg.suid);
                    }
                }
            }
        } else {
            if( verbosity_mode >= 2 ) {
                printf("No message received yet.\n");
            }
        }

        //usleep(100000); // Sleep briefly to avoid busy-waiting
        sleep(1);
    }
    return NULL;
}

// Callback to handle data
void data_handler(z_loaned_sample_t* data_sample, void* arg) {
    SSM_Zenoh_List *slist;
    const z_loaned_bytes_t* sub_attachment = z_sample_attachment(data_sample);

    // checks if attachment exists
    if (sub_attachment == NULL) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to get attachment from shared memory\n");
        }
        return;
    }

    int ssm_zenoh_tid_top = 0;
    int ssm_zenoh_num = 0;
    char ipv4_zenoh_address[NI_MAXHOST] = "";
    size_t ssm_zenoh_size = 0;
    double ssm_zenoh_cycle = 0;

    z_owned_string_t sub_attachment_string;
    z_bytes_to_string(sub_attachment, &sub_attachment_string);

    if (sscanf(z_string_data(z_loan(sub_attachment_string)), "%[^;];%d;%ld;%d;%lf", ipv4_zenoh_address, &ssm_zenoh_tid_top, &ssm_zenoh_size, &ssm_zenoh_num, &ssm_zenoh_cycle) != 5) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to read attachment for attachment string\n");
        }
        z_drop(z_move(sub_attachment_string));
        return;
    }

    if (strcmp(ipv4_zenoh_address, ipv4_address) == 0) {
        if( verbosity_mode >= 2 ) {
            printf("Own Shared memory\n");
        }
        return;
    }

    z_view_string_t sub_key_string;
    z_view_string_empty(&sub_key_string);
    z_keyexpr_as_view_string(z_sample_keyexpr(data_sample), &sub_key_string);

    char ssm_zenoh_name[SSM_SNAME_MAX];
    int ssm_zenoh_suid = 0;
    if (sscanf(z_string_data(z_loan(sub_key_string)), "data/%[^/]/%d", ssm_zenoh_name, &ssm_zenoh_suid) != 2) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to get topic for shared memory\n");
        }
        return;
    }

    slist = search_ssm_zenoh_list( ssm_zenoh_name, ssm_zenoh_suid );

    if( !slist ) {
        SSM_sid ssm_zenoh_ssm_sid = openSSM(ssm_zenoh_name, ssm_zenoh_suid, SSM_WRITE);
        if (ssm_zenoh_ssm_sid == 0) {
            ssm_zenoh_ssm_sid = createSSMP(ssm_zenoh_name, ssm_zenoh_suid, ssm_zenoh_size, ssm_zenoh_num, ssm_zenoh_cycle);
            if( verbosity_mode >= 2 ) {
                printf("Created new extern Shared Memory name: %s, suid: %d\n", ssm_zenoh_name, ssm_zenoh_suid);
            }
        }
        slist = add_ssm_zenoh_list( ssm_zenoh_ssm_sid, ssm_zenoh_name, ssm_zenoh_suid, ssm_zenoh_size, ssm_zenoh_num, ssm_zenoh_cycle, 1 );
    }

    uint8_t time[sizeof(ssmTimeT)];
    uint8_t data[slist->ssize];

    z_bytes_reader_t reader = z_bytes_get_reader(z_sample_payload(data_sample));
    z_bytes_reader_read(&reader, time, sizeof(ssmTimeT));
    z_bytes_reader_read(&reader, data, sizeof(data));

    ssmTimeT time_value;
    memcpy(&time_value, &time, sizeof(ssmTimeT));

    SSM_tid tid = writeSSM( slist->ssmId, data, time_value );
    if (tid < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to write to shared memory\n");
        }
        return;
    }

    if( verbosity_mode >= 2 ) {
        printf("Write to shared memory successfully.\n");
    }
    z_drop(z_move(sub_attachment_string));
}

// Callback to handel property
void property_handler(z_loaned_sample_t* property_sample, void* arg) {
    SSM_Zenoh_List *slist;
    const z_loaned_bytes_t* sub_property_attachment = z_sample_attachment(property_sample);

    // checks if attachment exists
    if (sub_property_attachment == NULL) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to get attachment for property\n");
        }
        return;
    }

    z_owned_string_t attachment_property_string;
    z_bytes_to_string(sub_property_attachment, &attachment_property_string);
    const char* ipv4_zenoh_address = z_string_data(z_loan(attachment_property_string));

    if (strcmp(ipv4_zenoh_address, ipv4_address) == 0) {
        if( verbosity_mode >= 2 ) {
            printf("Own Property\n");
        }
        z_drop(z_move(attachment_property_string));
        return;
    }

    z_view_string_t sub_property_key_string;
    z_view_string_empty(&sub_property_key_string);
    z_keyexpr_as_view_string(z_sample_keyexpr(property_sample), &sub_property_key_string);

    char ssm_zenoh_property_name[SSM_SNAME_MAX];
    int ssm_zenoh_property_suid = 0;
    if (sscanf(z_string_data(z_loan(sub_property_key_string)), "prop/%[^/]/%d", ssm_zenoh_property_name, &ssm_zenoh_property_suid) != 2) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to get topic for shared memory\n");
        }
        z_drop(z_move(attachment_property_string));
        return;
    }

    slist = search_ssm_zenoh_list( ssm_zenoh_property_name, ssm_zenoh_property_suid );

    if( !slist ) {
        if( verbosity_mode >= 2 ) {
            printf("There is no shared memory for the property\n");
        }
        z_drop(z_move(attachment_property_string));
        return;
    }

    uint8_t property_size[sizeof(size_t)];

    z_bytes_reader_t property_reader = z_bytes_get_reader(z_sample_payload(property_sample));
    z_bytes_reader_read(&property_reader, property_size, sizeof(size_t));
    size_t property_size_value;
    memcpy(&property_size_value, &property_size, sizeof(size_t));

    uint8_t property[property_size_value];
    z_bytes_reader_read(&property_reader, property, sizeof(property));

    if (set_propertySSM(ssm_zenoh_property_name, ssm_zenoh_property_suid, property, property_size_value) == 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed to set property\n");
        }
        return;
    }

    if( verbosity_mode >= 2 ) {
        printf("Write to property successfully.\n");
    }
    z_drop(z_move(attachment_property_string));
}


// Function to monitor Zenoh messages
void* zenoh_message_monitor(void* arg) {
    zc_init_log_from_env_or("error");
    zenoh_context* z_context = (zenoh_context*)arg;

    char sub_data_keyexpr[] = "data/**";
    z_view_keyexpr_t sub_key;
    z_view_keyexpr_from_str_unchecked(&sub_key, sub_data_keyexpr);

    z_owned_closure_sample_t data_callback;
    z_closure(&data_callback, data_handler, NULL, NULL);
    if( verbosity_mode >= 2 ) {
        printf("Declaring Subscriber on '%s'...\n", sub_data_keyexpr);
    }
    z_owned_subscriber_t data_sub;
    if (z_declare_subscriber(z_loan(z_context->session), &data_sub, z_loan(sub_key), z_move(data_callback), NULL)) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Unable to declare subscriber for data processing.\n");
        }
        exit(-1);
    }

    char sub_property_keyexpr[] = "prop/**";
    z_view_keyexpr_t property_sub_key;
    z_view_keyexpr_from_str_unchecked(&property_sub_key, sub_property_keyexpr);

    z_owned_closure_sample_t property_callback;
    z_closure(&property_callback, property_handler, NULL, NULL);
    if( verbosity_mode >= 2 ) {
        printf("Declaring Subscriber on '%s'...\n", sub_property_keyexpr);
    }
    z_owned_subscriber_t property_sub;
    if (z_declare_subscriber(z_loan(z_context->session), &property_sub, z_loan(property_sub_key), z_move(property_callback), NULL)) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Unable to declare subscriber for property processing.\n");
        }
        exit(-1);
    }

    while (keep_running) {
        z_sleep_s(1);
    }

    z_drop(z_move(data_sub));
    z_drop(z_move(data_callback));
    z_drop(z_move(property_sub));
    z_drop(z_move(property_callback));
    return NULL;
}

int print_help( char *name )
{
    fprintf( stderr, "HELP\n" );
    fprintf( stderr, "\t-v | --verbose              : print verbose.\n" );
    fprintf( stderr, "\t-q | --quiet                : print quiet.\n" );
    fprintf( stderr, "\t   | --version              : print version.\n" );
    fprintf( stderr, "\t-h | --help                 : print this help.\n" );
    fprintf( stderr, "\t-c | --config <file>        : specify config file.\n" );

    fprintf( stderr, "ex)\n\t%s\n", name );
    return 0;
}

int arg_analyze( int argc, char **argv )
{
    int opt, optIndex = 0, optFlag = 0;
    static char abs_path[PATH_MAX];
    struct option longOpt[] = {
        {"version", no_argument, &optFlag, 'V'},
        {"quiet", no_argument, 0, 'q'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"config", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    while( ( opt = getopt_long( argc, argv, "vqhc:", longOpt, &optIndex ) ) != -1 )
    {
        switch ( opt )
        {
            case 'v':
                verbosity_mode = 2;
            break;
            case 'q':
                verbosity_mode = 0;
            break;
            case 'c':
                if (optarg) {
                    if (realpath(optarg, abs_path) != NULL) {
                        zenoh_config_path = abs_path;
                    } else {
                        fprintf(stderr, "Error: Unable to resolve absolute path for %s\n", optarg);
                        return 0;
                    }
                }
            break;
            case 'h':
                print_help( argv[0] );
            return 0;
            break;
            case 0:
            {
                switch ( optFlag )
                {
                    case 'V':
                        printf( " Ver. %s\n", PACKAGE_VERSION );
                    return 0;
                    break;
                    default:
                        break;
                }
            }
            break;
            default:
            {
                fprintf( stderr, "help : %s -h\n", argv[0] );
                return 0;
            }
            break;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    // Register signal handler for graceful shutdown
    signal(SIGINT, handle_sigint);

    if( !arg_analyze( argc, argv ) )
        return 1;
    if( verbosity_mode >= 1 ) {
        printf( "\n" );
        printf( " --------------------------------------------------\n" );
        printf( " SSM-Zenoh ( Streaming data Sharing Manager - Zenoh )\n" );
        printf( " Ver. %s\n", PACKAGE_VERSION );
        printf( " --------------------------------------------------\n\n" );
    }

    if( !ssm_zenoh_ini(  ) || !initSSM() )
		return -1;

    // Configure Zenoh session
    z_owned_config_t config;
    if (zenoh_config_path != NULL) {
        if (zc_config_from_file(&config, zenoh_config_path) < 0) {
            printf( "Error: Unable to load config file %s\n", zenoh_config_path );
            exit(1);
        }
    } else {
        z_config_default(&config);
    }
    z_owned_session_t session;
    if (z_open(&session, z_move(config), NULL) < 0) {
        if( verbosity_mode >= 1 ) {
            printf("Unable to open Zenoh session!\n");
        }
        exit(1);
    }

    zenoh_context z_context;
    z_context.session = session;

    // Start the message queue monitoring thread
    pthread_t msg_thread;
    if (pthread_create(&msg_thread, NULL, message_queue_monitor, &z_context) != 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed creating message queue thread");
        }
        exit(1);
    }

    // Start the Zenoh message monitoring thread
    pthread_t zenoh_thread;
    if (pthread_create(&zenoh_thread, NULL, zenoh_message_monitor, &z_context) != 0) {
        if( verbosity_mode >= 1 ) {
            printf("Error: Failed creating Zenoh message thread");
        }
        exit(1);
    }

    // Wait for the threads to finish
    pthread_join(msg_thread, NULL);
    pthread_join(zenoh_thread, NULL);

    z_drop(z_move(session));
    z_drop(z_move(config));
    endSSM();

    return 1;
}
