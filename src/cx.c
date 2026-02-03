#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fast_strcat(char *dest, const char *src){
    while (*dest) dest++;
    while((*dest++ = *src++));
}

void process_text(char *s) {
    if (!s) return;

    char *temp_buf = malloc(strlen(s) * 2 + 1);
    if(!temp_buf) {
        perror("Malloc failed");
        return;
    }

    char *read_ptr = s;
    char *write_ptr = temp_buf;

    while (*read_ptr != '\0') {
        if (*read_ptr == '\n') {
            *write_ptr++ = '\\';
            *write_ptr++ = 'n';
            read_ptr++;
        }
        else if (*read_ptr == '"') {
            *write_ptr++ = '\\';
            *write_ptr++ = '"';
            read_ptr++;
        }
        else if (*read_ptr == '\\') {
            *write_ptr++ = '\\';
            *write_ptr++ = '\\';
            read_ptr++;
        }
        else if (*read_ptr == '\t') {
            *write_ptr++ = '\\';
            *write_ptr++ = 't';
            read_ptr++;
        }
        else if (*read_ptr == '\r') {
            read_ptr++;
            *write_ptr++ = '\\';
            *write_ptr++ = 'r';
            read_ptr++;
        }
        else if (((unsigned char)*read_ptr) >= 0x80) {
            unsigned char byte = (unsigned char)*read_ptr;

            int utf8_len = 1;
            if ((byte & 0xE0) == 0xC0) utf8_len = 2;      // 110xxxxx
            else if ((byte & 0xF0) == 0xE0) utf8_len = 3; // 1110xxxx
            else if ((byte & 0xF8) == 0xF0) utf8_len = 4; // 11110xxx

            for (int i = 0; i < utf8_len && *read_ptr != '\0'; i++) {
                *write_ptr++ = *read_ptr++;
            }
        }
        else {
            *write_ptr++ = *read_ptr++;
        }
    }

    *write_ptr = '\0';

    strcpy(s, temp_buf);
    free(temp_buf);
}

int get_file_length(FILE *f){
    fseek(f, 0L, SEEK_END);
    long length = ftell(f);
    fseek(f, 0L, SEEK_SET);
    return length;
}
