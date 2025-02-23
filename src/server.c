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
#include "db.h"
#include "routes.h"
#include "utils.h"

Server server;
MimeEntry mime_types[] = {
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

ResponseInfo get_response_info(ResponseStatus status) {
    switch (status) {
        case OK_OK:
            return (ResponseInfo){200, "OK"};
        case OK_CREATED:
            return (ResponseInfo){201, "Created"};
        case OK_NOCONTENT:
            return (ResponseInfo){204, "No Content"};
        case ERR_NOTFOUND:
            return (ResponseInfo){404, "Not Found"};
        case ERR_BADREQ:
            return (ResponseInfo){400, "Bad Request"};
        case ERR_UNPROC:
            return (ResponseInfo){422, "Unprocessable Content"};
        case ERR_INTERR:
            return (ResponseInfo){500, "Internal Server Error"};
        default:
            return (ResponseInfo){500, "Unknown Error"};
    }
}

int set_non_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void send_error_response(int client_fd, ResponseStatus status) {
    ResponseInfo info = get_response_info(status);

    char body[512];
    snprintf(body, sizeof(body), "<html><body><h1>%d %s</h1></body></html>", info.status, info.message);

    char response[1024];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             info.status, info.message, strlen(body), body);

    int result = send(client_fd, response, strlen(response), 0);
    if(result == -1){
        LOG("send failed.");
        send_error_response(client_fd, ERR_INTERR);
    }

}

void send_string(int client_fd, char *str) {
    size_t response_len = strlen(str) + 256;
    char *response = malloc(response_len);
    if(!response){
        LOG("malloc failed");
        send_error_response(client_fd, ERR_INTERR);
        return;
    }

    snprintf(response, response_len,
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             200, "OK", response_len, str);

    int result = send(client_fd, response, strlen(response), 0);
    if(result == -1){
        LOG("send failed.");
        send_error_response(client_fd, ERR_INTERR);
    }
}

