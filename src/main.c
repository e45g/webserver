#include <linux/limits.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>

#include "routes.h"
#include "server.h"
#include "utils.h"

#include "cxc/index.h"

void handle_root(int client_fd, http_req_t *req __attribute__((unused))) {
    IndexProps props = {.title = "e45g"};
    char *str = render_index(&props);

    send_string(client_fd, str);
    free(str);
}

void handle_robots(int client_fd, http_req_t *req __attribute__((unused))) {
    send_plain(client_fd, "User-agent: *\nAllow: /");
}

void handle_log(int client_fd, http_req_t *req __attribute__((unused))) {
    serve_file(client_fd, "../log.txt");
}

void load_routes() {
    add_route("GET", "/robots.txt", NULL, handle_robots);

    add_route("GET", "/", NULL, handle_root);
    add_route("GET", "/log", NULL, handle_log);
}

int main(void) {
    int result = load_env(".env");
    if(result != 0) LOG("Invalid env file.");

    // const char *db_password = get_db_password();
    // if(db_init("localhost", "db_name", "", db_password) != 0) {
    //     LOG("Failed to init postgresql");
    // }

    server_run(load_routes);

    return 0;
}
