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

#include "ssm.h"
#include "libssm.h"
#include "ssm-time.h"
#include "../external/zenoh-c/include/zenoh.h"

using namespace std;

#define MAX_SHM_SEGMENTS 10   // Maximum number of shared memory segments
#define SHM_SIZE 256          // Size of each shared memory segment
#define BASE_SHM_KEY 1000     // Base key for shared memory segments
#define ZENOH_KEY_PREFIX "shm/topic" // Zenoh key prefix for data distribution

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
    z_view_string_t key_string;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &key_string);

    z_owned_string_t payload_string;
    z_bytes_to_string(z_sample_payload(sample), &payload_string);

    printf(">> [Subscriber] Received %s ('%.*s': '%.*s')", kind_to_str(z_sample_kind(sample)),
           (int)z_string_len(z_loan(key_string)), z_string_data(z_loan(key_string)),
           (int)z_string_len(z_loan(payload_string)), z_string_data(z_loan(payload_string)));

    const z_loaned_bytes_t* attachment = z_sample_attachment(sample);
    // checks if attachment exists
    if (attachment != NULL) {
        z_owned_string_t attachment_string;
        z_bytes_to_string(attachment, &attachment_string);
        printf(" (%.*s)", (int)z_string_len(z_loan(attachment_string)), z_string_data(z_loan(attachment_string)));
        z_drop(z_move(attachment_string));
    }
    printf("\n");
    z_drop(z_move(payload_string));
}

void subscriber() {
    zc_init_log_from_env_or("error");

    char keyexpr[] = "**";

    z_owned_config_t config;
    z_config_default(&config);
    z_owned_session_t s;
    if (z_open(&s, z_move(config), NULL) < 0) {
        printf("Unable to open session!\n");
        exit(-1);
    }

    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str(&ke, keyexpr);

    z_owned_closure_sample_t callback;
    z_closure(&callback, data_handler, NULL, NULL);
    printf("Declaring Subscriber on '%s'...\n", keyexpr);
    z_owned_subscriber_t sub;
    if (z_declare_subscriber(z_loan(s), &sub, z_loan(ke), z_move(callback), NULL)) {
        printf("Unable to declare subscriber.\n");
        exit(-1);
    }

    printf("Press CTRL-C to quit...\n");
    while (1) {
        z_sleep_s(1);
    }

    z_drop(z_move(sub));
    z_drop(z_move(s));
};

void publish_simple() {
    zc_init_log_from_env_or("error");

    char keyexpr[] = "test/topic";
    char attachment_ssm[] = "more information";

    z_owned_config_t config;
    z_config_default(&config);
    z_owned_session_t s;
    if (z_open(&s, z_move(config), NULL) < 0) {
        printf("Unable to open session!\n");
        exit(-1);
    }

    z_owned_publisher_t pub;
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str(&ke, keyexpr);

    if (z_declare_publisher(z_loan(s), &pub, z_loan(ke), NULL) < 0) {
        printf("Unable to declare Publisher for key expression!\n");
        exit(-1);
    }

    printf("Press CTRL-C to quit...\n");
    char buf[256] = {};
    for (int idx = 0; 1; ++idx) {
        z_sleep_s(1);
        sprintf(buf, "[%4d] %s", idx, "test value");
        printf("Putting Data ('%s': '%s')...\n", keyexpr, buf);
        z_publisher_put_options_t options;
        z_publisher_put_options_default(&options);

        z_owned_bytes_t payload;
        z_bytes_copy_from_str(&payload, buf);
        z_owned_bytes_t attachment;
        if (sizeof(attachment_ssm) != 0) {
            z_bytes_copy_from_str(&attachment, attachment_ssm);
            options.attachment = z_move(attachment);
        }
        /// optional encoding
        // z_owned_encoding_t encoding;
        // z_encoding_clone(&encoding, z_encoding_text_plain());
        // options.encoding = z_move(encoding);

        z_publisher_put(z_loan(pub), z_move(payload), &options);
    }

    z_drop(z_move(pub));
    z_drop(z_move(s));
};

