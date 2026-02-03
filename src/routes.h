#ifndef ROUTES_H
#define ROUTES_H

#include "server.h"

int match_route(char *route, char *handle);
void get_wildcards(const http_req_t *req, const route_t *r);
void add_route(const char* sub_dom, const char *method, const char *path, void (*callback)(int client_fd, http_req_t *req));
void free_routes(void);
void print_routes(void);

#endif
