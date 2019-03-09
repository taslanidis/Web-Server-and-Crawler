#include <sys/time.h>
#include <pthread.h>                                                            /* threads */

typedef struct {
    int *data;
    int start;
    int end;
    int count;
} pool_t;

struct argv{
    pool_t* tpool;
    struct shared_used_st *shared;
    char* dirname;
};

struct shared_used_st{
    int pages;
    long int bytes;
};

pool_t* initialize_pool(int );

void place(pool_t* , int );

int obtain(pool_t* );

void delete_pool(pool_t*);

double my_clock(void);

void *server_threads(void*);