void publish_shm() {
    zc_init_log_from_env_or("error");

    char keyexpr[] = "shm/test/topic";
    unsigned int size = 256;
    unsigned long long shared_memory_size_mb = 32;

    z_owned_config_t config;
    z_config_default(&config);
    z_owned_session_t s;
    if (z_open(&s, z_move(config), NULL) < 0) {
        printf("Unable to open session!\n");
        exit(-1);
    }

    z_publisher_options_t options;
    z_publisher_options_default(&options);
    options.congestion_control = Z_CONGESTION_CONTROL_BLOCK;

    z_owned_publisher_t pub;
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str(&ke, keyexpr);
    if (z_declare_publisher(z_loan(s), &pub, z_loan(ke), &options)) {
        printf("Unable to declare publisher for key expression!\n");
        exit(-1);
    }

    printf("Creating POSIX SHM Provider...\n");
    z_alloc_alignment_t alignment = {0};
    z_owned_memory_layout_t layout;
    z_memory_layout_new(&layout, shared_memory_size_mb * 1024 * 1024, alignment);
    z_owned_shm_provider_t provider;
    z_posix_shm_provider_new(&provider, z_loan(layout));

    printf("Allocating single SHM buffer\n");
    z_buf_layout_alloc_result_t alloc;
    z_shm_provider_alloc(&alloc, z_loan(provider), size, alignment);
    if (alloc.status != ZC_BUF_LAYOUT_ALLOC_STATUS_OK) {
        printf("Unexpected failure during SHM buffer allocation...\n");
        exit(-1);
    }
    memset(z_shm_mut_data_mut(z_loan_mut(alloc.buf)), 1, size);
    z_owned_shm_t shm;
    z_shm_from_mut(&shm, z_move(alloc.buf));

    z_owned_bytes_t shmbs;
    if (z_bytes_from_shm(&shmbs, z_move(shm)) != Z_OK) {
        printf("Unexpected failure during SHM buffer serialization...\n");
        exit(-1);
    }

    while (1) {
        z_owned_bytes_t payload;
        z_bytes_clone(&payload, z_loan(shmbs));

        z_publisher_put(z_loan(pub), z_move(payload), NULL);
    }

    z_drop(z_move(pub));
    z_drop(z_move(s));

    z_drop(z_move(shm));
    z_drop(z_move(provider));
    z_drop(z_move(layout));
};

#define NUM_SHM_SEGMENTS 5  // Number of shared memory segments
#define SHM_SIZE 256        // Size of each shared memory segment
#define BASE_SHM_KEY 1000   // Base key for shared memory segments

void publisher() {
    int shm_ids[NUM_SHM_SEGMENTS];
    char *shm_ptrs[NUM_SHM_SEGMENTS];

    // Initialize shared memory segments
    for (int i = 0; i < NUM_SHM_SEGMENTS; i++) {
        int shm_key = BASE_SHM_KEY + i;

        // Create or access shared memory
        shm_ids[i] = shmget(shm_key, SHM_SIZE, IPC_CREAT | 0666);
        if (shm_ids[i] < 0) {
            perror("shmget");
            exit(1);
        }

        // Attach the shared memory to the process
        shm_ptrs[i] = (char *)shmat(shm_ids[i], NULL, 0);
        if (shm_ptrs[i] == (char *)-1) {
            perror("shmat");
            exit(1);
        }

        // Initialize shared memory with empty data
        memset(shm_ptrs[i], 0, SHM_SIZE);
        printf("Initialized SHM segment %d with key %d\n", i, shm_key);
    }

    // Write to shared memory in a round-robin manner
    int counter = 0;
    while (1) {
        for (int i = 0; i < NUM_SHM_SEGMENTS; i++) {
            snprintf(shm_ptrs[i], SHM_SIZE, "Message %d in SHM segment %d", counter++, i);
            printf("Written to SHM[%d]: %s\n", i, shm_ptrs[i]);
            sleep(1);  // Simulate a delay between writes
        }
    }

    // Cleanup (not reached in this infinite loop)
    for (int i = 0; i < NUM_SHM_SEGMENTS; i++) {
        shmdt(shm_ptrs[i]);
        shmctl(shm_ids[i], IPC_RMID, NULL);
    }
}

class Edge {
public:
    Edge(const char *name, int suid, SSM_sid ssmId, size_t size, int num, double cycle, size_t property_size,
         int zenoh_tid_top, int ref_count) {
        strncpy(this->name, name, SSM_SNAME_MAX);
        this->suid = suid;
        this->ssmId = ssmId;
        this->size = size;
        this->num = num;
        this->cycle = cycle;
        this->property_size = property_size;
        this->zenoh_tid_top = zenoh_tid_top;
        this->ref_count = ref_count;
    }

    char name[SSM_SNAME_MAX];
    int suid;
    SSM_sid ssmId;
    size_t size;
    int num;
    double cycle;
    size_t property_size;
    int zenoh_tid_top;
    int ref_count;

    void toString() {
        printf("%14.14s | %4d | %lu | %5d | %8.3f | %lu\n", this->name, this->suid, this->size, this->num, this->cycle,
               this->property_size);
    }
};

typedef list<Edge> edgeArrayT;
edgeArrayT edge;
int edge_ref_count = 0;

typedef struct {
    int shm_id;
    char *shm_ptr;
    char last_data[SHM_SIZE];
    char zenoh_key[128];
} ShmSegment;

