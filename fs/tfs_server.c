#include "operations.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>

// Which sessions are turned on, the handle for the pipe of every client, 
// and whether or not we have written to the working thread
int sessions[MAX_SESSIONS] = {0}, fclient[MAX_SESSIONS] = {0}, written[MAX_SESSIONS] = {0};
// The name of every client pipe and the parent-child buffer
char *client_pipename[MAX_SESSIONS], *client_buffer[MAX_SESSIONS];
// Conditional variables
pthread_cond_t conditions[MAX_SESSIONS];
// Working threads
pthread_t threads[MAX_SESSIONS];
// Locks
pthread_mutex_t locks[MAX_SESSIONS];

// Initialize all char vectors
void initalize_clients() {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        client_pipename[i] = (char*) malloc(sizeof(char) * MAX_NAME);
        client_buffer[i] = (char*) malloc(sizeof(char) * MAX_BUFFER);
        client_buffer[i][0] = '\0';
    }
}

int tfs_mount(const char *client_pipe_path) {
    int session_id = -1, temp = 0, error = -1;
    // Search for a free session
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i] == 0) {
            sessions[i] = 1;
            session_id = i;
            break;
        }
    }
    
    temp = open(client_pipe_path, O_WRONLY);
    // If all sessions are taken
    if (session_id == -1) {
        // Write back to client
        if (write(temp, &error, sizeof(int)) == -1) return -1;
        unlink(client_pipe_path);
        close(temp);
        return -1;
    }
    else {
        fclient[session_id] = temp;
        // Write back to client
        if (write(temp, &session_id, sizeof(int)) == -1) return -1;
        strcpy(client_pipename[session_id], client_pipe_path);
        return 0;
    }
}

int tfs_mount_filter(const char *buffer) {
    char name[MAX_NAME];

    // Filter the mount buffer
    memcpy(name, buffer, sizeof(char)*MAX_NAME);
    buffer += sizeof(char)*MAX_NAME;

    return tfs_mount(name);
}

int tfs_unmount(int session_id) {
    sessions[session_id] = 0;
    // Write back to client
    if (write(fclient[session_id], &sessions[session_id], sizeof(int)) == -1) return -1;
    // Close all pipes
    unlink(client_pipename[session_id]);
    close(fclient[session_id]);
    // Mark session as closed
    fclient[session_id] = 0;
    written[session_id] = 0;
    memset(client_pipename[session_id], '\0', MAX_NAME);
    memset(client_buffer[session_id], '\0', MAX_BUFFER);
    return 0;
}

int tfs_unmount_filter(int session_id) {
    return tfs_unmount(session_id);
}

int tfs_open_filter(const char *buffer, int session_id) {
    int flags = 0, error_check = 0;
    char name[MAX_NAME];

    // Filter the open buffer
    memcpy(name, buffer, sizeof(char)*MAX_NAME);
    buffer += sizeof(char)*MAX_NAME;
    memcpy(&flags, buffer, sizeof(int));
    buffer += sizeof(int);

    error_check = tfs_open(name, flags);
    // Write back to client
    if (write(fclient[session_id], &error_check, sizeof(int)) == -1) return -1;

    return error_check;
}

int tfs_close_filter(const char *buffer, int session_id) {
    int fhandle = 0, error_check = 0;

    // Filter the close buffer
    memcpy(&fhandle, buffer, sizeof(int));
    buffer += sizeof(int);
    
    error_check = tfs_close(fhandle);
    // Write back to client
    if (write(fclient[session_id], &error_check, sizeof(int)) == -1) return -1;

    return error_check;
}

ssize_t tfs_write_filter(const char *buffer, int session_id) {
    int fhandle = 0;
    size_t len = 0;
    ssize_t error_check = 0;
    char *buffer_content;

    // Filter the write buffer
    memcpy(&fhandle, buffer, sizeof(int));
    buffer += sizeof(int);
    memcpy(&len, buffer, sizeof(size_t));
    buffer += sizeof(size_t);
    buffer_content = (char*) malloc(sizeof(char)*len);
    memcpy(buffer_content, buffer, sizeof(char)*len);
    buffer += sizeof(char)*len;

    error_check = tfs_write(fhandle, buffer_content, len);
    // Write back to client
    if (write(fclient[session_id], &error_check, sizeof(int)) == -1) return -1;

    return error_check;
}

