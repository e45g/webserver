#include <errno.h>
#include <stddef.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>

#include "utils.h"
#include "server.h"


void logg(long line, const char *file, const char *func, const char *format, ...) {
    time_t raw_time;
    struct tm *time_info;
    char time_buffer[20];

    time(&raw_time);
    time_info = localtime(&raw_time);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

    FILE *log_file = fopen("log.txt", "a");
    if (!log_file) {
        perror("Unable to open log file");
        return;
    }

    fprintf(log_file, "[%s] LOG: [%s:%ld %s] ", time_buffer, file, line, func);
    printf("[%s] LOG: [%s:%ld %s] ", time_buffer, file, line, func);

    va_list args;
    va_start(args, format);

    vfprintf(log_file, format, args);

    va_end(args);
    va_start(args, format);

    vprintf(format, args);

    va_end(args);

    fprintf(log_file, " : %s", strerror(errno));
    printf(" status : %s\n", strerror(errno));
    errno = 0;

    fprintf(log_file, "\n\n\n");
    printf("\n\n\n");

    fclose(log_file);
}

int load_env(const char *path){
    FILE *f = fopen(path, "r");
    if(!f){
        LOG("Failed to open .env");
        return -1;
    }
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), f)) {
        *(strchr(line, '\n')) = '\0';

        char *key = strtok(line, "=");
        char *value = strtok(NULL, "=");

        if(key && value){
            int result = setenv(key, value, 1);
            if(result != 0) LOG("setenv failed");
        }

    }
    fclose(f);
    return 0;
}

const char *get_db_password(void){
    const char *key = getenv("DB_PASSWORD");
    return key;
}

int get_port(void){
    const char *port = getenv("PORT");
    return port ? strtol(port, NULL, 10) : 1444;
}

const char *get_routes_dir(void){
    const char *dir = getenv("ROUTES_DIR");
    return dir ? dir : "./routes";
}

const char *get_public_dir(void){
    const char *dir = getenv("PUBLIC_DIR");
    return dir ? dir : "./public";
}

/*
*   Pseudo random number generator by Terry A. Davis
*/
unsigned long get_num() {
    unsigned long i;
    /*
    __asm__ __volatile__ (
        "RDTSC\n"
        "MOV %%EAX, %%EAX\n"
        "SHL $32, %%RDX\n"
        "ADD %%RDX, %%RAX\n"
        : "=a" (i)
        :
        : "%rdx"
    );
    */
    i = 2;
    return (i * ((rand() % 230)+1) % ((rand() % 20)+1));
}

void generate_id(char *uuid) {
    const char *chars = "0123456789abcdef";
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            uuid[i] = '-';
        } else if (i == 14) {
            uuid[i] = '4';
        } else if (i == 19) {
            uuid[i] = chars[(get_num() % 4) + 8];
        } else {
            uuid[i] = chars[get_num() % 16];
        }
    }
    uuid[36] = '\0';
}


void get_current_time(char *buffer, size_t size, long offset) {
    time_t now = time(NULL) + offset;
    struct tm tm_info;
    void *result = gmtime_r(&now, &tm_info);
    if(!result) LOG("gmtime_r failed");

    (void)strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

char *get_header(http_req_t *request, const char *name) {
    for (int i = 0; i < request->headers_len; i++) {
        if (strcmp(request->headers[i].name, name) == 0) {
            return request->headers[i].value;
        }
    }
    return NULL;
}

int accepts_gzip(http_req_t *req){
    char *val = get_header(req, "Accept-Encoding");
    if(!val) return 0;
    char *s = strstr(val, "gzip");
    if(!s) return 0;
    return 1;
}

char* sanitize_path(const char* path) {
    size_t path_len = strlen(path);

    if (!path || path_len == 0) return NULL;
    if (path_len >= MAX_PATH_LENGTH) return NULL;

    char* sanitized = malloc(path_len + 1);
    if (!sanitized) return NULL;

    const char* src = path;
    char* dst = sanitized;

    while (*src) {
        if (*src == '.') {
            if (src[1] == '.' && (src[2] == '/' || src[2] == '\0')) {
                free(sanitized);
                return NULL;
            }
            if (src[1] == '/' || src[1] == '\0') {
                src += (src[1] == '/') ? 2 : 1;
                continue;
            }
        }

        if (isalnum(*src) || *src == '-' || *src == '_' || *src == '.' || *src == '/') {
            *dst++ = *src;
        } else {
            free(sanitized);
            return NULL;
        }
        src++;
    }

    *dst = '\0';

    // while (dst > sanitized && *(dst-1) == '/') {
    //     *(--dst) = '\0';
    // }

    return sanitized;
}

int validate_http_method(const char* method) {
    if (!method) return 0;

    size_t len = strlen(method);
    if (len == 0 || len > MAX_METHOD_LENGTH) return 0;

    const char* allowed_methods[] = {"GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS"};
    const int num_methods = sizeof(allowed_methods) / sizeof(allowed_methods[0]);

    for (int i = 0; i < num_methods; i++) {
        if (strcmp(method, allowed_methods[i]) == 0) return 1;
    }
    return 0;
}

int validate_header(const char* name, const char* value) {
    if (!name || !value) return 0;

    size_t name_len = strlen(name);
    size_t value_len = strlen(value);

    if (name_len == 0 || name_len > MAX_HEADER_LENGTH ||
        value_len > MAX_HEADER_LENGTH) return 0;

    // Validate header name (RFC 7230)
    for (size_t i = 0; i < name_len; i++) {
        char c = name[i];
        if (!isalnum(c) && c != '-' && c != '_') return 0;
    }

    // Check for dangerous headers
    const char* forbidden_headers[] = {"content-length", "transfer-encoding"};
    const int num_forbidden = sizeof(forbidden_headers) / sizeof(forbidden_headers[0]);

    for (int i = 0; i < num_forbidden; i++) {
        if (strcasecmp(name, forbidden_headers[i]) == 0) return 0;
    }

    return 1;
}