void publisher_shm_zenoh() {
    // Initialize Zenoh
    zc_init_log_from_env_or("error");

    // Configure Zenoh session
    z_owned_config_t config;
    z_config_default(&config);
    z_owned_session_t session;
    if (z_open(&session, z_move(config), NULL) < 0) {
        printf("Unable to open Zenoh session!\n");
        exit(1);
    }

    // Initialize shared memory segments
    ShmSegment shm_segments[MAX_SHM_SEGMENTS];
    int shm_count = 5; // Number of shared memory segments to manage
    for (int i = 0; i < shm_count; i++) {
        // Create or access a shared memory segment
        shm_segments[i].shm_id = shmget(BASE_SHM_KEY + i, SHM_SIZE, IPC_CREAT | 0666);
        if (shm_segments[i].shm_id < 0) {
            perror("shmget");
            exit(1);
        }

        // Attach the shared memory segment to the process
        shm_segments[i].shm_ptr = (char *) shmat(shm_segments[i].shm_id, NULL, 0);
        if (shm_segments[i].shm_ptr == (char *) -1) {
            perror("shmat");
            exit(1);
        }

        // Initialize last known data and generate a unique Zenoh key for each segment
        memset(shm_segments[i].last_data, 0, SHM_SIZE);
        snprintf(shm_segments[i].zenoh_key, sizeof(shm_segments[i].zenoh_key), "%s/%d", ZENOH_KEY_PREFIX, i);
        printf("Initialized SHM Key: %d, Zenoh Key: %s\n", BASE_SHM_KEY + i, shm_segments[i].zenoh_key);
    }

    // Monitor shared memory segments and publish updates via Zenoh
    while (1) {
        for (int i = 0; i < shm_count; i++) {
            // Check if data in the shared memory has changed
            if (strncmp(shm_segments[i].last_data, shm_segments[i].shm_ptr, SHM_SIZE) != 0) {
                // Update the last known data
                strncpy(shm_segments[i].last_data, shm_segments[i].shm_ptr, SHM_SIZE);

                // Create a Zenoh payload from the current shared memory content
                z_owned_bytes_t payload;
                z_bytes_copy_from_str(&payload, shm_segments[i].shm_ptr);

                // Publish the data
                z_owned_publisher_t pub;
                z_view_keyexpr_t ke;
                z_view_keyexpr_from_str(&ke, shm_segments[i].zenoh_key);
                if (z_declare_publisher(z_loan(session), &pub, z_loan(ke), NULL) < 0) {
                    printf("Unable to declare Publisher for key expression %s!\n", shm_segments[i].zenoh_key);
                    continue;
                }

                if (z_publisher_put(z_loan(pub), z_move(payload), NULL) < 0) {
                    printf("Failed to publish data for key %s!\n", shm_segments[i].zenoh_key);
                }

                printf("Published: %s -> %s\n", shm_segments[i].zenoh_key, shm_segments[i].shm_ptr);
                z_drop(z_move(pub));
            }
        }

        // Sleep for a short time to reduce CPU usage
        usleep(100000); // 100 ms
    }

    // Cleanup
    for (int i = 0; i < shm_count; i++) {
        shmdt(shm_segments[i].shm_ptr);
        shmctl(shm_segments[i].shm_id, IPC_RMID, NULL);
    }
    z_drop(z_move(session));
}

Edge *get_edge(const char *name, int suid) {
    for (auto &jt: edge) {
        if (strcmp(jt.name, name) == 0 || jt.suid == suid) {
            return &jt;
        }
    }
    return NULL;
}


void update_edges() {
    edgeArrayT::iterator jt = edge.begin();
    while (jt != edge.end()) {
        if (jt->ref_count < edge_ref_count) {
            closeSSM(&jt->ssmId);
            jt = edge.erase(jt);
        } else
            jt++;
    }
}

void print_edges() {
    printf("Current edges:\n");
    for (const auto &e: edge) {
        printf("Name: %s, Ref Count: %d\n", e.name, e.ref_count);
    }
}


int get_ssm_node_info() {
    edge_ref_count++;
    int sid_search = 0;
    char name[100];
    int suid, num;
    size_t size, property_size;
    double cycle;

    while (getSSM_name(sid_search, name, &suid, &size) >= 0) {
        if (getSSM_info(name, suid, &size, &num, &cycle, &property_size) < 0) {
            printf("ERROR: SSM read error.\n");
            return -1;
        }

        printf("sid: %d, name: %s, suid: %d, size: %lu\n", sid_search, name, suid, size);


        Edge *result = get_edge(name, suid);
        if (result == NULL) {
            SSM_sid ssmId = openSSM(name, suid, SSM_READ);
            if (ssmId == 0) {
                continue;
            }
            SSM_tid tid_top = getTID_top( ssmId );
            edge.push_back(Edge(name, suid, ssmId, size, num, cycle, property_size, tid_top, edge_ref_count));
        } else {
            result->ref_count = edge_ref_count;
            result->toString();
        }

        sid_search++;
    }

    update_edges();
    return 1;
}

int main(int argc, char **argv) {
    publisher();

    return 0;
}
