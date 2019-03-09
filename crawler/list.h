struct listHead{
    int links;
    struct list* first;
};

struct list{
    char* link;
    struct list* next;
};

struct argv{
    struct listHead* head;
    struct sockaddr_in* server;
    struct shared_used_st *shared;
    char* dirname;
};

struct shared_used_st{
    int pages;
    long int bytes;
};

void* client_threads(void* );

struct listHead* ListInit();

int ListInsert(struct listHead* , char* );

char* obtain(struct listHead* );

void ListDelete(struct listHead* );

int Crawling(struct listHead* );
