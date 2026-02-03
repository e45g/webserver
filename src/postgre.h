#ifndef POSTGRE_H
#define POSTGRE_H

typedef int (*db_callback)(void*,int,char**,char**);

#define MAX_ERROR_LEN 256
#define MAX_QUERY_LEN 4096

typedef struct {
    char ***rows;
    char **col_names;
    int num_rows;
    int num_cols;
} db_result_t;

int db_init(const char *host, const char *dbname, const char *user, const char *password);
void db_close(void);

db_result_t *db_exec(const char *query);
db_result_t *db_query(char *query, const char **params, int params_count);
void free_result(db_result_t *result);

int db_execute(const char *sql, const char **params, int param_count);
db_result_t *db_prepare(const char *query, const char **params, int param_count);
int db_exists(const char *id);

#endif
