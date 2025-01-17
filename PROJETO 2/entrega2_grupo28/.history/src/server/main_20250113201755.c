#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "constants.h"
#include "io.h"
#include "operations.h"
#include "parser.h"
#include "pthread.h"
#include "../common/constants.h"
#include "kvs.h"
#include "../common/io.h"
#include "../client/api.h"

struct SharedData {
  DIR *dir;
  char *dir_name;
  pthread_mutex_t directory_mutex;
};

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t n_current_backups_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t n_current_sessions_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t subscriptions_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t session_cond = PTHREAD_COND_INITIALIZER;


size_t active_backups = 0; // Number of active backups
size_t active_sessions = 0; // Number of active sessions
size_t max_backups;        // Maximum allowed simultaneous backups
size_t max_threads;        // Maximum allowed simultaneous threads
size_t subscriptions_count = 0; // Number of active subscriptions
//pthread_t *tid;
char *jobs_directory = NULL;
char *registration_fifo = NULL;
char *registers = NULL;
char *requests = NULL;
char *subscriptions = NULL;
int var = 0;
//t_args **buffer;


int fifo_de_registo_fd = -1;
int response_fd = -1;
int notification_fd = -1;
int request_fd = -1;

char req_pipe[41], resp_pipe[41], notif_pipe[41];

int filter_job_files(const struct dirent *entry) {
  const char *dot = strrchr(entry->d_name, '.');
  if (dot != NULL && strcmp(dot, ".job") == 0) {
    return 1; // Keep this file (it has the .job extension)
  }
  return 0;
}

static int entry_files(const char *dir, struct dirent *entry, char *in_path,
                       char *out_path) {
  const char *dot = strrchr(entry->d_name, '.');
  if (dot == NULL || dot == entry->d_name || strlen(dot) != 4 ||
      strcmp(dot, ".job")) {
    return 1;
  }

  if (strlen(entry->d_name) + strlen(dir) + 2 > MAX_JOB_FILE_NAME_SIZE) {
    fprintf(stderr, "%s/%s\n", dir, entry->d_name);
    return 1;
  }

  strcpy(in_path, dir);
  strcat(in_path, "/");
  strcat(in_path, entry->d_name);

  strcpy(out_path, in_path);
  strcpy(strrchr(out_path, '.'), ".out");

  return 0;
}

static int run_job(int in_fd, int out_fd, char *filename) {
  size_t file_backups = 0;
  while (1) {
    char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    unsigned int delay;
    size_t num_pairs;

    switch (get_next(in_fd)) {
    case CMD_WRITE:
      num_pairs = parse_write(in_fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_write(num_pairs, keys, values)) {
        write_str(STDERR_FILENO, "Failed to write pair\n");
      }
      break;

    case CMD_READ:
      num_pairs = parse_read_delete(in_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_read(num_pairs, keys, out_fd)) {
        write_str(STDERR_FILENO, "Failed to read pair\n");
      }
      break;

    case CMD_DELETE:
      num_pairs = parse_read_delete(in_fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);
      if (num_pairs == 0) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_delete(num_pairs, keys, out_fd)) {
        write_str(STDERR_FILENO, "Failed to delete pair\n");
      }
      break;

    case CMD_SHOW:
      kvs_show(out_fd);
      break;

    case CMD_WAIT:
      if (parse_wait(in_fd, &delay, NULL) == -1) {
        write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay > 0) {
        printf("Waiting %d seconds\n", delay / 1000);
        kvs_wait(delay);
      }
      break;

    case CMD_BACKUP:
      pthread_mutex_lock(&n_current_backups_lock);
      if (active_backups >= max_backups) {
        wait(NULL);
      } else {
        active_backups++;
      }
      pthread_mutex_unlock(&n_current_backups_lock);
      int aux = kvs_backup(++file_backups, filename, jobs_directory);

      if (aux < 0) {
        write_str(STDERR_FILENO, "Failed to do backup\n");
      } else if (aux == 1) {
        return 1;
      }
      break;

    case CMD_INVALID:
      write_str(STDERR_FILENO, "Invalid command. See HELP for usage\n");
      break;

    case CMD_HELP:
      write_str(STDOUT_FILENO,
                "Available commands:\n"
                "  WRITE [(key,value)(key2,value2),...]\n"
                "  READ [key,key2,...]\n"
                "  DELETE [key,key2,...]\n"
                "  SHOW\n"
                "  WAIT <delay_ms>\n"
                "  BACKUP\n" // Not implemented
                "  HELP\n");

      break;

    case CMD_EMPTY:
      break;

    case EOC:
      printf("EOF\n");
      return 0;
    }
  }
}

