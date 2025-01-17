#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "constants.h"
#include "parser.h"
#include "operations.h"

int main(int argc, char *argv[]) {

  DIR *dir;
  unsigned long int MAX_THREADS, MAX_BACKUPS, i; 
  pthread_t *tid = NULL;
  t_args* args = NULL;

  
  if(argc < 4) {
		fprintf(stderr, "Not enough arguments!\n"
    "Correct invocation example:\n"
    "./kvs DIR_PATH MAX_BACKUPS MAX_THREADS\n"
    "./kvs DIR_PATH MAX_BACKUPS MAX_THREADS DELAY\n");
		exit(1);
	}

  MAX_BACKUPS = strtoul(argv[2], NULL, 10);
  MAX_THREADS = strtoul(argv[3], NULL, 10);

  if(MAX_THREADS > UINT_MAX || MAX_BACKUPS > UINT_MAX) {
    fprintf(stderr, "Invalid max value for threads or backups\n");
    exit(1);
  }

  if (MAX_BACKUPS <= 0) {
		write_str(STDERR_FILENO, "Invalid number of backups\n");
		return 0;
	}

	if (MAX_THREADS <= 0) {
		write_str(STDERR_FILENO, "Invalid number of threads\n");
		return 0;
	}

  if(argc > 4) {
    
    char *endptr;
    unsigned long int delay = strtoul(argv[4], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
			fprintf(stderr, "Invalid delay value or value too large\n");
			exit(1);
		}

  }

  dir = opendir(argv[1]);
  if(dir == NULL) {
    fprintf(stderr, "opendir failed on '%s'.", argv[1]);
    exit(1);
  }

  tid = malloc(sizeof(pthread_t) * MAX_THREADS);
  if(tid == NULL){
    fprintf(stderr, "malloc failed for tid!\n");
    exit(1);
  }

  args = malloc(sizeof(t_args));
  if(args == NULL){
    fprintf(stderr, "malloc failed for args!\n");
    exit(1);
  }
  pthread_mutex_init(&(args->lock), NULL);
  args->dir = dir;
  args->dir_name = argv[1];

  for(i = 0; i < MAX_THREADS; i++){
    pthread_create(&(tid[i]), 0, thread_func, (void*) args);
  }

  for(i = 0; i < MAX_THREADS; i++){
    pthread_join(tid[i], NULL);
  }
  free(args);
  free(tid);

closedir(dir);
exit(0);
}

