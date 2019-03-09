#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>                                                             /* in_port_t, in_address_t */
#include <sys/socket.h>                                                         /* sockets */
#include <sys/types.h>                                                          /* sockets */
#include <sys/wait.h>                                                           /* sockets */
#include <sys/stat.h>
#include <unistd.h>                                                             /* fork */
#include <netdb.h>                                                              /* gethostbyaddr */
#include <signal.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <pthread.h>                                                            /* threads */
#include <sys/shm.h>
#include <time.h>
#include "server_tools.h"
#include "sockets.h"

#define MAX_BUFF 512

pthread_mutex_t mtx;
pthread_mutex_t buff_mtx;                                                       /* mutex for shared memory */
pthread_cond_t cond_nonempty;
pthread_cond_t cond_nonfull;

int POOL_SIZE;

double my_clock(void) {                                                         //function to return current clock
    struct timeval t;
    gettimeofday(&t, NULL);
    return (1.0e-6*t.tv_usec + t.tv_sec);
}

void *server_threads(void *args){
    int socket;
    FILE* fd;
    char ch;
    char* path;
    char* tempbuf;
    char* buffer;
    char* link;
    char* http_request = malloc((MAX_BUFF + 1)*sizeof(char));
    strcpy(http_request, "");
    int valid = 1;
    struct argv *arguments = (struct argv*)args;                                /* arguments required for thread */
    pool_t* pool = arguments->tpool;
    struct shared_used_st *shared_stuff = arguments->shared;
    char* root_dir = arguments->dirname;
    while(1){
        socket = obtain(pool);                                                  /* obtaining a socket fd from pool */
        //printf("Socket %d\n", socket);
        tempbuf = malloc((MAX_BUFF)*sizeof(char));
        memset(tempbuf,0,MAX_BUFF);
        size_t bytes_wrote, bytes_read = 0;
        bytes_read = read(socket, http_request, MAX_BUFF);                      /* reading the request */
        valid = 1;
        if(bytes_read <= 0){                                                    /* connection is closed */
            free(tempbuf);
            close(socket);
            continue;                                                           /* next iteration */
        }
        char* request_dup = strdup(http_request);
        /* checking the validity of the request */
        link = NULL;
        buffer = strtok(request_dup, " \n");
        int request_format = -1;
        while(valid == 1){                                                      /* checking for the validity of the reques header format */
            if (strcmp(buffer, "GET") == 0){
                link = strtok(NULL, " \n");
                buffer = strtok(NULL, " \n");                                   /* http 1.1 */
                request_format++;
            }else if (strcmp(buffer, "User-Agent:") == 0){
                buffer = strtok(NULL, " \n");
            }else if (strcmp(buffer, "Host:") == 0){
                buffer = strtok(NULL, " \n");
                request_format++;
            }else if (strcmp(buffer, "Connection:") == 0){
                buffer = strtok(NULL, " \n");
            }
            /* the fields host and get are mandatory, so I use the request_format flag */
            buffer = strtok(NULL, " \n");
            if (buffer == NULL){
                if (link == NULL) valid = 0;
                break;
            }
        }
        /* end */
        if(request_format != 1) valid = 0;                                      /* one of get or host field is missing (or both)*/
        /* setting date */
        char* date = malloc(120*sizeof(char));
        time_t t = time(NULL);
        struct tm tm = *gmtime(&t);
        sprintf(date, "Date: %d-%d-%d %d:%d:%d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        /* end */
        /* answering to client with the page requested */
        if(valid == 1){
            path = malloc((strlen(link) + strlen(root_dir) + 5)*sizeof(char));
            strcpy(path, root_dir);
            strcat(path, link);
            printf("link = %s\n", path);
            if ((fd = fopen(path, "r")) == NULL){
                sprintf(tempbuf,"HTTP/1.1 404 Not found\n%s\nServer: myhttpd/1.0.0 (Ubunutu64)\nContent-Length: %zu\nContent-Type: text/html\nConnection: Closed\n\n<html>Sorry dude, I couldn't find this file</html>\n", date, 124 + strlen(date));
                bytes_wrote = write(socket, tempbuf, MAX_BUFF);
                if (bytes_wrote < strlen(tempbuf)){
                    bytes_wrote += write(socket, tempbuf + bytes_wrote, MAX_BUFF);
                }
            }else{
                int total_bytes = 0;
                while(1){
                    ch = fgetc(fd);
                    if(feof(fd)){
                       break;
                    }else{
                        total_bytes++;
                    }
                }
                fseek(fd, 0, SEEK_SET);
                pthread_mutex_lock(&buff_mtx);
                shared_stuff->pages++;
                pthread_mutex_unlock(&buff_mtx);
                sprintf(tempbuf,"HTTP/1.1 200 OK\n%s\nServer: myhttpd/1.0.0 (Ubunutu64)\nContent-Length: %d\nContent-Type: text/html\nConnection: Closed\n\n", date, total_bytes);
                bytes_wrote = write(socket, tempbuf, MAX_BUFF);
                if (bytes_wrote < strlen(tempbuf)){
                    bytes_wrote += write(socket, tempbuf + bytes_wrote, MAX_BUFF);
                }
                int count = 0;
                /* writing the page requested on sokcet */
                memset(tempbuf,0,MAX_BUFF);
                while(1){
                    ch = fgetc(fd);
                    if(feof(fd)){
                        if(count == 0) break;
                        //tempbuf[count] = '\0';
                        bytes_wrote = write(socket, tempbuf, MAX_BUFF);
                        if(bytes_wrote <= 0){
                            bytes_wrote = write(socket, tempbuf, MAX_BUFF);
                        }
                        strcpy(tempbuf,"");
                        pthread_mutex_lock(&buff_mtx);
                        shared_stuff->bytes += bytes_wrote;
                        pthread_mutex_unlock(&buff_mtx);
                        break;
                    }else{
                        tempbuf[count] = ch;
                        count++;
                    }
                    if (count >= MAX_BUFF){
                        bytes_wrote = write(socket, tempbuf, MAX_BUFF);
                        if (bytes_wrote < strlen(tempbuf)){
                            bytes_wrote += write(socket, tempbuf + bytes_wrote, MAX_BUFF);
                        }
                        memset(tempbuf,0,strlen(tempbuf));
                        pthread_mutex_lock(&buff_mtx);
                        shared_stuff->bytes += bytes_wrote;
                        pthread_mutex_unlock(&buff_mtx);
                        count = 0;
                    }
                }
                /* end */
                fclose(fd);
            }
            free(path);
        }else{
            printf("Bad request! \n");
            sprintf(tempbuf,"HTTP/1.1 400 Bad Request\n%s\nServer: myhttpd/1.0.0 (Ubunutu64)\nContent-Length: %zu\nContent-Type: text/html\nConnection: Closed\n\n<html>That was a bad request mate</html>\n", date, 124 + strlen(date));
            write(socket, tempbuf, MAX_BUFF);
        }
        /* end of answer */
        free(date);
        close(socket);
        valid = 1;
        free(tempbuf);
    }
    free(http_request);
}

pool_t* initialize_pool(int num_of_threads){
    POOL_SIZE = 10*num_of_threads;                                              /* max connections for safety */
    pool_t *pool = malloc(sizeof(pool_t));
    pool->data = malloc(POOL_SIZE*sizeof(int));
    pool->start = 0;
    pool->end = -1;
    pool->count = 0;
    pthread_mutex_init(&mtx, 0);
    pthread_mutex_init(&buff_mtx, 0);
    pthread_cond_init(&cond_nonempty, 0);
    pthread_cond_init(&cond_nonfull, 0);
    return pool;
}

void place(pool_t* pool, int fileDesc){
    pthread_mutex_lock(&mtx);
    while(pool->count >= POOL_SIZE){
        /* Found buffer full */
        printf("found buffer full\n");
        pthread_cond_wait(&cond_nonfull, &mtx);
    }
    pool->end = (pool->end + 1)%POOL_SIZE;
    pool->data[pool->end] = fileDesc;
    pool->count++;
    pthread_cond_signal(&cond_nonempty);
    pthread_mutex_unlock(&mtx);
}

int obtain(pool_t* pool){
    int data = 0;
    pthread_mutex_lock(&mtx);
    while(pool->count <= 0){
        /* Found buffer empty */
        printf("found buffer empty\n");
        pthread_cond_wait(&cond_nonempty, &mtx);
    }
    //printf("data : %d\n", pool->data[pool->start]);
    data = pool->data[pool->start];
    pool->start = (pool->start + 1) % POOL_SIZE;
    pool->count--;
    pthread_cond_signal(&cond_nonfull);
    pthread_mutex_unlock(&mtx);
    return data;
}

void delete_pool(pool_t* pool){
    pthread_cond_destroy(&cond_nonempty);
    pthread_cond_destroy(&cond_nonfull);
    pthread_mutex_destroy(&mtx);
    free(pool);
}