// frees arguments
static void *get_file(void *arguments) {
  struct SharedData *thread_data = (struct SharedData *)arguments;
  DIR *dir = thread_data->dir;
  char *dir_name = thread_data->dir_name;

  if (pthread_mutex_lock(&thread_data->directory_mutex) != 0) {
    fprintf(stderr, "Thread failed to lock directory_mutex\n");
    return NULL;
  }

  struct dirent *entry;
  char in_path[MAX_JOB_FILE_NAME_SIZE], out_path[MAX_JOB_FILE_NAME_SIZE];
  while ((entry = readdir(dir)) != NULL) {
    if (entry_files(dir_name, entry, in_path, out_path)) {
      continue;
    }

    if (pthread_mutex_unlock(&thread_data->directory_mutex) != 0) {
      fprintf(stderr, "Thread failed to unlock directory_mutex\n");
      return NULL;
    }

    int in_fd = open(in_path, O_RDONLY);
    if (in_fd == -1) {
      write_str(STDERR_FILENO, "Failed to open input file: ");
      write_str(STDERR_FILENO, in_path);
      write_str(STDERR_FILENO, "\n");
      pthread_exit(NULL);
    }

    int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd == -1) {
      write_str(STDERR_FILENO, "Failed to open output file: ");
      write_str(STDERR_FILENO, out_path);
      write_str(STDERR_FILENO, "\n");
      pthread_exit(NULL);
    }

    int out = run_job(in_fd, out_fd, entry->d_name);

    close(in_fd);
    close(out_fd);

    if (out) {
      if (closedir(dir) == -1) {
        fprintf(stderr, "Failed to close directory\n");
        return 0;
      }

      exit(0);
    }

    if (pthread_mutex_lock(&thread_data->directory_mutex) != 0) {
      fprintf(stderr, "Thread failed to lock directory_mutex\n");
      return NULL;
    }
  }

  if (pthread_mutex_unlock(&thread_data->directory_mutex) != 0) {
    fprintf(stderr, "Thread failed to unlock directory_mutex\n");
    return NULL;
  }

  pthread_exit(NULL);
}

void notify_clients(char *key, char *value) {
  pthread_mutex_lock(&subscriptions_lock);
  notification_fd = open(notif_pipe, O_WRONLY);
  if(notification_fd == -1) {
    fprintf(stderr, "Failed to open notification pipe\n");
    exit(EXIT_FAILURE);
  }
  for (size_t i = 0; i < subscriptions_count; ++i) {
    if(strcmp(&(subscriptions[i]), key) == 0){
      char buffer[82];
      strncpy(buffer, key, 41);
      strncpy(buffer + 41, value, 41);
      write(notification_fd, buffer, sizeof(buffer));
      close(notification_fd);
    }
  }
  pthread_mutex_unlock(&subscriptions_lock);
}


