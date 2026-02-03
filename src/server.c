#include <errno.h>
#include <linux/limits.h>

#include <signal.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "server.h"
#include "postgre.h"
#include "routes.h"
#include "utils.h"

static buffer_pool_t buffer_pool = {0};
static connection_pool_t conn_pool = {0};
server_t server;

mime_entry_t mime_types[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".gif", "image/gif"},
    {".txt", "text/plain"},
    {".json", "application/json"},
    {".svg", "image/svg+xml"},
    {".pdf", "application/pdf"},
    {".txt", "text/plain"}
};

void handle_critical_error(char *msg, int sckt) {
    LOG(msg);
    if (sckt > 0) close(sckt);
    db_close();
    exit(EXIT_FAILURE);
}

response_info_t get_response_info(response_status_t status) {
    switch (status) {
        case OK_OK:
            return (response_info_t){200, "OK"};
        case OK_CREATED:
            return (response_info_t){201, "Created"};
        case OK_NOCONTENT:
            return (response_info_t){204, "No Content"};
        case ERR_NOTFOUND:
            return (response_info_t){404, "Not Found"};
        case ERR_BADREQ:
            return (response_info_t){400, "Bad Request"};
        case ERR_UNPROC:
            return (response_info_t){422, "Unprocessable Content"};
        case ERR_INTERR:
            return (response_info_t){500, "Internal Server Error"};
        default:
            return (response_info_t){500, "Unknown Error"};
    }
}

int set_non_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}


void send_error_response(int client_fd, response_status_t status) {
    response_info_t info = get_response_info(status);

    char body[512];
    snprintf(body, sizeof(body), "<html><body><h1>%d %s</h1></body></html>", info.status, info.message);

    char response[1024];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             info.status, info.message, strlen(body), body);

    ssize_t sent = 0;
    ssize_t total = strlen(response);
    while (sent < total) {
        ssize_t s = send(client_fd, response + sent, total - sent, MSG_NOSIGNAL);
        if (s < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        sent += s;
    }
}



void send_string(int client_fd, char *str) {
    if (!str) {
        send_error_response(client_fd, ERR_INTERR);
        return;
    }

    size_t str_len = strlen(str);
    size_t response_size = str_len + 512;
    char *response = malloc(response_size);
    if (!response) {
        send_error_response(client_fd, ERR_INTERR);
        return;
    }

    int response_len = snprintf(response, response_size,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html; charset=utf-8\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             str_len, str);

    ssize_t total_sent = 0;
    while (total_sent < response_len) {
        ssize_t sent = send(client_fd, response + total_sent, response_len - total_sent, MSG_NOSIGNAL);

        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }

        if (sent == 0) {
            break;
        }

        total_sent += sent;
    }

    free(response);
}



void send_plain(int client_fd, char *str) {
    if (!str) {
        send_error_response(client_fd, ERR_INTERR);
        return;
    }

    size_t str_len = strlen(str);
    size_t response_size = str_len + 512;
    char *response = malloc(response_size);
    if (!response) {
        send_error_response(client_fd, ERR_INTERR);
        return;
    }

    int response_len = snprintf(response, response_size,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             str_len, str);

    ssize_t total_sent = 0;
    while (total_sent < response_len) {
        ssize_t sent = send(client_fd, response + total_sent, response_len - total_sent, MSG_NOSIGNAL);

        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }

        if (sent == 0) {
            break;
        }

        total_sent += sent;
    }

    free(response);
}

void send_json_response(int client_fd, response_status_t status, const char *json) {
    response_info_t info = get_response_info(status);
    ssize_t json_len = strlen(json);

    char headers[1024] = {0};
    int headers_len = snprintf(headers, sizeof(headers),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: keep-alive\r\n",
             info.status, info.message, json_len);

    headers_len += snprintf(headers+headers_len, sizeof(headers), "\r\n");

    ssize_t total_sent = 0;
    while (total_sent < headers_len) {
        ssize_t sent = send(client_fd, headers + total_sent, headers_len - total_sent, 0);
        if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                LOG("send_json_response headers sent = -1");
                send_error_response(client_fd, ERR_INTERR);
                return;
            }
        }
        total_sent += sent;
    }

    ssize_t offset = 0;
    const ssize_t CHUNK_SIZE = 16384;
    while (offset < json_len) {
        ssize_t remaining = json_len  - offset;
        ssize_t chunk_size = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        ssize_t sent_chunk = 0;
        while (sent_chunk < chunk_size) {
            ssize_t sent = send(client_fd, json + offset + sent_chunk, chunk_size - sent_chunk, 0);
            if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                } else {
                    LOG("send_json_response JSON sent = -1");
                    send_error_response(client_fd, ERR_INTERR);
                    return;
                }
            }
            sent_chunk += sent;
        }
        offset += sent_chunk;
    }

    if (offset != json_len) {
        send_error_response(client_fd, ERR_INTERR);
        return;
    }
}

