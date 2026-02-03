/*
 * CX Template Generator
 *
 * .CX FILE SYNTAX:
 * ================
 * ({name: string, age: int})  - Props struct definition
 *
 * {{=props->name}}            - Output variable/expression
 * {{=%props->src}}            - Include file from dynamic path (from props)
 * {{%./static/file.html}}     - Include file from static path
 *
 * Standard HTML markup is passed through as-is.
 * Output format: ./src/cxc/{filename}.c and .h
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CTEMPLATE_PATH "./src_cxc/ctemplate.c"
#define HTEMPLATE_PATH "./src_cxc/htemplate.h"
#define SAVE_PATH "./src/cxc/"
#define PATH_TO_CX_FILES "./cx_files"

#define MAX_NAME 64
#define MAX_FILE_NAME 128
#define BUFFER_SIZE 1024 * 1024

typedef enum { C, H } Template;

typedef struct {
    char placeholder[128];
    char *replacement;
} placeholder_t;

typedef struct {
    char filename[MAX_NAME];
    char filepath[1024];
    int collision_count;
} processed_file_t;

unsigned long response_length = 1024 * 1024;

int is_hidden(const char *name) { return name[0] == '.'; }

int find_collision(processed_file_t *files, int count, const char *filename) {
    for (int i = 0; i < count; i++) {
        if (strcmp(files[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

int remove_files_recursive(const char *path) {
    struct dirent *entry;
    DIR *dir = opendir(path);

    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[1024] = {0};
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            remove_files_recursive(full_path);
            rmdir(full_path);
        } else {
            unlink(full_path);
        }
    }

    closedir(dir);
    return 0;
}

int clean_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        mkdir(path, 0755);
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: %s exists but is not a directory\n", path);
        return -1;
    }

    if (remove_files_recursive(path) != 0) {
        fprintf(stderr, "Error cleaning directory\n");
        return -1;
    }

    return 0;
}

int replace_placeholder(char **buffer, const char *placeholder,
                        const char *replacement) {
    if (!buffer || !*buffer || !placeholder || !replacement) {
        return -1;
    }

    size_t buffer_len = strlen(*buffer);
    size_t placeholder_len = strlen(placeholder);
    size_t replacement_len = strlen(replacement);

    char *pos = strstr(*buffer, placeholder);

    while (pos != NULL) {
        size_t offset = pos - *buffer;
        size_t new_len = buffer_len - placeholder_len + replacement_len;

        char *new_buffer = realloc(*buffer, new_len + 256);
        if (new_buffer == NULL) {
            perror("Error allocating memory");
            return -1;
        }
        *buffer = new_buffer;
        pos = *buffer + offset;

        memmove(pos + replacement_len, pos + placeholder_len,
                buffer_len - offset - placeholder_len + 1);

        memcpy(pos, replacement, replacement_len);

        buffer_len = new_len;
        pos = strstr(pos + replacement_len, placeholder);
    }

    return 0;
}

int get_props_name(char *dst, const char *filename) {
    char tmp[MAX_NAME];
    strcpy(tmp, filename);

    char *token = strtok(tmp, "_");
    while (token != NULL) {
        *token = toupper(*token);
        strcat(dst, token);
        token = strtok(NULL, "_");
    }
    strcat(dst, "Props");

    return 0;
}

int has_cx_extension(const char *filename) {
    const char *dot = strrchr(filename, '.');
    return (dot && strcmp(".cx", dot) == 0);
}

long get_file_length(FILE *f) {
    fseek(f, 0L, SEEK_END);
    long length = ftell(f);
    fseek(f, 0L, SEEK_SET);
    return length;
}

void process_text(char *s) {
    if (!s)
        return;

    char *temp_buf = malloc(strlen(s) * 2 + 1);
    if (!temp_buf) {
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
        } else if (*read_ptr == '"') {
            *write_ptr++ = '\\';
            *write_ptr++ = '"';
            read_ptr++;
        } else if (*read_ptr == '\\') {
            *write_ptr++ = '\\';
            *write_ptr++ = '\\';
            read_ptr++;
        } else if (*read_ptr == '\t') {
            *write_ptr++ = '\\';
            *write_ptr++ = 't';
            read_ptr++;
        } else if (*read_ptr == '\r') {
            read_ptr++;
            *write_ptr++ = '\\';
            *write_ptr++ = 'r';
            read_ptr++;
        } else if (((unsigned char)*read_ptr) >= 0x80) {
            unsigned char byte = (unsigned char)*read_ptr;
            int utf8_len = 1;

            if ((byte & 0xE0) == 0xC0)
                utf8_len = 2;
            else if ((byte & 0xF0) == 0xE0)
                utf8_len = 3;
            else if ((byte & 0xF8) == 0xF0)
                utf8_len = 4;

            for (int i = 0; i < utf8_len && *read_ptr != '\0'; i++) {
                *write_ptr++ = *read_ptr++;
            }
        } else {
            *write_ptr++ = *read_ptr++;
        }
    }

    *write_ptr = '\0';
    strcpy(s, temp_buf);
    free(temp_buf);
}

void process_code(char *s) {
    if (!s) return;

    char tmp[BUFFER_SIZE] = {0};
    char *start = s;

    while (isspace((unsigned char)*start)) start++;

    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    if (*start == '=' && *(start + 1) == '%') {
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s", start + 2);

        snprintf(tmp, BUFFER_SIZE,
                 "\tFILE *html_file = fopen(%s, \"r\");\n"
                 "\tif(!html_file) {\n"
                 "\t\tfast_strcat(output, \"HTML file not found : %s\");\n"
                 "\t\treturn output;\n"
                 "\t}\n"
                 "\tint html_size = get_file_length(html_file);\n"
                 "\tchar *html_content = malloc(html_size + 1);\n"
                 "\tif(!html_content) {\n"
                 "\t\tfast_strcat(output, \"Malloc failed?\");\n"
                 "\t\treturn output;\n"
                 "\t}\n"
                 "\tfread(html_content, 1, html_size, html_file);\n"
                 "\thtml_content[html_size] = '\\0';\n"
                 "\tfast_strcat(output, html_content);\n"
                 "\tfree(html_content);\n"
                 "\tfclose(html_file);\n"
                 "\t",
                 path, path);

        FILE *f = fopen(path, "r");
        if (f) {
            response_length += get_file_length(f);
            fclose(f);
        }
        else {
            response_length += 1024;
        }
    }
    else if (*start == '=') {
        snprintf(tmp, BUFFER_SIZE, "\tfast_strcat(output, %s);", start + 1);
    }
    else if (*start == '/') {
        snprintf(tmp, BUFFER_SIZE, "\t}\n");
    }
    else if (*start == '?' && *(start + 1) != '\0') {
        char condition[256] = {0};
        strncpy(condition, start + 1, sizeof(condition) - 1);
        snprintf(tmp, BUFFER_SIZE, "\tif (%s) {\n", condition);
    }
    else if (*start == '%') {
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s", start + 1);

        snprintf(tmp, BUFFER_SIZE,
                 "\tFILE *html_file = fopen(\"%s\", \"r\");\n"
                 "\tif(!html_file) {\n"
                 "\t\tfast_strcat(output, \"HTML file not found : %s\");\n"
                 "\t\treturn output;\n"
                 "\t}\n"
                 "\tint html_size = get_file_length(html_file);\n"
                 "\tchar *html_content = malloc(html_size + 1);\n"
                 "\tif(!html_content) {\n"
                 "\t\tfast_strcat(output, \"Malloc failed?\");\n"
                 "\t\treturn output;\n"
                 "\t}\n"
                 "\tfread(html_content, 1, html_size, html_file);\n"
                 "\thtml_content[html_size] = '\\0';\n"
                 "\tfast_strcat(output, html_content);\n"
                 "\tfree(html_content);\n"
                 "\tfclose(html_file);\n"
                 "\t",
                 path, path);

        FILE *f = fopen(path, "r");
        if (f) {
            response_length += get_file_length(f);
            fclose(f);
        }
        else {
            response_length += 1024;
        }
    }
    else {
        snprintf(tmp, BUFFER_SIZE, "\t%s", start);
    }

    strcpy(s, tmp);
    strcat(s, "\n");
}

char *get_template(Template t) {
    const char *path = (t == H) ? HTEMPLATE_PATH : CTEMPLATE_PATH;
    FILE *f = fopen(path, "r");

    if (f == NULL) {
        perror("Could not open template");
        return NULL;
    }

    long length = get_file_length(f);
    if (length <= 0) {
        perror("Template file is empty");
        fclose(f);
        return NULL;
    }

    char *content = calloc(length + 1, sizeof(char));
    if (content == NULL) {
        perror("Error allocating memory");
        fclose(f);
        return NULL;
    }

    fread(content, 1, length, f);
    fclose(f);
    return content;
}

int save_file(const char *path, const char *buf) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        perror("Error opening file for writing");
        return -1;
    }

    fwrite(buf, 1, strlen(buf), f);
    fclose(f);
    return 0;
}

int generate(FILE *f, const char *filename, long length, char *ctemp,
             char *htemp) {
    response_length = 1024 * 1024;

    char *content = calloc(length + 1, sizeof(char));
    if (content == NULL) {
        perror("Failed to allocate buffer");
        return -1;
    }

    fread(content, 1, length, f);
    content[length] = '\0';

    char props_name[MAX_NAME] = {0};
    char main_function_name[MAX_NAME] = {0};
    char file_id[MAX_NAME] = {0};
    char props_struct[BUFFER_SIZE] = {0};
    char prepend[BUFFER_SIZE] = {0};
    char function_code[BUFFER_SIZE] = {0};

    get_props_name(props_name, filename);
    snprintf(main_function_name, sizeof(main_function_name), "render_%s",
             filename);
    snprintf(file_id, sizeof(file_id), "%s", filename);

    char *props_start = strstr(content, "({") + 2;
    if (props_start != NULL) {
        char *props_end = strstr(props_start, "})");
        if (props_end == NULL) {
            perror("Could not find }) closing props struct");
            free(content);
            return -1;
        }
        strncat(props_struct, props_start, props_end - props_start);
        props_start = props_end + 2;
    } else {
        props_start = content;
    }

    char *first_html = strstr(props_start, "\n<");
    if (first_html == NULL) {
        perror("No HTML found, aborting");
        free(content);
        return -1;
    }
    strncat(prepend, props_start, first_html - props_start);

    char *ptr = first_html + 1;
    char *code_start, *code_end;

    while ((code_start = strstr(ptr, "{{")) && (code_end = strstr(ptr, "}}"))) {
        char text[BUFFER_SIZE / 2] = {0};
        strncpy(text, ptr, code_start - ptr);
        process_text(text);
        snprintf(function_code + strlen(function_code), BUFFER_SIZE,
                 "\tfast_strcat(output, \"%s\");\n", text);
        response_length += strlen(text);

        char code[BUFFER_SIZE] = {0};
        strncpy(code, code_start + 2, code_end - code_start - 2);
        process_code(code);
        strcat(function_code, code);
        response_length += 10 + strlen(code);

        ptr = code_end + 2;
    }

    if (ptr != NULL && *ptr != '\0') {
        char remaining[BUFFER_SIZE / 2] = {0};
        strcpy(remaining, ptr);
        process_text(remaining);
        snprintf(function_code + strlen(function_code), BUFFER_SIZE,
                 "\tfast_strcat(output, \"%s\");\n", remaining);
        response_length += strlen(remaining);
    }

    char *c_output = malloc(BUFFER_SIZE);
    char *h_output = malloc(BUFFER_SIZE);

    if (c_output == NULL || h_output == NULL) {
        perror("Error allocating memory");
        free(content);
        return -1;
    }

    strcpy(c_output, ctemp);
    strcpy(h_output, htemp);

    char response_length_buffer[128] = {0};
    snprintf(response_length_buffer, 128, "%ld", response_length);

    placeholder_t c_placeholders[] = {
        {"%%CODE%%", function_code},
        {"%%FUNC_NAME%%", main_function_name},
        {"%%PROPS_NAME%%", props_name},
        {"%%PREPEND%%", prepend},
        {"%%NAME%%", file_id},
        {"%%RESPONSE_SIZE%%", response_length_buffer}};

    for (int i = 0; i < 6; i++) {
        if (replace_placeholder(&c_output, c_placeholders[i].placeholder,
                                c_placeholders[i].replacement) < 0) {
            perror("Replace error");
            free(c_output);
            free(h_output);
            free(content);
            return -1;
        }
    }

    char h_file_id[256] = {0};
    snprintf(h_file_id, sizeof(h_file_id), "_%s_H", file_id);

    placeholder_t h_placeholders[] = {{"%%FILE_ID%%", h_file_id},
                                    {"%%PROPS%%", props_struct},
                                    {"%%STRUCT_NAME%%", props_name},
                                    {"%%FUNC_NAME%%", main_function_name}};

    for (int i = 0; i < 4; i++) {
        if (replace_placeholder(&h_output, h_placeholders[i].placeholder,
                                h_placeholders[i].replacement) < 0) {
            perror("Replace error");
            free(c_output);
            free(h_output);
            free(content);
            return -1;
        }
    }

    char c_file[MAX_FILE_NAME] = {0};
    snprintf(c_file, sizeof(c_file), "%s%s.c", SAVE_PATH, filename);
    if (save_file(c_file, c_output) != 0) {
        free(c_output);
        free(h_output);
        free(content);
        return -1;
    }

    char h_file[MAX_FILE_NAME] = {0};
    snprintf(h_file, sizeof(h_file), "%s%s.h", SAVE_PATH, filename);
    if (save_file(h_file, h_output) != 0) {
        free(c_output);
        free(h_output);
        free(content);
        return -1;
    }

    free(c_output);
    free(h_output);
    free(content);
    return 0;
}

int process_directory_recursive(const char *dir_path, char *ctemp, char *htemp,
                                processed_file_t *files, int *processed_count,
                                int *total_capacity) {
    struct dirent *entry;
    DIR *dir = opendir(dir_path);

    if (dir == NULL) {
        perror("Unable to open directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (is_hidden(entry->d_name)) {
            continue;
        }

        char path[1024] = {0};
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            process_directory_recursive(path, ctemp, htemp, files,
                                        processed_count, total_capacity);
        } else if (entry->d_type == DT_REG && has_cx_extension(entry->d_name)) {
            char filename[MAX_NAME] = {0};
            const char *dot = strrchr(entry->d_name, '.');
            size_t name_len =
                dot ? (size_t)(dot - entry->d_name) : strlen(entry->d_name);
            strncpy(filename, entry->d_name,
                    name_len < MAX_NAME ? name_len : MAX_NAME - 1);
            filename[name_len < MAX_NAME ? name_len : MAX_NAME - 1] = '\0';

            int collision_idx =
                find_collision(files, *processed_count, filename);
            if (collision_idx >= 0) {
                files[collision_idx].collision_count++;
                fprintf(stderr,
                        "WARNING: Duplicate filename '%s' found (count: %d)\n",
                        filename, files[collision_idx].collision_count);
                fprintf(stderr, "  First:  %s\n",
                        files[collision_idx].filepath);
                fprintf(stderr, "  Current: %s\n", path);
                fprintf(stderr,
                        "  Skipping current file to avoid overwriting.\n");
                continue;
            }

            if (*processed_count >= *total_capacity) {
                *total_capacity *= 2;
                processed_file_t *new_files =
                    realloc(files, sizeof(processed_file_t) * (*total_capacity));
                if (new_files == NULL) {
                    perror("Failed to expand file tracking array");
                    closedir(dir);
                    return -1;
                }
                files = new_files;
            }

            FILE *file = fopen(path, "r");
            if (file == NULL) {
                fprintf(stderr, "Failed to open %s\n", path);
                continue;
            }

            long length = get_file_length(file);
            if (length <= 0) {
                fprintf(stderr, "Invalid file length for %s: %ld\n", path,
                        length);
                fclose(file);
                continue;
            }

            int res = generate(file, filename, length, ctemp, htemp);
            fclose(file);

            if (res < 0) {
                fprintf(stderr, "Failed to generate files for %s\n", filename);
            } else {
                strcpy(files[*processed_count].filename, filename);
                strcpy(files[*processed_count].filepath, path);
                files[*processed_count].collision_count = 0;
                printf("Generated: %s.c and %s.h (from %s)\n", filename,
                       filename, path);
                (*processed_count)++;
            }
        }
    }

    closedir(dir);
    return 0;
}

int main(void) {
    if (clean_directory(SAVE_PATH) != 0) {
        fprintf(stderr, "Failed to clean output directory\n");
        return EXIT_FAILURE;
    }

    printf("Cleaned output directory: %s\n\n", SAVE_PATH);

    char *ctemplate = get_template(C);
    char *htemplate = get_template(H);

    if (ctemplate == NULL || htemplate == NULL) {
        if (ctemplate)
            free(ctemplate);
        if (htemplate)
            free(htemplate);
        return EXIT_FAILURE;
    }

    int processed_count = 0;
    int capacity = 256;
    processed_file_t *files = malloc(sizeof(processed_file_t) * capacity);

    if (files == NULL) {
        perror("Failed to allocate file tracking array");
        free(ctemplate);
        free(htemplate);
        return EXIT_FAILURE;
    }

    if (process_directory_recursive(PATH_TO_CX_FILES, ctemplate, htemplate,
                                    files, &processed_count, &capacity) != 0) {
        free(ctemplate);
        free(htemplate);
        free(files);
        return EXIT_FAILURE;
    }

    free(ctemplate);
    free(htemplate);
    free(files);

    printf("\nProcessed: %d files\n", processed_count);
    return EXIT_SUCCESS;
}