static void dispatch_threads(DIR *dir) {
  pthread_t *threads = malloc(max_threads * sizeof(pthread_t));

  if (threads == NULL) {
    fprintf(stderr, "Failed to allocate memory for threads\n");
    return;
  }

  struct SharedData thread_data = {dir, jobs_directory,
                                   PTHREAD_MUTEX_INITIALIZER};

  for (size_t i = 0; i < max_threads; i++) {
    if (pthread_create(&threads[i], NULL, get_file, (void *)&thread_data) !=
        0) {
      fprintf(stderr, "Failed to create thread %zu\n", i);
      pthread_mutex_destroy(&thread_data.directory_mutex);
      free(threads);
      return;
    }
  }

  // ler do FIFO de registo

  char opcode;
  ssize_t ret;
  int flag = 0;
  
  while(1){
    // Ler o OPCODE
    int result = read_all(fifo_de_registo_fd, &opcode, sizeof(opcode), NULL);
    if(result <= 0) {
      if(errno == EINTR) {continue;}
      fprintf(stderr, "Failed to read from registration FIFO\n");
      break;
    } 
    
    fprintf(stderr, "Pedido de registo recebido\n");

    // Ler o resto da mensagem
    result = read_all(fifo_de_registo_fd, registers + 1 , sizeof(registers) - 1, NULL);
    if(result == 0){continue;}
    else if (result < 0) {
      fprintf(stderr, "Failed to read complete registration registers\n");
      break;
    }


    ret = read(fifo_de_registo_fd, registers, sizeof(registers));
    switch (ret){

        case -1:
            if(errno == EINTR) {continue;}
            fprintf(stderr, "Invalid registers message format.\n");
            exit(EXIT_FAILURE);
            break;

        case 0:
            close(fifo_de_registo_fd);
            while(1){
              fifo_de_registo_fd = open(registration_fifo, O_RDONLY | O_NONBLOCK);
              if(fifo_de_registo_fd == -1) {
                if(errno == EINTR) {continue;}
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
              } else {break;}
          }
          continue;

        case 121:
            if(registers[0] < '1' || registers[0] > '4') {
              fprintf(stderr, "Wrong OP_CODE for this pipe\n");
              continue;
            }

            strncpy(req_pipe, registers + 1, 40);
            strncpy(resp_pipe, registers + 41, 40);
            strncpy(notif_pipe, registers + 81, 40);
            req_pipe[40] = '\0';
            resp_pipe[40] = '\0';
            notif_pipe[40] = '\0';

            request_fd = open(req_pipe, O_WRONLY);
            if(request_fd == -1){
              fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
              exit(EXIT_FAILURE);
            }


            int res = read_all(request_fd, requests, sizeof(requests), NULL);
            if(res == 0){
              continue;
            } else if(res < 0){
              fprintf(stderr, "[ERR]: Failed to read from request pipe\n");
              exit(EXIT_FAILURE);
            }
            //Envio de resposta
            char response[2];
            response[0] = registers[0];
            response_fd = open(resp_pipe, O_WRONLY);
            if(response_fd == -1) {
              fprintf(stderr, "Failed to open response pipe\n");
              continue;
            }

            switch (registers[0]) {
              case '1': // Connect
                  pthread_mutex_lock(&n_current_sessions_lock);
                  if(kvs_connect(req_pipe, resp_pipe, registration_fifo, notif_pipe, &notification_fd) == 1){
                    response[1] = '1'; //Erro
                  if(active_sessions < MAX_SESSION_COUNT){
                    active_sessions++;
                  }
                  else{
                    while(active_sessions >= MAX_SESSION_COUNT){
                      pthread_cond_wait(&session_cond, &n_current_sessions_lock);
                    }
                  }
                  response[1] = '0'; //Sucesso
                  }  
                  pthread_mutex_unlock(&n_current_sessions_lock);
                  break;

              case '2' : //Disconnect
                  pthread_mutex_lock(&n_current_sessions_lock);
                  active_sessions--;
                  pthread_cond_signal(&session_cond);
                  pthread_mutex_unlock(&n_current_sessions_lock);

                  // TODO remover todas as subscriçoes do cliente
                  response[1] = '0'; 
                  break;
              
              case '3': //Subscribe
                  if(key_exists(&(requests[1])) == 1){
                    response[1] = '1';
                    subscriptions[var] = registers[1];
                    var++;
                  } else { response[1] = '0'; }
                  break;

              case '4': //Unsubscribe 
                  // TODO cliente indica ao servidor que não quer mais receber notificações de uma chave
                  for(int k = 0; k < var; k++){
                    if(strcmp(&(subscriptions[k]), &(requests[1])) == 0){
                      subscriptions[k] = '\0';
                      response[1] = '0';
                      flag = 1;
                      break;
                    }
                  }
                  if(flag == 0){
                    response[1] = '1';
                  }
                  break;
              default:
                  fprintf(stderr, "[ERR]: Unknown operation code: %c\n", registers[0]);
                  break;
            }

            if(write(response_fd, response, sizeof(response)) == -1) {
              fprintf(stderr, "Failed to write response to client.\n");
            }
            close(response_fd);
            break;
        default:
            fprintf(stderr, "Unexpected message size: %zd bytes\n", ret);
            break; 
    }
  }

  for (unsigned int i = 0; i < max_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "Failed to join thread %u\n", i);
      pthread_mutex_destroy(&thread_data.directory_mutex);
      free(threads);
      exit(EXIT_FAILURE);
    }

  if (pthread_mutex_destroy(&thread_data.directory_mutex) != 0) {
    fprintf(stderr, "Failed to destroy directory_mutex\n");
  }
  free(threads);
  }
}

