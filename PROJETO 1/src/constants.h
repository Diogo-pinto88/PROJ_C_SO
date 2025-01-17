#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <pthread.h>
#include <semaphore.h>
#include <dirent.h>


#define MAX_WRITE_SIZE 256
#define MAX_STRING_SIZE 40
#define MAX_JOB_FILE_NAME_SIZE 256
#define IN_EXT ".job"
#define OUT_EXT ".out"
#define OUT_BCK ".bck"

 typedef struct {
     DIR* dir;
     pthread_mutex_t lock;   
     char* dir_name; 
     unsigned long int MAX_BACKUPS;                           
 } t_args;

void* thread_func(void *args);

#endif


