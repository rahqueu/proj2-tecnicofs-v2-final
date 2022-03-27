#include "tecnicofs_client_api.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// Session id, handle of server pipe, handle of client pipe
int session_id = -1, fserv, fclient;
// Client pipename
char const *client_pipename;

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    char *buffer = (char*) malloc(sizeof(char)*(MAX_NAME+1));
    char OP_CODE = TFS_OP_CODE_MOUNT;

    if ((fserv = open(server_pipe_path, O_WRONLY)) == -1) return -1;

    // Open the client FIFO
    if (mkfifo(client_pipe_path, 0640) < 0) return -1;

    memcpy(buffer, &OP_CODE, sizeof(char));
    buffer += sizeof(char);
    memcpy(buffer, client_pipe_path, sizeof(char)*MAX_NAME);
    buffer -= sizeof(char);

    // Write to server
    if (write(fserv, buffer, sizeof(char)+sizeof(char)*MAX_NAME) == -1) return -1;

    if ((fclient = open(client_pipe_path, O_RDONLY)) == -1) return -1;

    // Read from client pipe  
    if (read(fclient, &session_id, sizeof(int)) == -1) return -1;

    client_pipename = client_pipe_path;

    return session_id == -1 ? -1 : 0;
}

int tfs_unmount() {
    char *buffer = (char*) malloc(sizeof(char)+sizeof(int)), OP_CODE = TFS_OP_CODE_UNMOUNT;
    int _return;

    memcpy(buffer, &OP_CODE, sizeof(char));
    buffer += sizeof(char);
    memcpy(buffer, &session_id, sizeof(int));
    buffer -= sizeof(char);

    // Write to server
    if (write(fserv, buffer, sizeof(char)+sizeof(char)*MAX_NAME) == -1) return -1;
    // Read from client pipe
    if (read(fclient, &_return, sizeof(int)) == -1) return -1;
    
    return _return;
}

int tfs_open(char const *name, int flags) {
    char *buffer = (char*) malloc(sizeof(char)+sizeof(int)+sizeof(char)*MAX_NAME+sizeof(int));
    char OP_CODE = TFS_OP_CODE_OPEN;
    int _return;

    memcpy(buffer, &OP_CODE, sizeof(char));
    buffer += sizeof(char);
    memcpy(buffer, &session_id, sizeof(int));
    buffer += sizeof(int);
    memcpy(buffer, name, sizeof(char)*MAX_NAME);
    buffer += sizeof(char)*MAX_NAME;
    memcpy(buffer, &flags, sizeof(int));
    buffer -= (sizeof(char)*MAX_NAME + sizeof(int) + sizeof(char));

    // Write to server
    if (write(fserv, buffer, sizeof(char)+sizeof(int)+sizeof(char)*MAX_NAME+sizeof(int)) == -1) return -1;
    // Read from client pipe
    if (read(fclient, &_return, sizeof(int)) == -1) return -1;

    return _return;
}

int tfs_close(int fhandle) {
    char *buffer = (char*) malloc(sizeof(char)+sizeof(int)+sizeof(int));
    char buffer_serv[sizeof(int)], OP_CODE = TFS_OP_CODE_CLOSE;
    int _return;

    memcpy(buffer, &OP_CODE, sizeof(char));
    buffer += sizeof(char);
    memcpy(buffer, &session_id, sizeof(int));
    buffer += sizeof(int);
    memcpy(buffer, &fhandle, sizeof(int));
    buffer -= (sizeof(char)+sizeof(int));

    // Write to server
    if (write(fserv, buffer, sizeof(char)+sizeof(int)+sizeof(int)) == -1) return -1;
    // Read from client pipe
    if (read(fclient, buffer_serv, sizeof(int)) == -1) return -1;

    memcpy(&_return, buffer_serv, sizeof(int));
    return _return;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    char *buffer_client = (char*)malloc(sizeof(char)+sizeof(int)+sizeof(int)+sizeof(size_t)+sizeof(char)*len);
    char OP_CODE = TFS_OP_CODE_WRITE;
    int _return;

    memcpy(buffer_client, &OP_CODE, sizeof(char));
    buffer_client += sizeof(char);
    memcpy(buffer_client, &session_id, sizeof(int));
    buffer_client += sizeof(int);
    memcpy(buffer_client, &fhandle, sizeof(int));
    buffer_client += sizeof(int);
    memcpy(buffer_client, &len, sizeof(size_t));
    buffer_client += sizeof(size_t);
    memcpy(buffer_client, buffer, sizeof(char)*len);
    buffer_client -= (sizeof(size_t)+sizeof(int)+sizeof(int)+sizeof(char));

    // Write to server
    if (write(fserv, buffer_client, sizeof(char)+sizeof(int)+sizeof(int)+sizeof(size_t)+sizeof(char)*len) == -1) return -1;
    // Read from client pipe
    if (read(fclient, &_return, sizeof(int)) == -1) return -1;

    return _return;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    char *buffer_client = (char*) malloc(sizeof(char)+sizeof(int)+sizeof(int)+sizeof(size_t)), OP_CODE = TFS_OP_CODE_READ, 
    *temp_buffer = (char*) malloc(sizeof(char)*len + sizeof(int));
    int bytes = 0;

    memcpy(buffer_client, &OP_CODE, sizeof(char));
    buffer_client += sizeof(char);
    memcpy(buffer_client, &session_id, sizeof(int));
    buffer_client += sizeof(int);
    memcpy(buffer_client, &fhandle, sizeof(int));
    buffer_client += sizeof(int);
    memcpy(buffer_client, &len, sizeof(size_t));
    buffer_client -= (sizeof(int)+sizeof(int)+sizeof(char));

    // Write to server
    if (write(fserv, buffer_client, sizeof(char)+sizeof(int)+sizeof(int)+sizeof(size_t)) == -1) return -1;
    // Read from client pipe
    if (read(fclient, temp_buffer, sizeof(int) + sizeof(char)*len) == -1) return -1;

    memcpy(&bytes, temp_buffer, sizeof(int));
    temp_buffer += sizeof(int);
    if (bytes != -1) memcpy(buffer, temp_buffer, sizeof(char)*len);

    return bytes;
}

int tfs_shutdown_after_all_closed() {
    char *buffer = (char*) malloc(sizeof(char)+sizeof(int));
    char OP_CODE = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    int _return;

    memcpy(buffer, &OP_CODE, sizeof(char));
    buffer += sizeof(char);
    memcpy(buffer, &session_id, sizeof(int));
    buffer -= sizeof(char);

    // Write to server
    if (write(fserv, buffer, sizeof(char)+sizeof(int)) == -1) return -1;
    // Read from client pipe
    if (read(fclient, &_return, sizeof(int)) == -1) return -1;

    return _return;
}