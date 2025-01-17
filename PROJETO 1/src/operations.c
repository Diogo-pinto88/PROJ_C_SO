#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kvs.h"
#include "constants.h"

static struct HashTable* kvs_table = NULL;
static pthread_rwlock_t *global_lock;
pthread_rwlock_t *hash_lock;



/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}


int kvs_init() {


  if (kvs_table != NULL) {
    fprintf(stderr, "KVS state has already been initialized\n");
    return 1;
  }

  global_lock = malloc(sizeof(pthread_rwlock_t));
  if (global_lock == NULL) {
    fprintf(stderr, "Error allocating memory for lock\n");
    return 1;
  }
  
  pthread_rwlock_init(global_lock, NULL);
  hash_lock = malloc(sizeof(pthread_rwlock_t) * TABLE_SIZE);
  for(int i = 0; i < TABLE_SIZE; i++){
    pthread_rwlock_init(&hash_lock[i], NULL);
  }

  kvs_table = create_hash_table();
  return kvs_table == NULL;
}

int kvs_terminate() {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  for (int i = 0; i < TABLE_SIZE; i++) {
    pthread_rwlock_destroy(&hash_lock[i]);
  }
  free(hash_lock);

  pthread_rwlock_wrlock(global_lock);
  free_table(kvs_table);
  kvs_table = NULL;
  pthread_rwlock_unlock(global_lock);
  pthread_rwlock_destroy(global_lock);
  return 0;
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE]) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }


  for (size_t j = 0; j < num_pairs; j++) {
    int index = hash(keys[j]);
    if (index >= 0 && index < TABLE_SIZE) {
      pthread_rwlock_wrlock(&hash_lock[index]);
      if (write_pair(kvs_table, keys[j], values[j]) != 0) {
        fprintf(stderr, "Failed to write keypair (%s,%s)\n", keys[j], values[j]);
      }
      pthread_rwlock_unlock(&hash_lock[index]);
    } else {
      fprintf(stderr, "Invalid hash index for key: %s\n", keys[j]);
  }
}


  return 0;
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }


  qsort(keys, num_pairs, MAX_STRING_SIZE, (int (*)(const void *, const void *))strcmp);


  write(fd, "[", 1);
  for (size_t j = 0; j < num_pairs; j++) {
    int index = hash(keys[j]);
    if (index >= 0 && index < TABLE_SIZE){
      pthread_rwlock_rdlock(&hash_lock[index]);
      char* result = read_pair(kvs_table, keys[j]);
      pthread_rwlock_unlock(&hash_lock[index]);
      if (result == NULL) {
        write(fd, "(", 1);
        write(fd, keys[j], strlen(keys[j]));
        write(fd, ",", 1);
        write(fd, "KVSERROR)", 9);
      } else {
        write(fd, "(", 1);
        write(fd, keys[j], strlen(keys[j]));
        write(fd, ",", 1);
        write(fd, result, strlen(result));
        write(fd, ")", 1);
      }
      free(result);
      
    } else {
      fprintf(stderr, "Invalid hash index for key: %s\n", keys[j]);
    }
  }
  write(fd, "]\n", 2);

  return 0;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fd) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }
  int aux = 0;


  for (size_t j = 0; j < num_pairs; j++) {
    int index = hash(keys[j]);
    if(index >= 0 && index < TABLE_SIZE){
      pthread_rwlock_wrlock(&hash_lock[index]);
      if (delete_pair(kvs_table, keys[j]) != 0) {
        if (!aux) {
          write(fd, "[", 1);
          aux = 1;
        }
        write(fd, "(", 1);
        write(fd, keys[j], strlen(keys[j]));
        write(fd, ",", 1);
        write(fd, "KVSMISSING)", 11);
      }
      pthread_rwlock_unlock(&hash_lock[index]);
    }  
  }
  if (aux) {
    write(fd, "]\n", 2);
  }
  return 0;
}

void kvs_show(int fd) {

  pthread_rwlock_rdlock(global_lock);
  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = kvs_table->table[i];
    while (keyNode != NULL) {
      write(fd, "(", 1);
      write(fd, keyNode->key, strlen(keyNode->key));
      write(fd, ", ", 2);
      write(fd, keyNode->value, strlen(keyNode->value));
      write(fd, ")\n", 2);
      keyNode = keyNode->next; // Move to the next node
    }
  }
  pthread_rwlock_unlock(global_lock);
}

int kvs_backup(int fd) {

  pthread_rwlock_rdlock(global_lock);
  kvs_show(fd);
  pthread_rwlock_unlock(global_lock);
  return 0;
}

void kvs_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}