int main(int argc, char **argv) {
  if (argc < 5) {
    write_str(STDERR_FILENO, "Usage: ");
    write_str(STDERR_FILENO, argv[0]);
    write_str(STDERR_FILENO, " <jobs_dir>");
    write_str(STDERR_FILENO, " <max_threads>");
    write_str(STDERR_FILENO, " <max_backups> \n");
    write_str(STDERR_FILENO, " nome_do_FIFO_de_registo\n");
    return 1;
  }

  jobs_directory = argv[1];

  
  char *endptr;
  max_backups = strtoul(argv[3], &endptr, 10);

  if (*endptr != '\0') {
    fprintf(stderr, "Invalid max_proc value\n");
    return 1;
  }

  max_threads = strtoul(argv[2], &endptr, 10);

  if (*endptr != '\0') {
    fprintf(stderr, "Invalid max_threads value\n");
    return 1;
  }

  if (max_backups <= 0) {
    write_str(STDERR_FILENO, "Invalid number of backups\n");
    return 0;
  }

  if (max_threads <= 0) {
    write_str(STDERR_FILENO, "Invalid number of threads\n");
    return 0;
  }

  registration_fifo = argv[4];
  if(registration_fifo == NULL) {
    fprintf(stderr, "Failed to get registration FIFO\n");
    exit(EXIT_FAILURE);
  }

  // Remove pipe if it exists
  if(unlink(registration_fifo) != 0 && errno != ENOENT) {
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", registration_fifo, strerror(errno));
    exit(EXIT_FAILURE);
  }

  //Create FIFO
  if(mkfifo(registration_fifo, 0640) != 0) {
    fprintf(stderr, "[ERR]: mkfifo(%s) failed: %s\n", registration_fifo, strerror(errno));
    exit(EXIT_FAILURE);
  }

  while(1){
    fifo_de_registo_fd = open(registration_fifo, O_RDONLY | O_NONBLOCK);
    if(fifo_de_registo_fd == -1) {
      if(errno == EINTR) {continue;}
      fprintf(stderr, "Failed to open fifo_de_registo\n");
      exit(EXIT_FAILURE);
    }
    else {
      break;
    }
  }
  
  registers = malloc(sizeof(char) * (1+40+40+40));
  if(registers == NULL) {
    fprintf(stderr, "[Err]: allocation of memory for incoming registers failed\n");
    exit(EXIT_FAILURE);
  }

  /*
  buffer = malloc(sizeof(t_args*) * PROD_CONS_SIZE);
  if(buffer == NULL){
    fprintf(stderr, "[Err]: allocation of memory for buffer failed\n");
    exit(EXIT_FAILURE);
  }
  */

  if (kvs_init()) {
    write_str(STDERR_FILENO, "Failed to initialize KVS\n");
    exit(EXIT_FAILURE);
  }
/*
  for(i = 0; i < PROD_CONS_SIZE; i++){
    buffer[i] = malloc(sizeof(t_args));
    if(buffer[i] == NULL){
      fprintf(stderr, "[Err]: allocation of memory for prod/cons buffer failed\n");
      exit(EXIT_FAILURE);
    }
  }

  
  pthread_mutex_init(&lock, NULL);
  pthread_mutex_init(&n_current_backups_lock, NULL);
  pthread_mutex_init(&n_current_sessions_lock, NULL);


  // ISTO NAO E PARA FICAR AQUI??
  //[
  int *ids = malloc(sizeof(int)*MAX_SESSION_COUNT);
  for(i = 0; i < MAX_SESSION_COUNT; i++) {
    ids[i] = i;
  }

  tid = malloc(sizeof(pthread_t) * MAX_SESSION_COUNT);

  for(i = 0; i < MAX_SESSION_COUNT; i++){
    pthread_create(&(tid[i]), 0, dispatch_threads, (void*) &(ids[i]));
  } 

  
  for(i = 0; i < MAX_SESSION_COUNT; i++){
    pthread_join(tid[i], NULL);
  }
  //]*/

  DIR *dir = opendir(argv[1]);
  if (dir == NULL) {
    fprintf(stderr, "Failed to open directory: %s\n", argv[1]);
    return 0;
  }

  dispatch_threads(dir);
  free(registers);

  if (closedir(dir) == -1) {
    fprintf(stderr, "Failed to close directory\n");
    return 0;
  }

  while (active_backups > 0) {
    wait(NULL);
    active_backups--;
  }

  kvs_terminate();
  close(fifo_de_registo_fd);
  unlink(registration_fifo);

  return 0;
}
