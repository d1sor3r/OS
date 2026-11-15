#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

typedef struct Data {
    int id;
    char payload[64];
} Data;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static int ready = 0;
static Data *shared = NULL;

static void *provider(void *arg)
{
    (void)arg;
    for (int i = 0; ; ++i){
        sleep(1);

        Data *d = malloc(sizeof *d);
        if (!d) { perror("malloc"); exit(1); }
        d->id = i;
        snprintf(d->payload, sizeof d->payload, "event-%d", i);

        pthread_mutex_lock(&lock);
        while (ready) pthread_cond_wait(&cond, &lock);

        shared = d;
        ready = 1;
        printf("provided: %s\n", d->payload);
        fflush(stdout);

        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static void *consumer(void *arg){
    (void)arg;
    for (;;){
        pthread_mutex_lock(&lock);
        while (!ready) pthread_cond_wait(&cond, &lock);

        Data *d = shared;
        shared = NULL;
        ready = 0;
        printf("consumed: %s\n", d->payload);
        fflush(stdout);

        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&lock);

        free(d);
    }
    return NULL;
}

int main(void)
{
    pthread_t tp = 0, tc = 0;

    if (pthread_create(&tp, NULL, consumer, NULL) != 0) { perror("create consumer"); return 1;}
    if (pthread_create(&tc, NULL, provider, NULL) != 0) { perror("create provider"); return 1;}

    pthread_join(tp, NULL);
    pthread_join(tc, NULL);
    return 0;
}