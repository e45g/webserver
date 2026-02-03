#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils.h"
#include "json_utils.h"
#include "json.h"

int json_is_array(json_t *json){
    if(!json || json->type != JSON_ARRAY) return 0;
    return 1;
}

int json_is_object(json_t *json){
    if(!json || json->type != JSON_OBJECT) return 0;
    return 1;
}

int json_is_string(json_t *json){
    if(!json || json->type != JSON_STRING) return 0;
    return 1;
}

json_t *json_create_null(){
    json_t *json = calloc(1, sizeof(json_t));
    if(!json) {
        LOG("Malloc failed.");
        return NULL;
    };

    json->type = JSON_NULL;

    return json;
}

json_t *json_create_false(){
    json_t *json = calloc(1, sizeof(json_t));
    if(!json) {
        LOG("Malloc failed.");
        return NULL;
    };

    json->type = JSON_FALSE;

    return json;
}

json_t *json_create_true(){
    json_t *json = calloc(1, sizeof(json_t));
    if(!json) {
        LOG("Malloc failed.");
        return NULL;
    };


    json->type = JSON_TRUE;

    return json;
}

json_t *json_create_string(const char *string){
    json_t *json = calloc(1, sizeof(json_t));
    if(!json) {
        LOG("Malloc failed.");
        return NULL;
    };


    json->type = JSON_STRING;
    json->value.string = strdup(string);

    if(!json->value.string){
        free(json);
        return NULL;
    }

    return json;
}

json_t *json_create_number(double number){
    json_t *json = calloc(1, sizeof(json_t));
    if(!json) {
        LOG("Malloc failed.");
        return NULL;
    };


    json->type = JSON_NUMBER;
    json->value.number = number;

    return json;
}

json_t *json_create_object(){
    json_t *json = calloc(1, sizeof(json_t));
    if(!json) {
        LOG("Malloc failed.");
        return NULL;
    };


    json->type = JSON_OBJECT;
    json->value.object.key = NULL;
    json->value.object.value = NULL;
    json->value.object.next = NULL;

    return json;
}

json_t *json_create_array(size_t initial_capacity){
    json_t *array = calloc(1, sizeof(json_t));
    if(!array) {
        LOG("Malloc failed.");
        return NULL;
    };


    array->type = JSON_ARRAY;
    array->value.array.size = 0;
    array->value.array.capacity = initial_capacity > 0 ? initial_capacity : 16;
    array->value.array.elements = calloc(array->value.array.capacity, sizeof(json_t*));
    if(!array->value.array.elements){
        free(array);
        return NULL;
    }

    return array;
}

void json_free(json_t *json){
    if(!json) return;

    switch(json->type){
        case JSON_STRING: {
            free(json->value.string);
            break;
        }

        case JSON_ARRAY: {
            for(size_t i = 0; i < json->value.array.size; ++i){
                json_free(json->value.array.elements[i]);
            }
            free(json->value.array.elements);
            break;
        }

        case JSON_OBJECT: {
            json_t *current = json->value.object.next;
            while(current){
                json_t *next = current->value.object.next;
                free(current->value.object.key);
                json_free(current->value.object.value);
                free(current);
                current = next;
            }
            break;
        }
        default:
            break;
    }

    free(json);
}

int json_array_add(json_t *array, json_t *value){
    if(!json_is_array(array)) return -1;

    if(array->value.array.size >= array->value.array.capacity){
        array->value.array.capacity *= 2;
        array->value.array.elements = realloc(array->value.array.elements, array->value.array.capacity * sizeof(json_t*));
    }

    array->value.array.elements[array->value.array.size++] = value;

    return 0;
}

int json_object_add(json_t *object, const char *key, json_t *value){
    if(object->type != JSON_OBJECT) return -1;

    json_t *new_pair = json_create_object();
    if(!new_pair){
        LOG("json_t json_create_object failed");
        return -2;
    }

    new_pair->value.object.key = strdup(key);
    if(!new_pair->value.object.key) {
        LOG("json_t strdup failed");
        free(new_pair);
        return -3;
    }
    new_pair->value.object.value = value;

    new_pair->value.object.next = object->value.object.next;
    object->value.object.next = new_pair;


    return 0;
}

int json_object_add_string(json_t *json, const char *key, const char *value){
    if(!json_is_object(json)) return -1;
    json_t *str = json_create_string(value);
    if(!str){
        LOG("json_t json_create_string failed");
        return -1;
    }

    int result = json_object_add(json, key, str);
    if(result != 0){
        LOG("json_t json_object_add failed.");
        return -2;
    }

    return 0;
}

char *json_print(json_t *json){
    size_t max_size = 128;
    size_t pos = 0;

    char *buffer = malloc(max_size);
    if(!buffer){
        LOG("Malloc failed.");
        return NULL;
    }

    json_to_string(json, &buffer, &pos, &max_size);

    buffer[pos] = '\0';
    return buffer;
}

json_t *json_parse(const char *json_str){
    const char *str = json_str;
    json_t *json = parse_value(&str);
    return json;
}

json_t *json_array_get(json_t *array, size_t index){
    if(!json_is_array(array)) return NULL;
    if(index >= array->value.array.size) return NULL;
    return array->value.array.elements[index];
}

json_t *json_object_get(json_t *object, const char *key){
    if(!object || object->type != JSON_OBJECT || !object->value.object.next) return NULL;

    json_t *current = object->value.object.next;
    while(current){
        if(strcmp(current->value.object.key, key) == 0){
            return current;
        }
        current = current->value.object.next;
    }

    return NULL;
}

char *json_object_get_string(json_t *object, const char *key){
    json_t *json = json_object_get(object, key);
    if(!json) return NULL;
    if(json->value.object.value->type != JSON_STRING) return NULL;
    return json->value.object.value->value.string;
}

double *json_object_get_number(json_t *object, const char *key){
    json_t *json = json_object_get(object, key);
    if(!json) return NULL;
    return &json->value.object.value->value.number;
}

json_t *json_object_get_array(json_t *object, const char *key){
    json_t *json = json_object_get(object, key);
    if(!json) return NULL;
    if(json->value.object.value->type != JSON_ARRAY) return NULL;
    return json->value.object.value;
}
