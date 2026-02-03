#ifndef JSON_H
#define JSON_H

#include <stddef.h>

/*
*   TODO:
*   FIX double free problem when adding same Json* to object or array
*
*/

enum json_type_e {
    JSON_NULL,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
};

typedef struct json {
    enum json_type_e type;
    union {
        double number;
        char *string;

        struct {
            struct json **elements;
            size_t size;
            size_t capacity;
        } array;

        struct {
            char *key;
            struct json *value;
            struct json *next;
        } object;

    } value;
} json_t;

json_t *json_create_object();
json_t *json_create_array(size_t initial_capacity);
json_t *json_create_string(const char *string);
json_t *json_create_number(double number);
json_t *json_create_null();
json_t *json_create_true();
json_t *json_create_false();

json_t *json_parse(const char *json_str);
char *json_print(json_t *json);

int json_array_add(json_t *array, json_t *value);
// int json_array_remove(Json *array, size_t index); // TODO
json_t *json_array_get(json_t *array, size_t index);

int json_object_add(json_t *object, const char *key, json_t *value);
int json_object_add_string(json_t *json, const char *key, const char *value);
// int json_object_remove(Json *object, const char *key); // TODO
json_t *json_object_get(json_t *object, const char *key);
char *json_object_get_string(json_t *object, const char *key);
double *json_object_get_number(json_t *object, const char *key);
json_t *json_object_get_array(json_t *object, const char *key);


int json_is_string(json_t *json);
int json_is_array(json_t *json);
int json_is_object(json_t *json);

void json_free(json_t *json);

#endif
