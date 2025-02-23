#ifndef SERVER_H
#define SERVER_H

#define BUFFER_SIZE (1024 * 1024)
#define MAX_HEADERS 64
#define MAX_EVENTS 1024

typedef enum
{
    OK_OK = 200,
    OK_CREATED = 201,
    OK_NOCONTENT = 204,
    ERR_NOTFOUND = 404,
    ERR_BADREQ = 400,
    ERR_UNPROC = 422,
    ERR_INTERR = 500,
} ResponseStatus;

typedef struct
{
    ResponseStatus status;
    const char *message;
} ResponseInfo;

typedef struct
{
    char *name;
    char *value;
} Header;

typedef struct
{
    char *method;
    char *path;
    char *version;
    Header *headers;
    int headers_len;
    char *body;
    char wildcards[16][64];
    int wildcard_num;
} HttpRequest;

typedef struct Route
{
    char method[16];
    char path[265];
    void (*callback)(int client_fd, HttpRequest *req);
    struct Route *next;
} Route;

typedef struct
{
    char extension[16];
    char mime_type[64];
} MimeEntry;

typedef struct
{
    int sckt;
    Route *route;
} Server;

void free_http_req(HttpRequest *req);
int parse_http_req(const char *buffer, HttpRequest *http_req);
int serve_file(int client_fd, const char *path);
void handle_client(int client_fd);
void handle_sigint(int sig);

void send_json_response(int client_fd, ResponseStatus status, const char *json);
void send_string(int client_fd, char *str);

void server_run(void);

#endif