client_con_t* get_connection(int fd) {
    client_con_t* conn;

    if (conn_pool.free_connections) {
        conn = conn_pool.free_connections;
        conn_pool.free_connections = conn->next;
    } else if (conn_pool.total_count < CONNECTION_POOL_SIZE) {
        conn = malloc(sizeof(client_con_t));
        if (!conn) return NULL;
        conn_pool.total_count++;
    } else {
        return NULL;
    }

    conn->fd = fd;
    conn->last_activity = time(NULL);
    conn->keepalive_requests = 0;
    conn->next = conn_pool.active_connections;
    conn_pool.active_connections = conn;
    conn_pool.active_count++;

    return conn;
}

void release_connection(client_con_t* conn) {
    client_con_t** current = &conn_pool.active_connections;
    while (*current && *current != conn) {
        current = &(*current)->next;
    }
    if (*current) {
        *current = conn->next;
        conn_pool.active_count--;
    }

    conn->next = conn_pool.free_connections;
    conn_pool.free_connections = conn;
}


buffer_t* get_buffer(size_t min_size) {
    buffer_t* buf;

    if (buffer_pool.free_buffers && buffer_pool.free_buffers->size >= min_size) {
        buf = buffer_pool.free_buffers;
        buffer_pool.free_buffers = buf->next;
        buffer_pool.count--;
    } else {
        buf = malloc(sizeof(buffer_t));
        if (!buf) return NULL;

        buf->size = (min_size < BUFFER_SIZE) ? BUFFER_SIZE : min_size;
        buf->data = malloc(buf->size);
        if (!buf->data) {
            free(buf);
            return NULL;
        }
    }

    buf->next = NULL;
    return buf;
}

void release_buffer(buffer_t* buf) {
    if (!buf) return;

    if (buffer_pool.count < BUFFER_POOL_SIZE) {
        buf->next = buffer_pool.free_buffers;
        buffer_pool.free_buffers = buf;
        buffer_pool.count++;
    } else {
        free(buf->data);
        free(buf);
    }
}

const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    for (int i = 0; i < (int) (sizeof(mime_types) / sizeof(mime_types[0])); i++) {
        if (strcmp(mime_types[i].extension, ext) == 0) {
            return mime_types[i].mime_type;
        }
    }

    return "application/octet-stream";
}


server_status_t parse_http_request(const char* buffer, size_t buffer_len, http_req_t* http_req) {
    if (!buffer || !http_req || buffer_len > MAX_REQUEST_SIZE) {
        return SERVER_ERR_PROTOCOL;
    }

    memset(http_req, 0, sizeof(http_req_t));

    const char* line_end = strstr(buffer, "\r\n");
    if (!line_end) return SERVER_ERR_PROTOCOL;

    size_t request_line_len = line_end - buffer;
    if (request_line_len > MAX_URI_LENGTH) return SERVER_ERR_PROTOCOL;

    char request_line[MAX_URI_LENGTH + 1];
    memcpy(request_line, buffer, request_line_len);
    request_line[request_line_len] = '\0';

    char* method_end = strchr(request_line, ' ');
    if (!method_end) return SERVER_ERR_PROTOCOL;

    *method_end = '\0';
    if (!validate_http_method(request_line)) return SERVER_ERR_PROTOCOL;

    http_req->method = strndup(request_line, MAX_METHOD_LENGTH);
    if (!http_req->method) return SERVER_ERR_MEMORY;

    char* path_start = method_end + 1;
    char* path_end = strchr(path_start, ' ');
    if (!path_end) {
        free(http_req->method);
        return SERVER_ERR_PROTOCOL;
    }

    *path_end = '\0';
    char* sanitized_path = sanitize_path(path_start);
    if (!sanitized_path) {
        free(http_req->method);
        return SERVER_ERR_SECURITY;
    }
    http_req->path = sanitized_path;

    char* version_start = path_end + 1;
    if (strncmp(version_start, "HTTP/1.", 7) != 0) {
        free(http_req->method);
        free(http_req->path);
        return SERVER_ERR_PROTOCOL;
    }

    http_req->version = strndup(version_start, 16);
    if (!http_req->version) {
        free(http_req->method);
        free(http_req->path);
        return SERVER_ERR_MEMORY;
    }

    http_req->headers = calloc(MAX_HEADER_COUNT, sizeof(header_t));
    if (!http_req->headers) {
        free(http_req->method);
        free(http_req->path);
        free(http_req->version);
        return SERVER_ERR_MEMORY;
    }

    const char* header_line = line_end + 2;
    http_req->headers_len = 0;

    while (*header_line && http_req->headers_len < MAX_HEADER_COUNT) {
        const char* header_end = strstr(header_line, "\r\n");
        if (!header_end) break;

        if (header_line == header_end) {
            http_req->body = strdup(header_end + 2);
            break;
        }

        size_t header_len = header_end - header_line;
        if (header_len > MAX_HEADER_LENGTH) {
            header_line = header_end + 2;
            continue;
        }

        const char* colon = memchr(header_line, ':', header_len);
        if (!colon) {
            header_line = header_end + 2;
            continue;
        }

        size_t name_len = colon - header_line;
        const char* value_start = colon + 1;

        while (value_start < header_end && *value_start == ' ') value_start++;
        size_t value_len = header_end - value_start;

        char name[MAX_HEADER_LENGTH + 1];
        char value[MAX_HEADER_LENGTH + 1];

        memcpy(name, header_line, name_len);
        name[name_len] = '\0';
        memcpy(value, value_start, value_len);
        value[value_len] = '\0';

        if (validate_header(name, value)) {
            http_req->headers[http_req->headers_len].name = strdup(name);
            http_req->headers[http_req->headers_len].value = strdup(value);

            if (http_req->headers[http_req->headers_len].name &&
                http_req->headers[http_req->headers_len].value) {
                http_req->headers_len++;
            }
        }

        header_line = header_end + 2;
    }

    return SERVER_OK;
}

