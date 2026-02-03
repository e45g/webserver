#include "postgre.h"
#include "utils.h"
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PGconn *conn = NULL;

int resolve_result(PGresult *res) {
    ExecStatusType r = PQresultStatus(res);
    switch(r) {
        case PGRES_BAD_RESPONSE: return -1;
        case PGRES_NONFATAL_ERROR: return -2;
        case PGRES_FATAL_ERROR: return -3;
        case PGRES_PIPELINE_ABORTED: return -4;
        default: return 0;
    }
}

int db_init(const char *host, const char *dbname, const char *user, const char *password) {
    char details[512] = {0};
    snprintf(details, sizeof(details), "host=%s dbname=%s user=%s password=%s", host, dbname, user, password);
    conn = PQconnectdb(details);

    if (PQstatus(conn) != CONNECTION_OK) {
        LOG("POSTGRE Connection failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        conn = NULL;
        return 1;
    }

    return 0;
}

void db_close(void) {
    if(conn == NULL) return;
    PQfinish(conn);
    conn = NULL;
}

db_result_t *db_exec(const char *query) {
    PGresult *res = PQexec(conn, query);
    int r = resolve_result(res);
    if(r != 0) {
        LOG("Postgre: Error/Warning (query: %s): %s\n", query, PQerrorMessage(conn));
        if(r != -2) { // non-fatal
            PQclear(res);
            return NULL;
        }
    }
    db_result_t *result = malloc(sizeof(db_result_t));

    int rows = PQntuples(res);
    int cols = PQnfields(res);
    result->num_rows = rows;
    result->num_cols = cols;

    result->rows = malloc(sizeof(char**) * rows);
    for (int i = 0; i < rows; i++) {
        result->rows[i] = malloc(sizeof(char*) * cols);
    }

    result->col_names = malloc(sizeof(char*) * cols);

    for(int i = 0; i < cols; i++) {
        result->col_names[i] = strdup(PQfname(res, i));
    }

    for(int i = 0; i < rows; i++) {
        for(int j = 0; j < cols; j++) {
            result->rows[i][j] = strdup(PQgetvalue(res, i, j));
        }
    }

    PQclear(res);
    return result;
}

db_result_t *db_prepare(const char *query, const char **params, int param_count) {
    PGresult *res = PQexecParams(conn,
                                 query,
                                 param_count,
                                 NULL,
                                 params,
                                 NULL,
                                 NULL,
                                 0);

    int r = resolve_result(res);
    if(r != 0) {
        LOG("Postgre: Error/Warning (query: %s): %s\n", query, PQerrorMessage(conn));
        if(r != -2) { // non-fatal
            PQclear(res);
            return NULL;
        }
    }
    db_result_t *result = malloc(sizeof(db_result_t));

    int rows = PQntuples(res);
    int cols = PQnfields(res);
    result->num_rows = rows;
    result->num_cols = cols;

    result->rows = malloc(sizeof(char**) * rows);
    for (int i = 0; i < rows; i++) {
        result->rows[i] = malloc(sizeof(char*) * cols);
    }

    result->col_names = malloc(sizeof(char*) * cols);

    for(int i = 0; i < cols; i++) {
        result->col_names[i] = strdup(PQfname(res, i));
    }

    for(int i = 0; i < rows; i++) {
        for(int j = 0; j < cols; j++) {
            result->rows[i][j] = strdup(PQgetvalue(res, i, j));
        }
    }

    PQclear(res);
    return result;

}
