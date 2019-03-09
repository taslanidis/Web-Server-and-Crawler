#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>                                                             /* in_port_t, in_address_t */
#include <sys/socket.h>                                                         /* sockets */
#include <sys/types.h>                                                          /* sockets */
#include <sys/wait.h>
#include <sys/stat.h>                                                           /* folders */
#include <unistd.h>                                                             /* fork */
#include <netdb.h>                                                              /* gethostbyaddr */
#include <signal.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>                                                            /* threads */
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "list.h"
#include "Trie.h"

#define MAX_BUFF 512
#define perror2(s,e) fprintf(stderr, "%s: %s\n",s , strerror(e))
#define READ 0
#define WRITE 1

typedef struct listHead LinksHead;
typedef struct list Links;

int main(int argc, char* argv[]){
    /* reading arguments */
    int numWorkers = 2;
    if(argc != 12){
        printf("Wrong Argument Format!\n");
        exit(1);
    }
    int serving_port, command_port, num_of_threads;
    char* save_dir;
    char* starting_url;
    struct hostent* host_IP;
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i], "-p") == 0){
            serving_port = atoi(argv[++i]);
        }else if(strcmp(argv[i], "-c") == 0){
            command_port = atoi(argv[++i]);
        }else if(strcmp(argv[i], "-t") == 0){
            num_of_threads = atoi(argv[++i]);
        }else if(strcmp(argv[i], "-d") == 0){
            save_dir = malloc((strlen(argv[++i]) + 1)*sizeof(char));
            strcpy(save_dir, argv[i]);
            starting_url = malloc((strlen(argv[++i]) + 1)*sizeof(char));
            strcpy(starting_url, argv[i]);
        }else if(strcmp(argv[i], "-h") == 0){
            host_IP = gethostbyname(argv[++i]);                                 /* in order to work for either host name or IP */
        }else{
            printf("Argument given is not recognized:\t %s\n", argv[i]);
            exit(1);
        }
    }
    /* end */
    /* client init */
    int crawling = 1;
    struct sockaddr_in client;
    socklen_t clientlen;
    struct sockaddr *clientptr = (struct sockaddr *)&client;
    clientlen = sizeof(*clientptr);
    double starting_time = my_clock();
    LinksHead* lhead = ListInit();
    ListInsert(lhead, starting_url);
    struct stat sb;
    if ((stat(save_dir, &sb) == 0) && S_ISDIR(sb.st_mode)){                     /* checking for save dir */
        char* query = malloc(100*sizeof(char));
        strcpy(query, "rm -r ");
        strcat(query, save_dir);
        system(query);                                                          /* purging save dir */
        free(query);
    }
    if(mkdir(save_dir, 0777) == -1){
        perror("mkdir");
    }else{
        chmod(save_dir, 0777);
    }
    /* end */
    /* server variables init */
    struct sockaddr_in server;
    memset(&server,0,sizeof(server));
    server.sin_family = AF_INET;
    memcpy(&server.sin_addr.s_addr, host_IP->h_addr, host_IP->h_length);
    server.sin_port = htons(serving_port);
    /* end */
    /* shared memory init */
    key_t sem_key;
    int shmid = shmget(ftok("./client.c", 30), sizeof(struct shared_used_st), 0666 | IPC_CREAT); //creating shared memory
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
    arguments->head = lhead;
    arguments->server = &server;
    arguments->dirname = malloc((strlen(save_dir) + 1)*sizeof(char));
    strcpy(arguments->dirname, save_dir);
    arguments->shared = (struct shared_used_st *)shared_memory;                 //shared memory on arguments
    arguments->shared->pages = 0;
    arguments->shared->bytes = 0;
    for(int i = 0; i < num_of_threads; i++){
        if (err = pthread_create(&thr[i], NULL, client_threads, (void*)arguments)){
            perror2("pthread_create", err);
            exit(1);
        }
    }
    /* end */
    /* command socket connect */
    int command_socket, cm_socket;
    struct sockaddr_in comserver;
    memset(&comserver,0,sizeof(comserver));
    comserver.sin_family = AF_INET;
    memcpy(&comserver.sin_addr.s_addr, host_IP->h_addr, host_IP->h_length);
    comserver.sin_port = htons(command_port);

    if ((command_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("Socket creation failed!");
        free(save_dir);
        free(starting_url);
        exit(1);
    }

    if (bind(command_socket, (struct sockaddr *) &comserver, sizeof(comserver)) < 0){
        perror("bind");
        free(save_dir);
        free(starting_url);
        exit(1);
    }

    if (listen(command_socket, 2) < 0){
        perror("listen");
        free(save_dir);
        free(starting_url);
        exit(1);
    }

    int Crawler_IO = 1;
    struct pollfd fds[20];                                                      /* max connections */
    fds[0].fd = command_socket;
    fds[0].events = POLLIN;
    int pl, N = 1;
    int Socket_command = 0;
    /* end */
    /* communication */
    size_t bytes_read = 0;
    /* variables for search */
    int Did_I_Search = 0;                                                       /* variable to know whether search has ran ( workers are running) */
    pid_t parent_pid;
    pid_t Workers[numWorkers];
    int fd[numWorkers][2];                                                      //2 = 1 for read and 1 for write
    char* command = malloc((MAX_BUFF + 1)*sizeof(char));
    char** commands = malloc(11*sizeof(char*));
    char* pipe_name_in = (char*)malloc(5*sizeof(pid_t) + 3*sizeof(char));
    char* pipe_name_out = (char*)malloc(5*sizeof(pid_t) + 4*sizeof(char));
    /* end of variables */

    while(1){
        Socket_command = 0;
        if((pl = poll(fds, N, 50000)) < 0){
            perror("poll");
            break;
        }else if(pl == 0){
            printf("Connection timeout\n");
        }else{
            int bond = N;
            for(int j = 0; j < bond; j++){
                if(fds[j].revents & POLLIN){
                    if(fds[j].fd == command_socket){
                        if((cm_socket = accept(command_socket, clientptr, &clientlen)) < 0){
                            perror("accept");
                            free(save_dir);
                            free(starting_url);
                            exit(1);
                        }
                        fds[N].fd = cm_socket;
                        fds[N].events = POLLIN;
                        N++;
                    }else{
                        strcpy(command,"");
                        if( bytes_read = read(cm_socket, command, MAX_BUFF) <= 0){
                            close(cm_socket);
                            N--;
                        }else{
                            Socket_command = 1;
                        }
                    }
                }
            }
        }

        if(Socket_command == 1){
            command[strlen(command) - 2] = ' ';
            command[strlen(command) - 1] = '\0';
            commands[0] = strtok(command, " \n");
            if (strcmp(commands[0], "SHUTDOWN") == 0){
                for(int k = 0; k < num_of_threads; k++){
                    pthread_cancel(thr[k]);
                }
                if(Did_I_Search == 1){
                    for(int i = 0; i < numWorkers; i++){
                        write(fd[i][WRITE], "/exit", MAX_BUFF);
                        waitpid(Workers[i], &status, WUNTRACED);
                        kill(Workers[i], SIGCONT);
                    }
                }
                Crawler_IO = 0;
                close(cm_socket);
                break;
            }else if (strcmp(commands[0], "STATS") == 0){
                double end_time = my_clock();                                   /* top timer */
                double current_time = (end_time - starting_time);               /* time running */
                char* currbuff = malloc(120*sizeof(char));
                int minutes = current_time / 60;
                double modulo = current_time - minutes*60;
                sprintf(currbuff, "Crawler is up for %02d:%02.4f downloaded %d pages with %ld bytes\n", minutes, modulo, arguments->shared->pages, arguments->shared->bytes);
                write(cm_socket, currbuff, strlen(currbuff));
                free(currbuff);
            }else if (strcmp(commands[0], "SEARCH") == 0){
                if (Crawling(lhead) == 1){
                    char* tt = malloc(200*sizeof(char));
                    strcpy(tt,"Crawling is still in progress! Search command can not be executed right now. \n");
                    write(cm_socket,tt,strlen(tt));
                    free(tt);
                }else{
                    printf("Executing search on workers\n");
                    /* Job Executor */
                    /* find the number of catalogs */
                    if(Did_I_Search == 0){
                        char* buffer = malloc((MAX_BUFF + 1)*sizeof(char));
                        int num = 0;
                        int catalogs = 0;
                        DIR *directory;
                        struct dirent *ent;
                        struct stat sb;
                        if ((stat(save_dir, &sb) == 0) && S_ISDIR(sb.st_mode)){
                            //printf("dir : %s\n", save_dir);
                        }
                        if((directory = opendir(save_dir)) != NULL){
                            while((ent = readdir(directory)) != NULL){                        //searching directory for number of files
                                if((strcmp(ent->d_name, ".") != 0) && (strcmp(ent->d_name, "..") != 0)) catalogs++;
                            }
                        }else{
                            perror("opendir");
                        }
                        rewinddir(directory);
                        char **DirNames = malloc(catalogs*sizeof(char**));
                        int count = 0;
                        while((ent = readdir(directory)) != NULL){
                            if((strcmp(ent->d_name, ".") != 0) && (strcmp(ent->d_name, "..") != 0)){
                                strcpy(buffer, ent->d_name);
                                DirNames[count] = malloc((strlen(buffer) + strlen(save_dir) + 4)*sizeof(char));
                                strcpy(DirNames[count], "./");
                                strcat(DirNames[count], save_dir);
                                strcat(DirNames[count], "/");
                                strcat(DirNames[count], buffer);
                                count++;
                            }
                        }
                        closedir(directory);
                        /* end */
                        if(catalogs < numWorkers){
                            printf("Workers were more than the catalogs so they were reduced to %d\n", catalogs);
                            numWorkers = catalogs;
                        }
                        pid_t pid;
                        parent_pid = getpid();
                        int fd_Worker[2];
                        int status = 0;
                        for(int i = 0; i < numWorkers; i++){
                            switch(pid = fork()){                                                   //simultaneously child and parent create pipes for reading/writing
                                case -1:    perror("fork");
                                case  0:    kill(getpid(), SIGSTOP);                                //if child wait for parent to create the pipe
                                            sprintf(pipe_name_in, "%d", getpid());
                                            sprintf(pipe_name_out, "%d", getpid());
                                            strcat(pipe_name_in, "_in");
                                            strcat(pipe_name_out, "_out");
                                            fd_Worker[WRITE] = open(pipe_name_out, O_WRONLY);       //only for writing
                                            fd_Worker[READ] = open(pipe_name_in, O_RDONLY | O_NONBLOCK);//only for reading, it is NONBLOCK because I know when to terminate my input
                                            break;
                                default:    Workers[i] = pid;                                       //if parent store the pid
                                            sprintf(pipe_name_in, "%d", pid);
                                            sprintf(pipe_name_out, "%d", pid);
                                            strcat(pipe_name_in, "_in");
                                            strcat(pipe_name_out, "_out");
                                            mkfifo(pipe_name_in, 0666);
                                            mkfifo(pipe_name_out, 0666);
                                            waitpid(pid, &status, WUNTRACED);                       //wait for child to be created and stopped
                                            kill(pid, SIGCONT);                                     //when child stopped, make sure to restart it
                                            fd[i][READ] = open(pipe_name_out, O_RDONLY);            //only for reading
                                            fd[i][WRITE] = open(pipe_name_in, O_WRONLY);            //only for writing
                            }
                            if(pid == 0)    break;
                        }

                        srand(time(NULL));                                                          //initializing time
                        int num_of_bytes = 0;
                        int num_of_words = 0;
                        int num_of_lines = 0;
                        int num_of_files = 0;
                        int num_of_catalogs = catalogs/numWorkers;                                  //number of catalogs for every worker (maybe + 1 if remaining catalogs > 0)
                        int remaining_catalogs = catalogs%numWorkers;                               //static
                        int temp_remaining_catalogs = remaining_catalogs;                           //changing for the iterations
                        int* doclines;                                                              //necessary for search and wc ( I keep number of lines of every file)
                        // end of initialization //
                        // passing the names of folders and intialiazing the tries for every worker //
                        if(getpid() == parent_pid){
                            count = 0;
                            for(int i = 0; i < numWorkers; i++){
                                waitpid(Workers[i], &status, WUNTRACED);
                                for(int j = 0; j < (num_of_catalogs + (temp_remaining_catalogs != 0)); j++){//Iteration where temp_remaining_catalogs is changing dynamically, so every worker
                                    write(fd[i][WRITE], DirNames[count], MAX_BUFF);
                                    kill(Workers[i], SIGCONT);                                      //continue child now that parent wrote in pipe
                                    waitpid(Workers[i], &status, WUNTRACED);                        //parent waits for child to read what is written in pipe
                                    count++;
                                }
                                if(temp_remaining_catalogs > 0)  temp_remaining_catalogs--;
                                kill(Workers[i], SIGCONT);
                            }
                        }else{                                                                      //worker part
                            num = 0;
                            kill(getpid(),SIGSTOP);                                               //wait for parent to resume child and go read from pipe
                            int bytesread;
                            char** catalog = malloc((num_of_catalogs + (remaining_catalogs != 0))*sizeof(char*));
                            while((bytesread = read(fd_Worker[READ], buffer, MAX_BUFF)) > 0){
                                catalog[num] = malloc((bytesread + 1)*sizeof(char));
                                strcpy(catalog[num], buffer);
                                num++;
                                kill(getpid(), SIGSTOP);
                            }
                            if((num == num_of_catalogs) && (remaining_catalogs > 0)){               //I declare an array with an extra more space for all workers
                                catalog[num] = NULL;                                                //ex. 4 workers with 5 catalogs, first worker takes 2files and every other gets one
                            }                                                                       //catalog array is 2 for all of them because they do not know that from the begining

                            char ch;
                            int docs[num];
                            for(int i = 0; i < num; i++){
                                docs[i] = 0;
                            }
                            int count = 0;

                            DIR *dir;
                            struct dirent *ent;
                            char ***FileNames = malloc(num*sizeof(char**));         //array for storing all names of every file inside every catalog
                            for(int i = 0; i < num; i++){
                                if(catalog[i] != NULL){
                                    if((dir = opendir(catalog[i])) != NULL){
                                        while((ent = readdir(dir)) != NULL){        //searching directory for number of files
                                            if((strcmp(ent->d_name, ".") != 0) && (strcmp(ent->d_name, "..") != 0)) docs[i]++;
                                        }
                                    }else{
                                        perror("opendir");
                                        return -1;
                                    }
                                    rewinddir(dir);                                 //rewinding directory to search it again, now for names
                                    FileNames[i] = malloc(docs[i]*sizeof(char*));
                                    count = 0;
                                    while((ent = readdir(dir)) != NULL){
                                        if((strcmp(ent->d_name, ".") != 0) && (strcmp(ent->d_name, "..") != 0)){
                                            strcpy(buffer, ent->d_name);
                                            FileNames[i][count] = malloc((strlen(buffer) + 1)*sizeof(char));
                                            strcpy(FileNames[i][count], buffer);
                                            count++;
                                        }
                                    }
                                    closedir(dir);
                                }
                            }

                            char* filename;
                            CreateTrie();                                       //Trie init

                            int j = 0;
                            int html_tag = 0;
                            num_of_files = 0;
                            for(int i = 0; i < num; i++){
                                num_of_files += docs[i];
                            }
                            doclines = malloc(num_of_files*sizeof(int));
                            num_of_files = 0;
                            for(int i = 0; i < num; i++){
                                for(int k = 0; k < docs[i]; k++){
                                    if(catalog[i] != NULL){
                                        filename = malloc((strlen(FileNames[i][k]) + strlen(catalog[i]) + 4)*sizeof(char));
                                        strcpy(filename, catalog[i]);
                                        strcat(filename, "/");
                                        strcat(filename, FileNames[i][k]);          //making the full relative path for file to fopen
                                        FILE* file = fopen(filename, "r");
                                        if(file == NULL){
                                            perror("fopen");
                                            free(filename);
                                            continue;
                                        }
                                        num_of_files++;
                                        html_tag = 0;

                                        while(1){                                   //read all file character by character and insert into map
                                           ch = fgetc(file);
                                           if(feof(file)){
                                               break;
                                           }else{
                                               num_of_bytes++;
                                               if(ch == '\n'){
                                                   Insert(' ', j, num_of_files - 1, filename);//insert space to change the word (pointer in insert)
                                                   j++;
                                                   num_of_lines++;
                                               }else{
                                                   if(ch == '<'){                   // Discarding html tags with the flag
                                                       html_tag++;
                                                   }else if(ch == '>'){
                                                       html_tag--;
                                                   }

                                                   if(html_tag == 0){
                                                       if(CheckValidity(ch) == 0)   num_of_words++;
                                                       Insert(ch, j, num_of_files - 1, filename);
                                                   }
                                               }
                                           }
                                       }
                                       fclose(file);
                                       free(filename);
                                   }
                                   doclines[num_of_files - 1] = j;                                  //array where storing how many lines there are on each file
                                   j = 0;
                               }
                           }

                           /*char* word = malloc((MAX_BUFF + 1)*sizeof(char));
                           char* pidd = malloc(40*sizeof(char));
                           sprintf(pidd, "%d", getpid());
                           strcat(pidd,".txt");
                           FILE *fp3 = fopen(pidd, "w+");
                           df(GetRoot(), word, 0, fp3);
                           fclose(fp3);
                           free(word);
                           free(pidd);*/
                           for(int k = 0; k < num; k++){
                               for(int l = 0; l < docs[k]; l++){
                                   free(FileNames[k][l]);
                               }
                               free(FileNames[k]);
                           }
                           free(FileNames);
                           for(int k = 0; k < num_of_catalogs + (remaining_catalogs != 0); k++){
                               if(catalog != NULL)
                                   free(catalog[k]);
                           }
                           free(catalog);
                        }
                        /* structures are ready for search query */

                        /* worker part */
                        if(getpid() != parent_pid){
                            int stop = 0;
                            while(stop == 0){
                                kill(getpid(),SIGSTOP);
                                int bytesread;
                                char** Words = malloc(10*sizeof(char*));                            //array for worker words
                                while((bytesread = read(fd_Worker[READ], buffer, MAX_BUFF) > 0)){   //job executor has cheked for correct format
                                    if (strcmp(buffer, "/exit") == 0){
                                        free(Words);
                                        stop = 1;
                                        break;
                                    }
                                    int flag = 1;
                                    int** Sent = malloc(num_of_files*sizeof(int*));                 //Array for evey file and every line in order to flag those who I want to print
                                    char** FullPath = malloc(num_of_files*sizeof(char*));           //Array for storing full paths to those files that some lines needs to be printed
                                    for(int i = 0; i < num_of_files; i++){
                                        FullPath[i] = NULL;
                                    }
                                    for(int i = 0; i < num_of_files; i++){                          //Initializing array(sent = to know what lines are to be printed and whether a line is already flaged)
                                        Sent[i] = malloc(doclines[i]*sizeof(int));
                                        for(int k = 0; k < doclines[i]; k++){
                                            Sent[i][k] = 0;
                                        }
                                    }
                                    Words[0] = strtok(buffer, " \n");
                                    for(int i = 1; i < 10; i++){                                    //Initializing words
                                        Words[i] = strtok(NULL, " \n");
                                    }

                                    int i = 0;
                                    while((Words[i] != NULL) && (i < 10)){
                                        PListNode* PLNode = dfSingle(Words[i], 0);                  //return the posting list of the word searched
                                        i++;
                                        while(PLNode != NULL){                                      //if it exists and not null, worker will flag all the positions in sent array to know who to print
                                            if(Sent[PLNode->File_id][PLNode->Doc_id] == 0){
                                                Sent[PLNode->File_id][PLNode->Doc_id] = 1;          //flaggin the line on that file
                                                if(FullPath[PLNode->File_id] == NULL)
                                                    FullPath[PLNode->File_id] = malloc((strlen(PLNode->DocName) + 1)*sizeof(char));
                                                strcpy(FullPath[PLNode->File_id], PLNode->DocName);
                                            }
                                            PLNode = PLNode->Next;
                                        }
                                    }

                                    char* tempbuff = NULL;
                                    size_t n;
                                    size_t ArrSize = 1;
                                    char* buff = malloc((MAX_BUFF + 1)*sizeof(char));
                                    char** Answers = malloc(ArrSize*sizeof(char*));                 //buffer with all the answers to job executor
                                    Answers[0] = malloc((MAX_BUFF + 1)*sizeof(char));                      //starting from a small size and then reallocing to a bigger size if the output is big
                                    strcpy(Answers[0], "");
                                    int line = 0;
                                    char* message1 = malloc(20*sizeof(char));                       //default messages for the answers on job executor
                                    strcpy(message1,"Path: ");
                                    char* message2 = malloc(20*sizeof(char));
                                    strcpy(message2,"#Line: ");
                                    char* message3 = malloc(20*sizeof(char));
                                    strcpy(message3,"Contents: ");
                                    flag = 0;
                                    for(int i = 0; i < num_of_files; i++){                          //for every file open it and check for every line if it is flagged by Sent array
                                        if(FullPath[i] != NULL){
                                            FILE* file = fopen(FullPath[i], "r");
                                            if(file == NULL){
                                                perror("fopen");
                                                continue;
                                            }
                                            line = 0;
                                            char* sline = malloc(40*sizeof(char));
                                            while(((bytesread = getline(&tempbuff, &n, file)) > 1)){
                                                if(Sent[i][line] == 1){                             //if it is flagged I include it in the answer.
                                                    flag = 1;
                                                    if((strlen(FullPath[i]) + strlen(message1) + 1) < MAX_BUFF - strlen(Answers[ArrSize - 1])){//if path doesnt fit
                                                        strcat(Answers[ArrSize - 1], message1);
                                                        strcat(Answers[ArrSize - 1], FullPath[i]);
                                                        strcat(Answers[ArrSize - 1], "\n");
                                                    }else{
                                                        Answers = realloc(Answers, (++ArrSize)*sizeof(char*));
                                                        Answers[ArrSize - 1] = malloc((MAX_BUFF + 1)*sizeof(char));
                                                        memset(Answers[ArrSize - 1],0,strlen(Answers[ArrSize - 1]));
                                                        strcpy(Answers[ArrSize - 1], message1);
                                                        strcat(Answers[ArrSize - 1], FullPath[i]);
                                                        strcat(Answers[ArrSize - 1], "\n");
                                                    }
                                                    sprintf(sline , "%d", line);
                                                    if((strlen(sline) + strlen(message2) + 1) < MAX_BUFF - strlen(Answers[ArrSize - 1])){//if line number doesnt fit
                                                        strcat(Answers[ArrSize - 1], message2);
                                                        strcat(Answers[ArrSize - 1], sline);
                                                        strcat(Answers[ArrSize - 1], "\n");
                                                    }else{
                                                        Answers = realloc(Answers, (++ArrSize)*sizeof(char*));
                                                        Answers[ArrSize - 1] = malloc((MAX_BUFF + 1)*sizeof(char));
                                                        memset(Answers[ArrSize - 1],0,strlen(Answers[ArrSize - 1]));
                                                        strcpy(Answers[ArrSize - 1], message2);
                                                        strcat(Answers[ArrSize - 1], sline);
                                                        strcat(Answers[ArrSize - 1], "\n");
                                                    }
                                                    if((strlen(tempbuff) + strlen(message3)) < MAX_BUFF - strlen(Answers[ArrSize - 1])){//if buffer with line doesnt fit
                                                        strcat(Answers[ArrSize - 1], message3);
                                                        strcat(Answers[ArrSize - 1], tempbuff);
                                                    }else{
                                                        Answers = realloc(Answers, (++ArrSize)*sizeof(char*));
                                                        Answers[ArrSize - 1] = malloc((MAX_BUFF + 1)*sizeof(char));
                                                        memset(Answers[ArrSize - 1],0,strlen(Answers[ArrSize - 1]));
                                                        if((strlen(tempbuff) + strlen(message3)) < MAX_BUFF){
                                                            strcpy(Answers[ArrSize - 1], message3);
                                                            strcat(Answers[ArrSize - 1], tempbuff);
                                                        }else{
                                                            strcpy(Answers[ArrSize - 1], message3);
                                                            strcat(Answers[ArrSize - 1], "Error! Line was too long for a preview.\n");
                                                        }
                                                    }
                                                }
                                                line++;
                                                //tempbuff = NULL;
                                            }
                                            free(sline);
                                        }
                                    }
                                    if(flag == 0){
                                        strcpy(Answers[ArrSize - 1], "Not found\n");
                                    }
                                    for(int k = 0; k < ArrSize; k++){
                                        write(fd_Worker[WRITE], Answers[k], MAX_BUFF);               //then all the answers
                                        free(Answers[k]);
                                        kill(getpid(), SIGSTOP);
                                    }
                                    write(fd_Worker[WRITE], "/eow", MAX_BUFF);                       //send eow to parent to know that the answer is over
                                    free(Answers);
                                    free(Words);
                                    for(int k = 0; k < num_of_files; k++){
                                        free(Sent[k]);
                                        free(FullPath[k]);
                                    }
                                    free(Sent);
                                    free(FullPath);
                                    free(message1);
                                    free(message2);
                                    free(message3);
                                    free(buff);
                                }
                            }
                            /* end of worker */
                            /* I now exit worker since a SHUTDOWN came from commands */
                            TrieDelete(GetRoot());
                            close(fd_Worker[READ]);
                            close(fd_Worker[WRITE]);
                            free(pipe_name_in);
                            free(pipe_name_out);
                            printf("Exiting %u\n", getpid());
                            exit(1);
                        }
                    }

                    /* Job executor part */
                    if(getpid() == parent_pid){
                        Did_I_Search = 1;
                        int bytes_wrote = 0;
                        char* Query = malloc((MAX_BUFF + 1)*sizeof(char));
                        commands[1] = strtok(NULL, " \n");
                        char* result = malloc((MAX_BUFF + 1)*sizeof(char));
                        if(commands[1] == NULL){
                            printf("Unknown command! [You have to type queries also.]\n");
                            for(int i = 0; i < numWorkers; i++){
                                waitpid(Workers[i], &status, WUNTRACED);
                                kill(Workers[i], SIGCONT);
                            }
                        }else{
                            int i;
                            for(i = 2; i <= 10; i++){
                                commands[i] = strtok(NULL, " \n");
                                if(commands[i] == NULL) break;
                            }

                            i = 1;
                            strcpy(Query,"");
                            while((commands[i] != NULL) && (i <= 10)){             //reconstructing the query with the first arguments
                                if((strlen(Query) + strlen(commands[i])) < MAX_BUFF){
                                    strcat(Query, commands[i]);
                                    strcat(Query, " ");
                                }
                                i++;
                            }
                            for(i = 0; i < numWorkers; i++){
                                write(fd[i][WRITE], Query, MAX_BUFF);
                                waitpid(Workers[i], &status, WUNTRACED);
                                kill(Workers[i], SIGCONT);
                            }
                            char* FullPath;
                            int num_of_answers = numWorkers;                        //keeping track of how many workers answered
                            size_t bytesread = 0;
                            size_t temp;
                            char** Answers = malloc(numWorkers*sizeof(char*));
                            for(int i = 0; i < numWorkers; i++){
                                temp = read(fd[i][READ], result, MAX_BUFF);
                                Answers[i] = malloc(0);                             //initialize o null pointer in order for realloc to work
                                while(strcmp(result, "/eow") != 0){                 //waiting for eow = end of worker to stop reading from his pipe
                                    bytesread += temp;                              //I dont double it because since the pipe is very big not many reallocs will be needed
                                    Answers[i] = realloc(Answers[i],(bytesread * sizeof(char)));//if the answer is exceeding the max size I realloc it to the new size
                                    if(bytesread == temp)   memset(Answers[i],0,strlen(Answers[i]));  //first realloc = malloc
                                    strcat(Answers[i], result);
                                    waitpid(Workers[i], &status, WUNTRACED);
                                    kill(Workers[i], SIGCONT);
                                    memset(result,0,strlen(result));
                                    temp = read(fd[i][READ], result, MAX_BUFF);
                                }
                                bytesread = 0;
                            }
                            for(int i = 0; i < numWorkers; i++){
                                //write in socket
                                char* header = malloc(40*sizeof(char));
                                sprintf(header, "\nResults by %d:\n",Workers[i]);
                                bytes_wrote = write(cm_socket, header, strlen(header));
                                if(bytes_wrote < strlen(header)){
                                    bytes_wrote = write(cm_socket, header + bytes_wrote, strlen(header) - bytes_wrote);
                                }
                                int total = 0;
                                while(total < strlen(Answers[i])){
                                    if(strlen(Answers[i] + total) >= MAX_BUFF){
                                        bytes_wrote = write(cm_socket, Answers[i] + total, MAX_BUFF);
                                        if(bytes_wrote < MAX_BUFF){
                                            bytes_wrote = write(cm_socket, Answers[i] + total + bytes_wrote, MAX_BUFF - bytes_wrote);
                                        }
                                    }else{
                                        bytes_wrote = write(cm_socket, Answers[i] + total, strlen(Answers[i] + total));
                                        if(bytes_wrote < strlen(Answers[i] + total)){
                                            bytes_wrote = write(cm_socket, Answers[i] + total + bytes_wrote, strlen(Answers[i] + total) - bytes_wrote);
                                        }
                                    }
                                    total += MAX_BUFF;
                                }

                                strcpy(Answers[i],"");
                                free(Answers[i]);
                                free(header);
                            }
                            free(Answers);
                        }
                        for(int i = 0; i <= 10; i++){
                            commands[i] = NULL;
                        }
                        strcpy(Query,"");
                        free(result);
                        free(Query);
                    }
                }
            }else{
                printf("Unknown command\n");
            }
        }
    }

    /* ending workers */
    if(Did_I_Search == 1){
        for(int k = 0; k < numWorkers; k++){
            waitpid(Workers[k], &status, WUNTRACED);
            sprintf(pipe_name_in, "%d", Workers[k]);
            strcat(pipe_name_in,"_in");
            if(unlink(pipe_name_in) < 0)    perror("unlink");
            sprintf(pipe_name_out, "%d", Workers[k]);
            strcat(pipe_name_out,"_out");
            if(unlink(pipe_name_out) < 0)    perror("unlink");
            close(fd[k][READ]);
            close(fd[k][WRITE]);
        }
    }
    free(pipe_name_in);
    free(pipe_name_out);
    /* end */

    free(command);
    free(commands);
    int rr;
    for(int i = 0; i < num_of_threads; i++){
        rr = pthread_tryjoin_np(thr[i], NULL);
    }
    ListDelete(lhead);
    shmdt(arguments->shared);
    shmctl(shmid, IPC_RMID, 0);
    free(arguments->dirname);
    free(arguments);
    return 0;
}