void http_req_free(http_req_t *req) {
    if(req->sub_domain != NULL) free(req->sub_domain);
    free(req->method);
    free(req->path);
    free(req->version);

    for (int i = 0; i < req->headers_len; i++) {
        free(req->headers[i].name);
        free(req->headers[i].value);
    }
    free(req->headers);
    free(req->body);
}

server_status_t serve_file(int client_fd, const char* path) {
    char full_path[PATH_MAX];
    struct stat st;
    int file_fd = -1;
    server_status_t result = SERVER_OK;

    snprintf(full_path, sizeof(full_path), "%s/%s", get_routes_dir(), path);
    file_fd = open(full_path, O_RDONLY);

    if (file_fd == -1) {
        snprintf(full_path, sizeof(full_path), "%s/%s", get_public_dir(), path);
        file_fd = open(full_path, O_RDONLY);

        if (file_fd == -1) {
            return SERVER_ERR_FILE;
        }
    }

    if (fstat(file_fd, &st) == -1) {
        close(file_fd);
        return SERVER_ERR_FILE;
    }

    if (!S_ISREG(st.st_mode)) {
        close(file_fd);
        return SERVER_ERR_FILE;
    }

    const char* mime_type = get_mime_type(path);

    buffer_t* header_buf = get_buffer(1024);
    if (!header_buf) {
        close(file_fd);
        return SERVER_ERR_MEMORY;
    }

    int header_len = snprintf(header_buf->data, header_buf->size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: keep-alive\r\n"
        "Server: hehe/1.0\r\n"
        "\r\n",
        mime_type, st.st_size);

    ssize_t sent = send(client_fd, header_buf->data, header_len, MSG_NOSIGNAL);
    release_buffer(header_buf);

    if (sent != header_len) {
        close(file_fd);
        return SERVER_ERR_NETWORK;
    }

    off_t offset = 0;
    while (offset < st.st_size) {
        size_t chunk_size = (st.st_size - offset > SENDFILE_CHUNK_SIZE) ?
                           SENDFILE_CHUNK_SIZE : (st.st_size - offset);

        ssize_t sent_bytes = sendfile(client_fd, file_fd, &offset, chunk_size);
        if (sent_bytes <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            result = SERVER_ERR_NETWORK;
            break;
        }
    }

    close(file_fd);
    return result;
}

static void extract_subdomain(http_req_t *req) {
    char *host = get_header(req, "Host");
    if (!host) {
        req->sub_domain = NULL;
        return;
    }

    char *sub = strdup(host);
    if (!sub) {
        req->sub_domain = NULL;
        return;
    }

    char *port = strchr(sub, ':');
    if (port) *port = '\0';

    char *dot1 = strrchr(sub, '.');
    if (!dot1) {
        free(sub);
        req->sub_domain = NULL;
        return;
    }

    *dot1 = '\0';
    char *dot2 = strchr(sub, '.');
    if (dot2) {
        *dot2 = '\0';
        req->sub_domain = strdup(sub);
    } else {
        req->sub_domain = NULL;
    }

    free(sub);
}