void* thread_func(void* args){

  t_args *t = (t_args*) args;
  DIR *d = t->dir;
  pthread_mutex_t *m = &(t->lock);
  struct dirent *dp;
  char *in_path, *out_path, *bck_path, final[MAX_STRING_SIZE] = "";
  mode_t write_perms = 00400 | 00200 | 00040 | 00020 | 00004;
  size_t name_size;
  int flag = 1, status = 1;
  unsigned long int MAX_BACKUPS = t->MAX_BACKUPS, i, NUM_BACKUPS_EXEC = 0;
  pid_t pid_temp = -1, pid2= -1;
  

  for(;;){
      pthread_mutex_lock(m);
      dp = readdir(d);
      pthread_mutex_unlock(m);
      if(dp == NULL){
        break;
      }
      if(strchr(dp->d_name, '.') == NULL || strcmp(strchr(dp->d_name, '.'), IN_EXT) != 0){
        continue;
      }
      in_path = malloc(sizeof(char) * (strlen(t->dir_name) + strlen(dp->d_name) + 2));
      if(in_path == NULL){
        fprintf(stderr, "Malloc failed for in_path\n");
        exit(1);
      }
      strcpy(in_path, t->dir_name);
      strcat(in_path, "/");
      strcat(in_path, dp->d_name);
      int in_fd = open(in_path, O_RDONLY);
      free(in_path);

      name_size = strlen(dp->d_name) - (strlen(IN_EXT));
      out_path = malloc(sizeof(char) * (strlen(t->dir_name) + name_size + strlen(OUT_EXT) + 2));
        if(out_path == NULL){
          fprintf(stderr, "Malloc failed for out_path\n");
          exit(1);
      }
      strcpy(out_path, t->dir_name);
      strcat(out_path, "/");
      strncat(out_path, dp->d_name, name_size);
      strcat(out_path, OUT_EXT);
      int out_fd = open(out_path, O_CREAT | O_WRONLY | O_TRUNC, write_perms);
      free(out_path);

      if (kvs_init()) {
        fprintf(stderr, "Failed to initialize KVS\n");
        exit(1);
      }

      while (flag) {
        char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
        char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
        unsigned int delay;
        size_t num_pairs;

        switch (get_next(in_fd)) {

          case CMD_WRITE:
            num_pairs = parse_write(in_fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
            if (num_pairs == 0) {
              fprintf(stderr, "Invalid write command. See HELP for usage\n");
              break;
            }

            if (kvs_write(num_pairs, keys, values)) {
              fprintf(stderr, "Failed to write pair\n");
            }

            break;

          case CMD_READ:
            num_pairs = parse_read_delete(in_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
            if (num_pairs == 0) {
              fprintf(stderr, "Invalid read command. See HELP for usage\n");
              break;
            }

            if (kvs_read(num_pairs, keys, out_fd)) {
              fprintf(stderr, "Failed to read pair\n");
            }
            break;

          case CMD_DELETE:
            num_pairs = parse_read_delete(in_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
            if (num_pairs == 0) {
              fprintf(stderr, "Invalid delete command. See HELP for usage\n");
              break; 
            }
            if (kvs_delete(num_pairs, keys, out_fd)) {
              fprintf(stderr, "Failed to delete pair\n");
            }
            break; 

          case CMD_SHOW:
            kvs_show(out_fd);
            break;

          case CMD_WAIT:
            if (parse_wait(in_fd, &delay, NULL) == -1) {
              fprintf(stderr, "Invalid wait command. See HELP for usage\n");
              break;
            }

            if (delay > 0) {
              write(out_fd, "Waiting...\n", 11);
              kvs_wait(delay);
            }
            break;  

          case CMD_BACKUP:
            if(NUM_BACKUPS_EXEC > MAX_BACKUPS){
              waitpid(-1, &status, WNOHANG);
              if(!WIFEXITED(status) || (WIFEXITED(status) && WEXITSTATUS(status) > 0)){
                fprintf(stderr, "Child backup %d returned error\n", pid_temp);
                exit(1);
              }
              fprintf(stderr, "Backup %d exited with state %d\n", pid_temp, status);
            }
            else{
              NUM_BACKUPS_EXEC++;
            }
            fprintf(stderr, "Starting backup %lu for file %s\n", NUM_BACKUPS_EXEC, dp->d_name);
            
              
            bck_path = malloc(sizeof(char) * (strlen(t->dir_name) + name_size + strlen(OUT_BCK) + 20));
            if(bck_path == NULL){
              fprintf(stderr, "Malloc failed for bck_path\n");
              exit(1);
            } 
            strcpy(bck_path, t->dir_name);
            strcat(bck_path, "/");
            strncat(bck_path, dp->d_name, name_size);
            strcat(bck_path, "-");
            sprintf(final + strlen(final), "%lu", NUM_BACKUPS_EXEC);
            strcat(bck_path, final);
            final[0] = '\0';
            strcat(bck_path, OUT_BCK);
            int bck_fd = open(bck_path, O_CREAT | O_WRONLY | O_TRUNC, write_perms);
            free(bck_path);
            
            pid2 = fork();
            if(pid2 > 0){
              break;
            }
            if(pid2 == 0){
              if (kvs_backup(bck_fd)) {
                fprintf(stderr, "Failed to perform backup.\n");
                exit(1);
              }
              close(bck_fd);
              fprintf(stderr, "Backup completed successfully in child process %d\n", getpid());
              exit(0);
              }
              if (pid2 < 0) {  
                fprintf(stderr, "Fork Failed\n");
              } 
              break;

          case CMD_INVALID:
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              break;

          case CMD_HELP:
            printf( 
                "Available commands:\n"
                "  WRITE [(key,value)(key2,value2),...]\n"
                "  READ [key,key2,...]\n"
                "  DELETE [key,key2,...]\n"
                "  SHOW\n"
                "  WAIT <delay_ms>\n"
                "  BACKUP\n" 
                "  HELP\n"
            );

            break;
            
          case CMD_EMPTY:
            break; 

          case EOC:
            flag = 0;
            break; 
      }
    } 
  close(in_fd);
  close(out_fd);
  kvs_terminate();
  flag = 1; 

    if(pid2 > 0){
      while (pid_temp == waitpid(-1, &status, WNOHANG)){
        for(i = 1; i < NUM_BACKUPS_EXEC; i++){
          if(!WIFEXITED(status) || (WIFEXITED(status) && WEXITSTATUS(status) > 0)){
            fprintf(stderr, "Child backup %d returned error\n", pid_temp);
            exit(1);
          }
          fprintf(stderr, "Backup %d exited with state %d\n", pid_temp, status);
        }
      }
    }
  }  
  return NULL;
}

