#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"
#include "src/client/api.h"
#include "src/common/constants.h"
#include "src/common/io.h"



int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <client_unique_id> <register_pipe_path>\n",
            argv[0]);
    return 1;
  }

  char req_pipe_path[256] = "/tmp/req";
  char resp_pipe_path[256] = "/tmp/resp";
  char notif_pipe_path[256] = "/tmp/notif";

  char keys[MAX_NUMBER_SUB][MAX_STRING_SIZE] = {0};
  unsigned int delay_ms;
  size_t num;
  int register_fd = -1;
  pthread_t notification_thread;
  


  strncat(req_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
  strncat(resp_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
  strncat(notif_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));

  // open pipes

  register_fd = open(argv[2], O_WRONLY);
  if (register_fd == -1) {
    fprintf(stderr, "Failed to open register pipe\n");
    return 1;
  }

  int notif_pipe;
  if (kvs_connect(req_pipe_path, resp_pipe_path, argv[2], notif_pipe_path,
                  &notif_pipe) != 0) {
    fprintf(stderr, "Failed to connect to the server\n");
    close(register_fd);
    return 1;
  }


  if (pthread_create(&notification_thread, NULL, process_notifications, NULL) != 0) {
    fprintf(stderr, "[ERR] Falha ao criar a thread de notificações\n");
    close(notification_fd);
    return 1;
}

  fprintf(stderr, "Connected to server successfully.\n");
  close(register_fd);

  while (1) {
    switch (get_next(STDIN_FILENO)) {
    case CMD_DISCONNECT:
      if (kvs_disconnect() != 0) {
        fprintf(stderr, "Failed to disconnect to the server\n");
        return 1;
      }
      // TODO: end notifications thread

      printf("Disconnected from server\n");
      return 0;

    case CMD_SUBSCRIBE:
      num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
      if (num == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_subscribe(keys[0])) {
        fprintf(stderr, "Command subscribe failed\n");
      }

      break;

    case CMD_UNSUBSCRIBE:
      num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
      if (num == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (kvs_unsubscribe(keys[0])) {
        fprintf(stderr, "Command subscribe failed\n");
      }

      break;

    case CMD_DELAY:
      if (parse_delay(STDIN_FILENO, &delay_ms) == -1) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (delay_ms > 0) {
        printf("Waiting...\n");
        delay(delay_ms);
      }
      break;

    case CMD_INVALID:
      fprintf(stderr, "Invalid command. See HELP for usage\n");
      break;

    case CMD_EMPTY:
      break;

    case EOC:
      kvs_disconnect();
      break;
    }
  }
}

// Função para processar notificações
void *process_notifications(void *arg) {
  int notification_fd = -1;
    char buffer[82]; // 40 caracteres para chave + 1 para '\0' + 40 para valor + 1 para '\0'
    while (1) {
        ssize_t bytes_read = read(notification_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0'; // Garante que está terminado por '\0'

            // Separar chave e valor
            char key[41] = {0};
            char value[41] = {0};
            strncpy(key, buffer, 40);
            strncpy(value, buffer + 40, 40);

            // Determinar se é uma exclusão
            if (strcmp(value, "DELETED") == 0) {
                printf("(%s,DELETED)\n", key);
            } else {
                printf("(%s,%s)\n", key, value);
            }
        } else if (bytes_read == -1) {
            perror("[ERR] Falha ao ler do FIFO de notificações");
            break;
        } else {
            break; // FIFO foi fechado
        }
    }
    return NULL;
}
