#ifndef SERVER_H
#define SERVER_H

#include <time.h>

#define BUFFER_SIZE (1024 * 1024)
#define MAX_HEADERS 64
#define MAX_EVENTS 1024

#define MAX_PATH_LENGTH 1024
#define MAX_METHOD_LENGTH 16
#define MAX_URI_LENGTH 2048
#define MAX_REQUEST_SIZE (128 * 1024)
#define MAX_HEADER_COUNT 32
#define MAX_HEADER_LENGTH 2048

#define CONNECTION_POOL_SIZE 1000
#define BUFFER_POOL_SIZE 100
#define SENDFILE_CHUNK_SIZE (64 * 1024)
#define KEEPALIVE_TIMEOUT 30
#define MAX_KEEPALIVE_REQUESTS 100

typedef enum
{
    OK_OK = 200,
    OK_CREATED = 201,
    OK_NOCONTENT = 204,
    ERR_AUTH = 401,
    ERR_NOTFOUND = 404,
    ERR_BADREQ = 400,
    ERR_UNPROC = 422,
    ERR_INTERR = 500,
} response_status_t;

typedef enum {
    SERVER_OK = 0,
    SERVER_ERR_MEMORY,
    SERVER_ERR_NETWORK,
    SERVER_ERR_FILE,
    SERVER_ERR_PROTOCOL,
    SERVER_ERR_SECURITY,
    SERVER_ERR_RESOURCE
} server_status_t;

typedef struct
{
    response_status_t status;
    const char *message;
} response_info_t;

typedef struct Buffer {
    char* data;
    size_t size;
    struct Buffer* next;
} buffer_t;

typedef struct {
    buffer_t* free_buffers;
    size_t count;
} buffer_pool_t;

typedef struct client_con {
    int fd;
    time_t last_activity;
    int keepalive_requests;
    struct client_con* next;
} client_con_t;

typedef struct {
    client_con_t* active_connections;
    client_con_t* free_connections;
    size_t active_count;
    size_t total_count;
} connection_pool_t;

typedef struct
{
    char *name;
    char *value;
} header_t;

typedef struct
{
    char *sub_domain;
    char *method;
    char *path;
    char *version;
    header_t *headers;
    int headers_len;
    char *body;
    char wildcards[16][64];
    int wildcard_num;
} http_req_t;

typedef struct route
{
    char *sub_domain;
    char method[16];
    char path[265];
    void (*callback)(int client_fd, http_req_t *req);
    struct route *next;
} route_t;

typedef struct
{
    char extension[16];
    char mime_type[64];
} mime_entry_t;

typedef struct
{
    int sckt;
    route_t *route;
} server_t;

void free_http_req(http_req_t *req);
server_status_t parse_http_request(const char *buffer, size_t bytes, http_req_t *http_req);
server_status_t serve_file(int client_fd, const char *path);
void handle_client(int client_fd);
void handle_sigint(int sig);

void send_json_response(int client_fd, response_status_t status, const char *json);
void send_string(int client_fd, char *str);
void send_plain(int client_fd, char *str);

void server_run(void (*load_routes)());

#endif