static int process_routes(int client_fd, http_req_t *req) {
    for (route_t* r = server.route; r; r = r->next) {
        if (strcmp(req->method, r->method) != 0) continue;
        if (!match_route(req->path, r->path)) continue;

        int subdomain_match = 0;
        if (r->sub_domain == NULL && req->sub_domain == NULL) {
            subdomain_match = 1;
        } else if (r->sub_domain != NULL && req->sub_domain != NULL &&
                   strcmp(r->sub_domain, req->sub_domain) == 0) {
            subdomain_match = 1;
        }

        if (!subdomain_match) continue;

        get_wildcards(req, r);
        r->callback(client_fd, req);
        return 1;
    }

    return 0;
}

static void handle_static_file(int client_fd, http_req_t *req) {
    if (strcmp(req->method, "GET") != 0) {
        send_error_response(client_fd, ERR_NOTFOUND);
        return;
    }

    server_status_t file_result = serve_file(client_fd, req->path);
    if (file_result != SERVER_OK) {
        send_error_response(client_fd, ERR_NOTFOUND);
    }
}

static server_status_t read_full_body(int client_fd, http_req_t *req, buffer_t *buf) {
    char *content_len_header = get_header(req, "Content-Length");
    if (!content_len_header) {
        return SERVER_OK;
    }

    int content_length = atoi(content_len_header);
    if (content_length <= 0 || content_length > MAX_REQUEST_SIZE) {
        return SERVER_ERR_PROTOCOL;
    }

    const char *body_start = strstr(buf->data, "\r\n\r\n");
    if (!body_start) {
        return SERVER_ERR_PROTOCOL;
    }
    body_start += 4;

    int body_received = buf->data + strlen(buf->data) - body_start;

    while (body_received < content_length) {
        ssize_t more = recv(client_fd, buf->data, buf->size - 1, 0);
        if (more <= 0) {
            return SERVER_ERR_NETWORK;
        }

        buf->data[more] = '\0';

        char *temp = malloc(strlen(req->body) + more + 1);
        if (!temp) {
            return SERVER_ERR_MEMORY;
        }

        strcpy(temp, req->body);
        strcat(temp, buf->data);
        free(req->body);
        req->body = temp;

        body_received += more;
    }

    return SERVER_OK;
}

static void cleanup_request(http_req_t *req, buffer_t *buf, client_con_t *conn, int client_fd, int should_close) {
    if (req) http_req_free(req);
    if (buf) release_buffer(buf);
    if (conn) release_connection(conn);
    if (should_close && client_fd >= 0) close(client_fd);
}

void handle_client(int client_fd) {
    client_con_t* conn = NULL;
    buffer_t* request_buf = NULL;
    http_req_t req = {0};
    server_status_t status = SERVER_OK;

    conn = get_connection(client_fd);
    if (!conn) {
        close(client_fd);
        return;
    }

    request_buf = get_buffer(MAX_REQUEST_SIZE);
    if (!request_buf) {
        cleanup_request(&req, NULL, conn, client_fd, 1);
        return;
    }

    ssize_t bytes_received = recv(client_fd, request_buf->data, request_buf->size - 1, 0);

    if (bytes_received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            release_buffer(request_buf);
            release_connection(conn);
            return;
        }
        release_buffer(request_buf);
        release_connection(conn);
        close(client_fd);
        return;
    }

    if (bytes_received == 0) {
        release_buffer(request_buf);
        release_connection(conn);
        close(client_fd);
        return;
    }

    request_buf->data[bytes_received] = '\0';

    status = parse_http_request(request_buf->data, bytes_received, &req);
    if (status != SERVER_OK) {
        send_error_response(client_fd, ERR_BADREQ);
        cleanup_request(&req, request_buf, conn, client_fd, 1);
        return;
    }

    LOG("request: %s %s", req.method, req.path);

    status = read_full_body(client_fd, &req, request_buf);
    if (status != SERVER_OK) {
        send_error_response(client_fd, ERR_BADREQ);
        cleanup_request(&req, request_buf, conn, client_fd, 1);
        return;
    }

    release_buffer(request_buf);
    request_buf = NULL;

    extract_subdomain(&req);

    int route_handled = process_routes(client_fd, &req);

    if (!route_handled) {
        handle_static_file(client_fd, &req);
    }

    conn->keepalive_requests++;
    conn->last_activity = time(NULL);

    http_req_free(&req);
    release_connection(conn);
    close(client_fd);
}