ssize_t tfs_read_filter(const char *buffer, int session_id) {
    int fhandle = 0;
    size_t len = 0;
    ssize_t error_check = 0;
    char *buffer_content, *buffer_ans;

    // Filter the read buffer
    memcpy(&fhandle, buffer, sizeof(int));
    buffer += sizeof(int);
    memcpy(&len, buffer, sizeof(size_t));
    buffer += sizeof(size_t);
    // Buffer with what we read
    buffer_content = (char*) malloc(sizeof(char)*len);
    // Buffer to be written back to client
    buffer_ans = (char*) malloc(sizeof(char)*len + sizeof(int));

    error_check = tfs_read(fhandle, buffer_content, len);
    memcpy(buffer_ans, &error_check, sizeof(int));
    buffer_ans += sizeof(int);
    if (error_check != -1) memcpy(buffer_ans, buffer_content, sizeof(char)*len);
    buffer_ans -= sizeof(int);

    // Write back to client
    if (write(fclient[session_id], buffer_ans, sizeof(char)*len+sizeof(int)) == -1) return -1;

    return error_check;
}

int tfs_shutdown_filter(int session_id) {
    int error_check = 0;

    error_check = tfs_destroy_after_all_closed();
    // Write back to client
    if (write(fclient[session_id], &error_check, sizeof(int)) == -1) return -1;

    return error_check;
}

// Run a thread in for a specific session
void* thread_operations(void* id) {
    // Transform given input into an integer
    int* id2 = (int*) id;
    int session_id = *id2;

    while(1) {
        char *buffer = (char*) malloc(sizeof(char)*MAX_BUFFER), OP_CODE;

        pthread_mutex_lock(&locks[session_id]);
        while (written[session_id] == 0) pthread_cond_wait(&conditions[session_id], &locks[session_id]);
        pthread_mutex_unlock(&locks[session_id]);

        memcpy(buffer, client_buffer[session_id], sizeof(char)*MAX_BUFFER);
        memcpy(&OP_CODE, buffer, sizeof(char));
        buffer++;

        switch (OP_CODE) {
            case TFS_OP_CODE_UNMOUNT:
                tfs_unmount_filter(session_id);
                break;
            case TFS_OP_CODE_OPEN:
                tfs_open_filter(buffer, session_id);
                break;
            case TFS_OP_CODE_CLOSE:
                tfs_close_filter(buffer, session_id);
                break;
            case TFS_OP_CODE_WRITE:
                tfs_write_filter(buffer, session_id);
                break;
            case TFS_OP_CODE_READ:
                tfs_read_filter(buffer, session_id);
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                tfs_shutdown_filter(session_id);
                break;
            default:
                break;
        }
        client_buffer[session_id][0] = '\0';
        written[session_id] = 0;
    }
}

// Run the main server process
int main(int argc, char **argv) {

    int fserv = 0;

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);
    // Create the server FIFO
    if (mkfifo(pipename, 0640) < 0) return -1;
    fserv = open(pipename, O_RDONLY);
    if (fserv == -1) return -1;

    // Initialize all threads
    for (int i = 0; i < MAX_SESSIONS; i++) {
        int x = i - 1;
        if (pthread_create(&threads[i], NULL, thread_operations, &x) != 0) return -1;
        if (pthread_cond_init(&conditions[i], NULL) != 0) return -1;
        if (pthread_mutex_init(&locks[i], NULL) != 0) return -1;
    }

    tfs_init();
    initalize_clients();

    while(1) {
        char *buffer = (char*) malloc(sizeof(char)*MAX_BUFFER), OP_CODE;
        int session_id = 0;
        ssize_t read_value = 0;

        if ((read_value = read(fserv, buffer, MAX_BUFFER)) == 0) {
            close(fserv);
            fserv = open(pipename, O_RDONLY);
            if (fserv == -1) return -1;
        } else if (read_value == -1) {
            close(fserv);
            exit(EXIT_FAILURE);
        } else if (read_value > 0) {
            memcpy(&OP_CODE, buffer, sizeof(char));
            buffer += sizeof(char);

            switch (OP_CODE) {
                case TFS_OP_CODE_MOUNT:
                    tfs_mount_filter(buffer);
                    break;
                default:
                    memcpy(&session_id, buffer, sizeof(int));
                    buffer += sizeof(int);
                    memcpy(client_buffer[session_id], &OP_CODE, sizeof(char));
                    memcpy(client_buffer[session_id] + sizeof(char), buffer, sizeof(char)*MAX_BUFFER);
                    written[session_id] = 1;
                    pthread_cond_broadcast(&conditions[session_id]);
                    break;
            }
        }
    }

    return 0;
}