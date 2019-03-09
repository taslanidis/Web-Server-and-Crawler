#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>                                                             /* in_port_t, in_address_t */
#include <sys/socket.h>                                                         /* sockets */
#include <sys/types.h>                                                          /* sockets */
#include <sys/wait.h>                                                           /* sockets */
#include <unistd.h>                                                             /* fork */
#include <netdb.h>                                                              /* gethostbyaddr */
#include <signal.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>                                                            /* threads */
#include <poll.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include "server_tools.h"
#include "sockets.h"

#define MAX_BUFF 512
#define perror2(s,e) fprintf(stderr, "%s: %s\n",s , strerror(e))

int main(int argc, char* argv[]){
    /* reading arguments */
    if(argc != 9){
        printf("Wrong Argument Format!\n");
        exit(1);
    }
    int serving_port, command_port, num_of_threads;
    char* root_dir;
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "-p") == 0){
            serving_port = atoi(argv[++i]);
        }else if(strcmp(argv[i], "-c") == 0){
            command_port = atoi(argv[++i]);
        }else if(strcmp(argv[i], "-t") == 0){
            num_of_threads = atoi(argv[++i]);
        }else if(strcmp(argv[i], "-d") == 0){
            root_dir = malloc((strlen(argv[++i]) + 1)*sizeof(char));
            strcpy(root_dir, argv[i]);
        }else{
            printf("Argument given is not recognized:\t %s\n", argv[i]);
            exit(1);
        }
    }
    /* end */
    /* server requests buffer creation */
    pool_t* pool = initialize_pool(num_of_threads);
    /* end */
    /* shared segment init */
    key_t sem_key;
    struct shared_used_st *shared;
    int shmid = shmget(ftok("./webserver.c", 64), sizeof(struct shared_used_st), 0666 | IPC_CREAT); //creating shared memory
    if (shmid == -1){
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    void *shared_memory = (void *)shmat(shmid, (void *)0, 0);                   //attach shared memory
    if ( shared_memory == (void*)-1){
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    /* end */
    /* thread creation */
    pthread_t thr[num_of_threads];
    int err, status;
    struct argv* arguments = malloc(sizeof(struct argv));
    arguments->tpool = pool;
    arguments->dirname = malloc((strlen(root_dir) + 1)*sizeof(char));
    strcpy(arguments->dirname, root_dir);
    arguments->shared = (struct shared_used_st *)shared_memory;
    arguments->shared->pages = 0;
    arguments->shared->bytes = 0;
    for(int i = 0; i < num_of_threads; i++){
        if (err = pthread_create(&thr[i], NULL, server_threads, (void*)arguments)){
            perror2("pthread_create", err);
            exit(1);
        }
    }
    /* end */
    /* server init */
    struct sockaddr_in client;
    socklen_t clientlen;
    struct sockaddr *clientptr = (struct sockaddr *)&client;
    clientlen = sizeof(*clientptr);
    double starting_time = my_clock();
    /* end */
    /* command socket */
    int command_socket;
    struct sockaddr_in comserver;
    memset(&comserver,0,sizeof(comserver));
    comserver.sin_family = AF_INET;
    comserver.sin_addr.s_addr = INADDR_ANY;                               /* for any address */
    comserver.sin_port = htons(command_port);

    if ((command_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("Socket creation failed!");
        free(root_dir);
        exit(1);
    }

    if (bind(command_socket, (struct sockaddr *) &comserver, sizeof(comserver)) < 0){
        perror("bind");
        free(root_dir);
        exit(1);
    }

    if (listen(command_socket, 5) < 0){
        perror("listen");
        free(root_dir);
        exit(1);
    }

    printf("server is now listening for connections to port %d\n", command_port);
    /* end */
    /* serving socket */
    int service_socket;
    struct sockaddr_in server;
    memset(&server,0,sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(serving_port);

    if ((service_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("Socket creation failed!");
        free(root_dir);
        exit(1);
    }

    if (bind(service_socket, (struct sockaddr *) &server, sizeof(server)) < 0){ /* binding on server */
        perror("bind");
        free(root_dir);
        exit(1);
    }

    if (listen(service_socket, num_of_threads) < 0){                            /* liistening for connections */
        perror("listen");
        free(root_dir);
        exit(1);
    }

    printf("server is now listening for connections to port %d\n", serving_port);

    int pl, cm_socket = 0;
    char* http_request = malloc((MAX_BUFF + 1)*sizeof(char));
    char* format;
    char* command = malloc((MAX_BUFF + 1)*sizeof(char));
    /* end */
    /* communication */
    int N = 2;
    struct pollfd fds[200];                                                     /* max connections */
    int new_socket, incoming_command;
    size_t bytes_read;
    int Server_IO = 1;
    int i = 0;
    fds[0].fd = service_socket;
    fds[1].fd = command_socket;
    fds[0].events = POLLIN;
    fds[1].events = POLLIN;

    while(1){

        incoming_command = 0;
        if((pl = poll(fds, N, 50000)) < 0){                                     /* looking for events at sockets */
            perror("poll");
            break;
        }else if(pl == 0){
            printf("Connection timeout\n");
        }else{
            int bond = N;
            for(int j = 0; j < bond; j++){
                incoming_command = 0;
                if(fds[j].revents & POLLIN){
                    if(fds[j].fd == service_socket){                            /* incoming serving connection */
                        if((new_socket = accept(service_socket, clientptr, &clientlen)) < 0){
                            perror("accept");
                            break;
                        }
                        strcpy(http_request,"");
                        place(pool, new_socket);
                    } else if(fds[j].fd == command_socket){                     /* incoming command connection */
                        if((cm_socket = accept(command_socket, clientptr, &clientlen)) < 0){
                            perror("accept");
                            break;
                        }
                        fds[N].fd = cm_socket;                                  /* adding the new socket to poll struct in order to read from there later */
                        fds[N].events = POLLIN;
                        N++;
                    } else if(fds[j].fd == cm_socket){                          /* reading from the new open connection with command port */
                        strcpy(command,"");
                        if( bytes_read = read(cm_socket, command, MAX_BUFF) <= 0){
                            close(cm_socket);
                            N--;
                        }else{
                            incoming_command = 1;
                        }
                        if(incoming_command == 1){                              /* if it was a valid command and not the exit of connection */
                            command[strlen(command) - 2] = '\0';
                            char* cmd = strtok(command, " \n");
                            if (strcmp(cmd, "SHUTDOWN") == 0){
                                for(int k = 0; k < num_of_threads; k++){
                                    pthread_cancel(thr[k]);
                                }
                                Server_IO = 0;
                                close(cm_socket);
                                break;
                            }else if (strcmp(cmd, "STATS") == 0){
                                double end_time = my_clock();                   /* top timer */
                                double current_time = (end_time - starting_time);/* time running */
                                char* currbuff = malloc(120*sizeof(char));
                                int minutes = current_time / 60;
                                double modulo = current_time - minutes*60;
                                sprintf(currbuff, "Server is up for %02d:%02.4f and served %d pages with %ld bytes\n", minutes, modulo, arguments->shared->pages, arguments->shared->bytes);
                                write(cm_socket, currbuff, strlen(currbuff));
                                free(currbuff);
                            }else{
                                printf("Unknown command\n");
                            }
                        }
                    }
                }
            }
        }
        if(Server_IO == 0) break;
    }
    /* end */
    int rr;
    for(int i = 0; i < num_of_threads; i++){
        rr = pthread_tryjoin_np(thr[i], NULL);
    }
    delete_pool(pool);
    free(root_dir);
    free(command);
    shmdt(arguments->shared);                                                   /* detaching shared memory */
    shmctl(shmid, IPC_RMID, 0);
    free(arguments->dirname);
    free(arguments);
    free(http_request);
    return 0;
}
