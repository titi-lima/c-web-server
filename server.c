#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8082
#define BUFFER_SIZE 2097152

const char *get_file_extension(const char *file_name) {
    const char *dot = strrchr(file_name, '.');
    if (!dot || dot == file_name) {
        return "";
    }
    return dot + 1;
}

const char *get_mime_type(const char *file_ext) {
    if (strcasecmp(file_ext, "html") == 0 || strcasecmp(file_ext, "htm") == 0) {
        return "text/html";
    } else if (strcasecmp(file_ext, "txt") == 0) {
        return "text/plain";
    } else if (strcasecmp(file_ext, "jpg") == 0 || strcasecmp(file_ext, "jpeg") == 0) {
        return "image/jpeg";
    } else if (strcasecmp(file_ext, "png") == 0) {
        return "image/png";
    } else {
        return "application/octet-stream";
    }
}

bool case_insensitive_compare(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (tolower((unsigned char)*s1) != tolower((unsigned char)*s2)) {
            return false;
        }
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

char *get_file_case_insensitive(const char *file_name) {
    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("opendir");
        return NULL;
    }

    struct dirent *entry;
    char *found_file_name = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (case_insensitive_compare(entry->d_name, file_name)) {
            found_file_name = entry->d_name;
            break;
        }
    }

    closedir(dir);
    return found_file_name;
}

void build_http_response(const char *file_name,
                         const char *file_ext,
                         char *response,
                         size_t *response_len) {
    // build HTTP header
    const char *mime_type = get_mime_type(file_ext);
    char *header = (char *)malloc(BUFFER_SIZE * sizeof(char));
    snprintf(header, BUFFER_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "\r\n",
             mime_type);

    // if file does not exist, response is 404 Not Found
    int file_fd = open(file_name, O_RDONLY);
    if (file_fd == -1) {
        snprintf(response, BUFFER_SIZE,
                 "HTTP/1.1 404 Not Found\r\n"
                 "Content-Type: text/plain\r\n"
                 "\r\n"
                 "404 Not Found");
        *response_len = strlen(response);
        return;
    }

    // get file size for Content-Length
    struct stat file_stat;
    fstat(file_fd, &file_stat);
    off_t file_size = file_stat.st_size;
    snprintf(header, BUFFER_SIZE,
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "\r\n",
             mime_type, file_size);

    // copy header to response buffer
    *response_len = 0;
    memcpy(response, header, strlen(header));
    *response_len += strlen(header);

    // copy file to response buffer
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd,
                              response + *response_len,
                              BUFFER_SIZE - *response_len)) > 0) {
        *response_len += bytes_read;
    }
    free(header);
    close(file_fd);
}

char *url_decode(const char *src) {
    size_t src_len = strlen(src);
    char *decoded = malloc(src_len + 1);
    size_t decoded_len = 0;

    // decode %2x to hex
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            int hex_val;
            sscanf(src + i + 1, "%2x", &hex_val);
            decoded[decoded_len++] = hex_val;
            i += 2;
        } else {
            decoded[decoded_len++] = src[i];
        }
    }

    // add null terminator
    decoded[decoded_len] = '\0';
    return decoded;
}

void *client_handler(void *client_fd) {
    int client = *(int *)client_fd;
    char buffer[BUFFER_SIZE] = {0};
    ssize_t bytes_received = recv(client, buffer, BUFFER_SIZE, 0);

    if (bytes_received < 0) {
        perror("Error receiving data from client");
        exit(1);
    }
    printf("Data received: %s\n", buffer);
    regex_t regex;
    regcomp(&regex, "GET /([^ ]*) HTTP/1.1", REG_EXTENDED);
    regmatch_t matches[2];

    if (regexec(&regex, buffer, 2, matches, 0) != 0) {
        perror("Error matching regex");
        exit(1);
    }

    buffer[matches[1].rm_eo] = 0;
    const char *url_encoded_file_name = buffer + matches[1].rm_so;
    char *file_name = url_decode(url_encoded_file_name);

    // get file extension
    char file_ext[32];
    strcpy(file_ext, get_file_extension(file_name));

    // build HTTP response
    char response[BUFFER_SIZE * 2];
    size_t response_len;
    build_http_response(file_name, file_ext, response, &response_len);

    // send HTTP response to client
    send(client, response, response_len, 0);

    free(file_name);
    free(client_fd);
    regfree(&regex);
    close(client);
    return NULL;
}

int main() {

    struct sockaddr_in server_addr = {sin_family : AF_INET, sin_addr : {s_addr : INADDR_ANY}, sin_port : htons(PORT)};
    int server_fd;

    // create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Error creating server socket");
        exit(1);
    }
    printf("Server socket created\n");

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    // bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding server socket");
        exit(1);
    }
    printf("Server socket binded\n");

    // listen
    if (listen(server_fd, 3) < 0) {
        perror("Error listening server socket");
        exit(1);
    }
    printf("Server socket listening\n");

    while (1) {
        int *client_fd = (int *)malloc(sizeof(int));
        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);

        // accept client connection
        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
        if (client_fd < 0) {
            perror("Error accepting client connection");
            exit(1);
        }
        printf("Client connected\n");

        // create thread for client
        pthread_t client_thread;
        pthread_create(&client_thread, NULL, client_handler, (void *)client_fd);
        pthread_detach(client_thread);
    }

    close(server_fd);
}