void send_json_response(int client_fd, ResponseStatus status, const char *json) {
    ResponseInfo info = get_response_info(status);
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


int parse_http_req(const char *buffer, HttpRequest *http_req) {
    char *buf = strdup(buffer);
    if (!buf) {
        LOG("Memory allocation failed.");
        return ERR_INTERR;
    }

    char *line_end = strstr(buf, "\r\n");
    if (!line_end) {
        free(buf);
        return ERR_BADREQ;
    }

    *line_end = '\0';

    char *method_end = strchr(buf, ' ');
    if (!method_end) {
        free(buf);
        return ERR_BADREQ;
    }

    *method_end = '\0';
    http_req->method = strdup(buf);
    if (!http_req->method) {
        LOG("Mem allocation failed.");
        free(buf);
        return ERR_INTERR;
    }

    char *path_start = method_end + 1;
    char *path_end = strchr(path_start, ' ');
    if (!path_end) {
        free(buf);
        return ERR_BADREQ;
    }

    *path_end = '\0';
    http_req->path = strdup(path_start);
    if (!http_req->path) {
        LOG("Mem allocation failed.");
        free(buf);
        return ERR_INTERR;
    }

    char *version_start = path_end + 1;
    http_req->version = strdup(version_start);
    if (!http_req->version) {
        LOG("Memory allocation failed.");
        free(buf);
        return ERR_INTERR;
    }

    http_req->headers_len = 0;
    http_req->headers = calloc(MAX_HEADERS, sizeof(Header));

    if (!http_req->headers) {
        LOG("Memory allocation failed. (headers)");
        free(buf);
        return ERR_INTERR;
    }

    char *header_line = line_end + 2;
    char *body_start = NULL;

    while (*header_line && http_req->headers_len < MAX_HEADERS) {
        char *header_end = strstr(header_line, "\r\n");
        if (!header_end) break;

        *header_end = '\0';

        if (header_line == header_end) {
            body_start = header_end + 2;
            break;
        }

        char *colon = strchr(header_line, ':');
        if (!colon) {
            header_line += 2;
            continue;
        }

        *colon = '\0';
        char *header_name = header_line;
        char *header_value = colon + 1;

        while (*header_value == ' ') header_value++;

        http_req->headers[http_req->headers_len].name = strdup(header_name);
        http_req->headers[http_req->headers_len].value = strdup(header_value);

        if (!http_req->headers[http_req->headers_len].name || !http_req->headers[http_req->headers_len].value) {
            LOG("Memory allocation failed.");
            header_end += 2;
            continue;
        }

        http_req->headers_len++;
        header_line = header_end + 2;
    }

    if (body_start) {
        http_req->body = strdup(body_start);
        if (!http_req->body) {
            LOG("Memory allocation failed. (body)");
            free(buf);
            return -1;
        }
    }

    free(buf);
    return 0;
}

void http_req_free(HttpRequest *req) {
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

int serve_file(int client_fd, const char *path) {
    int result = 0;
    char p[PATH_MAX];
    LOG("\n%s", path);

    snprintf(p, PATH_MAX, "%s/%s", get_routes_dir(), path);
    int file_fd = open(p, O_RDONLY);

    if (file_fd == -1) {
        snprintf(p, PATH_MAX, "%s/%s", get_public_dir(), path);
        file_fd = open(p, O_RDONLY);

        if (file_fd == -1) {
            send_error_response(client_fd, ERR_NOTFOUND);
            return -1;
        }
    }

    struct stat st;
    result = fstat(file_fd, &st);
    if (result == -1) {
        close(file_fd);
        send_error_response(client_fd, ERR_INTERR);
        return -1;
    }

    char buffer[BUFFER_SIZE];
    const char *mime_type = get_mime_type(path);
    snprintf(buffer, BUFFER_SIZE, "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: %s\r\n"
                                  "Content-Length: %ld\r\n"
                                  "Connection: close\r\n\r\n",
             mime_type, st.st_size);

    result = send(client_fd, buffer, strlen(buffer), 0);
    if(result == -1){
        LOG("Send failed\n%s", buffer);
    }


    ssize_t offset = 0;
    result = sendfile(client_fd, file_fd, &offset, st.st_size);
    if(result == -1){
        LOG("sendfile failed");
    }

    close(file_fd);
    return 0;
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

    if (bytes_received < 0) {
        LOG("RECV failed.");
        return;
    }
    buffer[bytes_received] = '\0';
    LOG(buffer); // FLAG;

    HttpRequest req = {0};
    int result = parse_http_req(buffer, &req);
    if (result != 0){
        http_req_free(&req);
        send_error_response(client_fd, result);
        return;
    }

    for (Route *r = server.route; r; r = r->next) {
        if (strcmp(req.method, r->method) == 0 && match_route(req.path, r->path)) {
            get_wildcards(&req, r);
            r->callback(client_fd, &req);
            http_req_free(&req);
            return;
        }
    }

    if (strcmp(req.method, "GET") == 0) {
        serve_file(client_fd, req.path + 1);
    }

    send_error_response(client_fd, ERR_NOTFOUND);
    http_req_free(&req);
}

void handle_sigint(int sig) {
    LOG("Shutting down server... (%d)", sig);
    if (server.sckt) {
        close(server.sckt);
    }
    db_close();
    free_routes(server.route);
    exit(0);
}

void server_run(void) {
    if(remove("log.txt") != 0){
        perror("Error deleting log.txt");
    }

    int result = 0;
    result = setvbuf(stdout, NULL, _IONBF, 0);
    if(result != 0) handle_critical_error("setvbuf failed", 0);

    signal(SIGINT, handle_sigint);

    result = load_env(".env");
    if(result != 0) LOG("Invalid env file.");

    // Example of database usage
    // result = db_init("games.db");
    // if(result != 0) handle_critical_error("db_init failed", 0);
    //
    // db_exec("CREATE TABLE IF NOT EXISTS games (id TEXT PRIMARY KEY NOT NULL, created_at DATE NOT NULL, updated_at DATE, name TEXT NOT NULL, difficulty TEXT NOT NULL, game_state TEXT NOT NULL, board TEXT NOT NULL);", NULL, 0, NULL);
    // db_execute("DELETE FROM games;", NULL, 0);

    const int PORT = get_port();
    struct epoll_event ev, events[MAX_EVENTS];

    int sckt = socket(AF_INET, SOCK_STREAM, 0);
    if (sckt < 0) handle_critical_error("Socket creation failed.", -1);
    server.sckt = sckt;
    server.route = NULL;

    (void) set_non_blocking(sckt);

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr = {INADDR_ANY},
        .sin_zero = {0},
    };

    int opt = 1;
    result = setsockopt(sckt, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(result < 0) handle_critical_error("setsockopt failed.", server.sckt);

    result = bind(sckt, (struct sockaddr *)&addr, sizeof(addr));
    if (result != 0) handle_critical_error("Bind failed.", server.sckt);

    result = listen(sckt, SOMAXCONN);
    if (result != 0) handle_critical_error("Listen failed.", server.sckt);

    int epoll_fd;
    epoll_fd = epoll_create1(0);
    if(epoll_fd == -1){
        close(sckt);
        handle_critical_error("epoll_create1 failed.", epoll_fd);
    }

    ev.events = EPOLLIN;
    ev.data.fd = sckt;
    result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sckt, &ev);
    if(result < 0) handle_critical_error("epoll ctl failed.", sckt);

    LOG("Server running on http://0.0.0.0:%d", PORT);

    load_routes();
    LOG("Routes loaded.");
    print_routes();

    while (1) {
        int num_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(num_fds < 0) handle_critical_error("epoll wait failed", sckt);
        for(int i = 0; i < num_fds; i++){
            if(events[i].data.fd == sckt){
                int client_fd = accept(server.sckt, NULL, NULL);
                if(client_fd < 0) {
                    LOG("Accept failed.");
                    continue;
                }

                (void) set_non_blocking(client_fd);

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                if(result < 0) {
                    LOG("epoll_ctl failed.");
                    close(client_fd);
                }
            }
            else{
                int client_fd = events[i].data.fd;
                if(client_fd < 0) continue;

                (void) handle_client(client_fd);

                result = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                if(result < 0) LOG("epoll_ctl EPOLL_CTL_DEL failed");
                close(client_fd);
            }
        }
    }

    db_close();
    close(sckt);
    close(epoll_fd);
}
