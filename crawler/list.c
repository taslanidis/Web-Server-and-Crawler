#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>                                                             /* in_port_t, in_address_t */
#include <sys/socket.h>                                                         /* sockets */
#include <sys/types.h>                                                          /* sockets */
#include <sys/wait.h>                                                           /* sockets */
#include <sys/stat.h>                                                           /* folders */
#include <unistd.h>                                                             /* fork */
#include <netdb.h>                                                              /* gethostbyaddr */
#include <signal.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>                                                            /* threads */
#include <poll.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "list.h"
#include "Trie.h"

#define MAX_BUFF 512

pthread_mutex_t mtx;
pthread_mutex_t buff_mtx;
pthread_mutex_t file_mtx;
pthread_mutex_t socket_mtx;
pthread_cond_t cond_nonempty;
pthread_cond_t cond_nonfull;

void* client_threads(void* args){
    printf("Thread creation\n");
    struct argv *arguments = (struct argv*)args;
    struct listHead* head = arguments->head;
    struct sockaddr_in* server = arguments->server;
    char* save_dir = malloc((strlen(arguments->dirname) + 1)*sizeof(char));
    strcpy(save_dir, arguments->dirname);
    struct shared_used_st *shared_stuff = arguments->shared;
    int service_socket, bytes_read, bytes_wrote;
    char* http_answer = malloc((MAX_BUFF)*sizeof(char));
    char* http_request = malloc((MAX_BUFF + 1)*sizeof(char));
    struct stat sb;
    while(1){
        strcpy(http_request,"");
        strcpy(http_answer,"");
        char* req_link = obtain(head);
        /* create dir if it doesn't exist */
        char* dirbuf = malloc((strlen(req_link) + strlen(save_dir) + 6)*sizeof(char));
        strcpy(dirbuf, req_link);
        char* dir = strtok(dirbuf, "/");
        char* newdir = malloc((strlen(req_link) + strlen(save_dir) + 6)*sizeof(char));
        strcpy(newdir, "./");
        strcat(newdir, save_dir);
        strcat(newdir, "/");
        strcat(newdir, dir);

        if (!((stat(newdir, &sb) == 0) && S_ISDIR(sb.st_mode))){
            if(mkdir(newdir, 0777) == -1){
                perror("mkdir");
            }else{
                chmod(newdir, 0777);
            }
        }

        strcpy(newdir, "./");
        strcat(newdir, save_dir);
        strcat(newdir, req_link);
        /* end */
        pthread_mutex_lock(&file_mtx);
        if ( access(newdir, F_OK) == -1 ){
            FILE* fd = fopen(newdir, "w");
            if (fd == NULL) perror("fopen");
            pthread_mutex_unlock(&file_mtx);
            /* service socket connect */
            if ((service_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
                perror("Socket creation failed!");
                exit(1);
            }

            if (connect(service_socket, (struct sockaddr *)server, sizeof(*server)) < 0){
                perror("connect");
                exit(1);
            }
            /* end */
            strcpy(http_answer,"");
            /* assembling request */
            strcpy(http_request,"GET ");
            strcat(http_request, req_link);
            strcat(http_request, " HTTP/1.1\n");
            strcat(http_request, "User-Agent: AnInsaneCrawler/1.0\n");
            strcat(http_request, "Host: ");
            char* host = malloc(45*sizeof(char));
            inet_ntop(AF_INET, &server->sin_addr, host, 45);
            strcat(http_request, host);
            strcat(http_request, "\n");
            strcat(http_request, "Connection: keep-alive\n");
            /* end */
            int valid = 1;
            char* buffer;
            bytes_wrote = write(service_socket, http_request, MAX_BUFF);
            if(bytes_wrote < strlen(http_request)){
                bytes_wrote += write(service_socket, http_request + bytes_wrote, MAX_BUFF);
            }
            bytes_read = read(service_socket, http_answer, MAX_BUFF);
            if(bytes_read <= 0){
                printf("Unpredicted close of connection\n");
                close(service_socket);
                valid = 0;
                continue;
            }
            /* check if page is found */
            if(valid == 1){
                buffer = strtok(http_answer, " \n");
                buffer = strtok(NULL, " \n");
                if(buffer != NULL){
                    if (strcmp(buffer, "400") == 0){
                        printf("Error: %s\n", buffer);
                        buffer = strtok(NULL, " \n");
                        free(newdir);
                        free(dirbuf);
                        close(service_socket);
                        continue;
                    }else if (strcmp(buffer, "404") == 0){
                        printf("Error: %s\n", buffer);
                        buffer = strtok(NULL, " \n");
                        buffer = strtok(NULL, " \n");
                        free(newdir);
                        free(dirbuf);
                        close(service_socket);
                        continue;
                    }else if (strcmp(buffer, "200") == 0){
                        buffer = strtok(NULL, " \n");
                    }else{
                        valid = 0;
                    }
                }else{
                    valid = 0;
                }
            }
            int length = -1;
            while(valid == 1){
                buffer = strtok(NULL, " \n");
                if (buffer == NULL){
                    if (length < 0){
                        valid = 0;
                    }
                    break;
                }

                if (strcmp(buffer, "Server:") == 0){
                    buffer = strtok(NULL, " \n");
                    buffer = strtok(NULL, " \n");
                }else if (strcmp(buffer, "Content-Length:") == 0){
                    buffer = strtok(NULL, " \n");
                    length = atoi(buffer);
                }else if (strcmp(buffer, "Content-Type:") == 0){
                    buffer = strtok(NULL, " \n");
                }else if (strcmp(buffer, "Connection:") == 0){
                    buffer = strtok(NULL, " \n");
                }
            }
            /* end */
            /* search for links/download files */
            if(valid == 1){
                pthread_mutex_lock(&buff_mtx);                                  /* shared memory write */
                shared_stuff->pages++;
                pthread_mutex_unlock(&buff_mtx);
                bytes_read = read(service_socket, http_answer, MAX_BUFF);
                if(bytes_read <= 0){
                    free(newdir);
                    free(dirbuf);
                    close(service_socket);
                    continue;
                }
                char* link;
                char* link2;
                char* path = malloc((MAX_BUFF + 1)*sizeof(char));
                strcpy(path, "");
                int ready = 0;
                int read_again = 0;
                /* inserting links to list and downloading files */
                while(bytes_read > 0){
                    pthread_mutex_lock(&buff_mtx);
                    shared_stuff->bytes += bytes_read;
                    pthread_mutex_unlock(&buff_mtx);
                    fprintf(fd, "%s", http_answer);
                    link = strchr(http_answer, '<');
                    while(link != NULL){
                        if(read_again == 1){                                    /* used when a link is not sent whole in one read */
                            strcat(path, strtok(http_answer, ">"));
                            ready = 1;
                        }
                        if(strncmp(link, "<a href=", 8) == 0){                  /* check if it is a tag of link */
                            strcpy(path, "");
                            link = link + 8;
                            if(strchr(link, '>') != NULL){
                                ready = 1;
                            }else{
                                read_again = 1;
                            }
                            link2 = strtok(link, ">");
                            if(link2 != NULL)
                                strcpy(path, link2);
                        }

                        if(ready == 1){
                            memmove(path, path + 2, strlen(path) - 2);
                            memset(path + strlen(path) - 2, '\0', 1);
                            ListInsert(head, path);
                            ready = 0;
                            read_again = 0;
                        }
                        if((link + 1) == NULL)  break;
                        link = strchr(link + 1, '<');                           /* got to the next < */

                    }
                    bytes_read = read(service_socket, http_answer, MAX_BUFF);
                }
                /* end of downloading file */
                free(path);
            }
            /* end */
            free(host);
            free(req_link);
            fclose(fd);
            close(service_socket);
        }else{
            pthread_mutex_unlock(&file_mtx);
        }
        free(newdir);
        free(dirbuf);
    }
    free(http_request);
    free(http_answer);
    free(save_dir);
}

struct listHead* ListInit(){
    struct listHead* head = malloc(sizeof(struct listHead));
    head->links = 0;
    head->first = NULL;
    /* initialization of mutexes */
    pthread_mutex_init(&mtx, 0);
    pthread_mutex_init(&buff_mtx, 0);
    pthread_mutex_init(&file_mtx, 0);
    pthread_mutex_init(&socket_mtx, 0);
    pthread_cond_init(&cond_nonempty, 0);
    pthread_cond_init(&cond_nonfull, 0);
    return head;
}

int ListInsert(struct listHead* head, char* path){
    pthread_mutex_lock(&mtx);
    struct list* current = head->first;
    struct list* newnode;
    if(current != NULL){
        while(current->next != NULL){
            current = current->next;
        }
        newnode = malloc(sizeof(struct list));
        newnode->next = current->next;
        current->next = newnode;
    }else{
        newnode = malloc(sizeof(struct list));
        head->first = newnode;
        newnode->next = NULL;
    }
    head->links++;
    newnode->link = malloc((strlen(path) + 1)*sizeof(char));
    strcpy(newnode->link, path);
    pthread_cond_signal(&cond_nonempty);
    pthread_mutex_unlock(&mtx);
    return 1;
}

char* obtain(struct listHead* head){
    pthread_mutex_lock(&mtx);
    while(head->links <= 0){
        /* Found list empty */
        printf("found list empty\n");
        pthread_cond_wait(&cond_nonempty, &mtx);
    }
    char* temp = malloc((strlen(head->first->link) + 1)*sizeof(char));
    strcpy(temp, head->first->link);
    head->links--;
    struct list* tempnode = head->first;
    head->first = head->first->next;
    free(tempnode->link);
    free(tempnode);
    pthread_mutex_unlock(&mtx);
    return temp;
}

void ListDelete(struct listHead* head){
    struct list* temp;
    struct list* current = head->first;
    while(current != NULL){
        temp = current;
        current = current->next;
        free(temp);
    }
    free(head);
}

int Crawling(struct listHead* head){
    return (head->links > 0);
}
