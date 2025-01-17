#include "api.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>

// Global Variables

int request, response, notification; 

char req_path[40];
char resp_path[40];
char notif_path[40];
char result;
int i;

void print_response(const char *operation, char response_code) {
    printf("Server returned %c for operation: %s\n", response_code, operation);
}

int kvs_connect(char const *req_pipe_path, char const *resp_pipe_path,
                char const *server_pipe_path, char const *notif_pipe_path,
                int *notif_pipe) {

  // create pipes and connect

  strncpy(req_path, req_pipe_path, sizeof(req_path));
  strncpy(resp_path, resp_pipe_path, sizeof(resp_path));
  strncpy(notif_path, notif_pipe_path, sizeof(notif_path));

  
  if (mkfifo(req_path, 0640) != 0 || mkfifo(resp_path, 0640) != 0 || mkfifo(notif_path, 0640) != 0)
  {
    fprintf(stderr, "[ERR]: mkfifo failed\n");
    return 1;
  }

  int serv = open(server_pipe_path, O_WRONLY);
  if(serv == -1){
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  char *msg_serv = malloc(sizeof(char) * (1+40+40+40));
  msg_serv[0] = '1';
  strncpy(msg_serv + 1, req_path, strlen(req_path));
  for(i = strlen(req_path) + 1; i < 1 + 40; i++)
  {
    msg_serv[i] = '\0';
  }

  strncpy(msg_serv + 1 + 40, resp_path, strlen(resp_path));
  for(i = strlen(resp_path) + 1 + 40; i < 1 + 40 + 40; i++)
  {
    msg_serv[i] = '\0';
  }

  strncpy(msg_serv + 1 + 40 + 40, notif_path, strlen(notif_path));
  for(i = strlen(notif_path) + 1 + 40 + 40; i < 1 + 40 + 40 + 40; i++)
  {
    msg_serv[i] = '\0';
  }

  if(write(serv, msg_serv, sizeof(char) * (1 + 40 + 40 + 40)) == -1)
  {
    fprintf(stderr, "[ERR]: write to server failed: %s\n", strerror(errno));
    free(msg_serv);
    close(serv);
    return 1;
  }
  
  request = open(req_path, O_WRONLY);
  if(request == -1){
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  response = open(resp_path, O_RDONLY);
  if(response == -1){
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  notification = open(notif_path, O_RDONLY);
  if(notification == -1){
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  char response_msg[2] = {0}; // 1 byte para OP_CODE e 1 byte para resultado
  if(read(response, response_msg, sizeof(response_msg)) == -1)
  {
    fprintf(stderr, "[ERR]: Failed to read connect response: %s\n", strerror(errno));
    close(request);
    close(response);
    close(notification);
    close(serv);
    return 1;
  }

  // Validar a resposta
  if(response_msg[0] != '1') {
    fprintf(stderr, "[ERR]: Invalid response OP_CODE for CONNECT\n");
    close(request);
    close(response);
    close(notification);
    close(serv);
    return 1;
  }

  print_response("connect", response_msg[1]);

  //Verificar resultado do servidor
  if(response_msg[1] != '0') {
    fprintf(stderr, "[ERR]: Server returned error for CONNECT operation\n");
    close(request);
    close(response);
    close(notification);
    close(serv);
    return 1;
  }

  
  *notif_pipe = notification;

  free(msg_serv);
  close(serv);

  return 0;
}

int kvs_disconnect(void) {
  // close pipes and unlink pipe files

 // Enviar mensagem de pedido ao servidor
 if(write(request, "2", 1) == -1) {
  fprintf(stderr, "[ERR]: Failed to send DISCONNECT request: %s\n", strerror(errno));
  return 1;
  }

  // Ler a resposta do servidor
  char response_msg[2] = {0}; // 1 byte para OP_CODE e 1 byte para resultado
  if(read(response, response_msg, sizeof(response_msg)) == -1)
  {
    fprintf(stderr, "[ERR]: Failed to read DISCONNECT response: %s\n", strerror(errno));
    return 1;
  }

  // Validar OP_CODE na resposta
  if(response_msg[0] != '2') {
    fprintf(stderr, "[ERR]: Invalid response OP_CODE for DISCONNECT\n");
    return 1;
  }

  print_response("disconnect", response_msg[1]);

  // Verificar resultado do servidor
  if(response_msg[1] != '0') {
    fprintf(stderr, "[ERR]: Server returned error for DISCONNECT operation\n");
    return 1;
  }

  close(request);
  close(response);
  close(notification);

  if(unlink(req_path) != 0 && errno != ENOENT){
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", req_path, strerror(errno));
    return 1;
  }

  if(unlink(resp_path) != 0 && errno != ENOENT){
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", resp_path, strerror(errno));
    return 1;
  }

  if(unlink(notif_path) != 0 && errno != ENOENT){
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", notif_path, strerror(errno));
    return 1;
  }

  return 0;
}

int kvs_subscribe(const char *key) {
  // send subscribe message to request pipe and wait for response in response
  // pipe

  if(strlen(key) > 40)
  {
    fprintf(stderr, "[ERR]: Key exceeds maximum length of 40 characters\n");
    return 1;
  }

  char *msg = malloc(sizeof(char) * (1 + 41));
  msg[0] = '3';
  strncpy(msg + 1, key, strlen(key));
  for(i = strlen(key) + 1; i < 1 + 41; i++)
  {
    msg[i] = '\0';
  }

  if(write(request, msg, sizeof(msg) == -1))
  {
    fprintf(stderr, "[ERR]: Failed to send SUBSCRIBE request: %s\n", strerror(errno));
    free(msg);
    return 1;
  }

  char response_msg[2] = {0}; 
  if(resd(response, response_msg, sizeof(response_msg)) == -1)
  {
    fprintf(stderr, "[ERR]: Failed to read SUBSCRIBE response: %s\n", strerror(errno));
    free(msg);
    return 1;
  }

  if(response_msg[0] != '3')
  {
    fprintf(stderr, "[ERR]: Invalid response OP_CODE for SUBSCRIBE\n");
    free(msg);
    return 1;
  }

  if(response_msg[1] == 0){
    fprintf(stderr, "Server returned 0 for operation: subscribe (key does not exist)\n");
  } else if (response_msg[1] == 1){
    fprintf(stderr, "Server returned 1 for operation: subscribe (key exists)\n");
  } else {
        fprintf(stderr, "[ERR]: Unknown result for SUBSCRIBE operation\n");
        return 1;
  }

  free(msg);
  return 0;

}

int kvs_unsubscribe(const char *key) {
  // send unsubscribe message to request pipe and wait for response in response
  // pipe

  if(strlen(key) > 40)
  {
    fprintf(stderr, "[ERR]: Key exceeds maximum length of 40 characters\n");
    return 1;
  }

  char *msg = malloc(sizeof(char) * (1 + 41));
  msg[0] = '4';
  strncpy(msg + 1, key, strlen(key));
  for(i = strlen(key) + 1; i < 1 + 41; i++)
  {
    msg[i] = '\0';
  }

  if(write(request, msg, sizeof(msg) == -1))
  {
    fprintf(stderr, "[ERR]: Failed to send UNSUBSCRIBE request: %s\n", strerror(errno));
    free(msg);
    return 1;
  }

  char response_msg[2] = {0};
  
  if(read(response, response_msg, sizeof(response_msg)) == -1)
  {
    fprintf(stderr, "[ERR]: Failed to read UNSUBSCRIBE response: %s\n", strerror(errno));
    free(msg);
    return 1;
  }

  if(response_msg[0] != '4')
  {
    fprintf(stderr, "[ERR]: Invalid response OP_CODE for UNSUBSCRIBE\n");
    free(msg);
    return 1;
  }

  if(response_msg[1] == 0){
    fprintf(stderr, "Server returned 0 for operation: Subscription removed\n");
  } else if(response_msg[1] == 1){
    fprintf(stderr, "Server returned 1 for operation: Subscription does not exist, nothing to remove\n");
  } else {
    fprintf(stderr, "[ERR]: Unknown result for UNSUBSCRIBE operation\n");
    return 1;
  }

  free(msg);
  return 0;

}