void shutdown_pools(void) {
    buffer_t *curr_buf = buffer_pool.free_buffers;
    while (curr_buf) {
        buffer_t *next = curr_buf->next;
        free(curr_buf->data);
        free(curr_buf);
        curr_buf = next;
    }

    client_con_t *curr_conn = conn_pool.free_connections;
    while (curr_conn) {
        client_con_t *next = curr_conn->next;
        free(curr_conn);
        curr_conn = next;
    }

    curr_conn = conn_pool.active_connections;
    while (curr_conn) {
        client_con_t *next = curr_conn->next;
        close(curr_conn->fd);
        free(curr_conn);
        curr_conn = next;
    }
}

void handle_sigint(int sig) {
    LOG("Shutting down server... (%d)", sig);
    if (server.sckt) {
        close(server.sckt);
    }
    db_close();
    free_routes();
    shutdown_pools();
    exit(0);
}

void cleanup_expired_connections() {
    time_t now = time(NULL);
    client_con_t** current = &conn_pool.active_connections;

    while (*current) {
        client_con_t* conn = *current;
        if (now - conn->last_activity > KEEPALIVE_TIMEOUT) {
            *current = conn->next;
            close(conn->fd);
            conn->next = conn_pool.free_connections;
            conn_pool.free_connections = conn;
            conn_pool.active_count--;
        } else {
            current = &conn->next;
        }
    }
}

void server_run(void (*load_routes)()) {
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    // if(remove("log.txt") != 0){
    //     perror("Error deleting log.txt");
    // }

    int result = 0;
    result = setvbuf(stdout, NULL, _IONBF, 0);
    if(result != 0) handle_critical_error("setvbuf failed", 0);

    // Example of sqlite database usage
    // result = db_init("games.db");
    // if(result != 0) handle_critical_error("db_init failed", 0);
    //
    // db_exec("CREATE TABLE IF NOT EXISTS games (id TEXT PRIMARY KEY NOT NULL, created_at DATE NOT NULL, updated_at DATE, name TEXT NOT NULL, difficulty TEXT NOT NULL, game_state TEXT NOT NULL, board TEXT NOT NULL);", NULL, 0, NULL);
    // db_execute("DELETE FROM games;", NULL, 0);

    time_t last_cleanup = time(NULL);

    const int PORT = get_port();
    struct epoll_event ev, events[MAX_EVENTS];

    int sckt = socket(AF_INET, SOCK_STREAM, 0);
    if (sckt < 0) handle_critical_error("Socket creation failed.", -1);
    server.sckt = sckt;
    server.route = NULL;

    int opt = 1;
    setsockopt(sckt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sckt, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    (void) set_non_blocking(sckt);

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr = {INADDR_ANY},
        .sin_zero = {0},
    };

    result = bind(sckt, (struct sockaddr *)&addr, sizeof(addr));
    if (result != 0) handle_critical_error("Bind failed.", server.sckt);

    result = listen(sckt, SOMAXCONN);
    if (result != 0) handle_critical_error("Listen failed.", server.sckt);

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(epoll_fd == -1){
        close(sckt);
        handle_critical_error("epoll_create1 failed.", epoll_fd);
    }

    ev.events = EPOLLIN;
    ev.data.fd = sckt;
    result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sckt, &ev);
    if(result < 0) handle_critical_error("epoll ctl failed.", sckt);

    LOG("Server running on http://0.0.0.0:%d", PORT);
    (*load_routes)();
    print_routes();

    while (1) {
        time_t now = time(NULL);
        if (now - last_cleanup > 60) {
            cleanup_expired_connections();
            last_cleanup = now;
        }

        int num_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if(num_fds < 0) {
            if (errno == EINTR) continue;
            handle_critical_error("epoll wait failed", sckt);
        }

        for(int i = 0; i < num_fds; i++){
            if(events[i].data.fd == sckt){
                int client_fd = accept(server.sckt, NULL, NULL);
                if(client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    LOG("Accept failed.");
                    continue;
                }

                if (set_non_blocking(client_fd) < 0) {
                    close(client_fd);
                    continue;
                }

                ev.events = EPOLLIN; //| EPOLLET;
                ev.data.fd = client_fd;
                result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                if(result < 0) {
                    LOG("epoll_ctl failed.");
                    close(client_fd);
                }
            }
            else{
                int client_fd = events[i].data.fd;
                handle_client(client_fd);
            }
        }
    }

    db_close();
    close(sckt);
    close(epoll_fd);
}
