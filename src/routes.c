#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"
#include "utils.h"

extern server_t server;

int match_route(const char *route, const char *handle) {
    const char *r = route;
    const char *h = handle;
    while (*r && *h) {
        if (*h == '*') {
            r = strchr(r, '/');
            if (!r) {
                return 1;
            }
            h++;
            continue;
        }
        else if (*h != *r) {
            return 0;
        }
        h++;
        r++;
    }

    return (*r == '\0' && (*h == '\0' || *h == '*'));
}

void get_wildcards(http_req_t *req, const route_t *r){
    const char *req_path = req->path;
    const char *route_path = r->path;

    int req_len = strlen(req_path);
    int route_len = strlen(route_path);

    int i = 0;
    int j = 0;

    req->wildcard_num = 0;

    while(i < req_len && j < route_len){
        if(route_path[j] == '*'){
            j++;
            if(req->wildcard_num < 16){
                int k = 0;
                while(i < req_len && req_path[i] != '/' && k < 63){
                    req->wildcards[req->wildcard_num][k++] = req_path[i++];
                }
                req->wildcards[req->wildcard_num][k] = '\0';
                req->wildcard_num++;
            }
        }
        i++;
        j++;
    }
}

void add_route(const char *method, const char *path, const char* sub_dom, void (*callback)(int client_fd, http_req_t *req)) {
    route_t *r = malloc(sizeof(route_t));
    if (r == NULL) {
        LOG("Failed to allocate memory");
        return;
    }
    r->sub_domain = NULL;
    if(sub_dom != NULL) {
        r->sub_domain = strdup(sub_dom);
    }
    strncpy(r->method, method, sizeof(r->method) - 1);
    strncpy(r->path, path, sizeof(r->path) - 1);
    r->callback = callback;
    r->next = server.route;
    server.route = r;
}

void print_routes(void) {
    for (route_t *r = server.route; r; r = r->next)
    {
        LOG("Route - %s: %s", r->method, r->path);
    }
}

void free_routes(void) {
    route_t *current = server.route;
    while (current)
    {
        route_t *tmp = current->next;
        free(current->sub_domain);
        free(current);
        current = tmp;
    }
}
