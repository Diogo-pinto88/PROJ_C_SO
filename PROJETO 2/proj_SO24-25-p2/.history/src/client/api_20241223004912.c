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
char result; 
int i;

int kvs_connect(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path,
                char const* notif_pipe_path, int* notif_pipe) {
  // create pipes and connect

  if(unlink(req_pipe_path) != 0 && errno != ENOENT){
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", req_pipe_path, strerror(errno));
    return 1;
  }

  if(unlink(resp_pipe_path) != 0 && errno != ENOENT){
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", resp_pipe_path, strerror(errno));
    return 1;
  }

  if(unlink(notif_pipe_path) != 0 && errno != ENOENT){
    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", notif_pipe_path, strerror(errno));
    return 1;
  }
  
  if (mkfifo(req_pipe_path, 0640) != 0 || mkfifo(resp_pipe_path, 0640) != 0 || mkfifo(notif_pipe_path, 0640) != 0)
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
  strncpy(msg_serv + 1, req_pipe_path, strlen(req_pipe_path));
  for(i = strlen(req_pipe_path) + 1; i < 1 + 40; i++)
  {
    msg_serv[i] = '\0';
  }

  strncpy(msg_serv + 1 + 40, resp_pipe_path, strlen(resp_pipe_path));
  for(i = strlen(resp_pipe_path) + 1 + 40; i < 1 + 40 + 40; i++)
  {
    msg_serv[i] = '\0';
  }

  strncpy(msg_serv + 1 + 40 + 40, notif_pipe_path, strlen(notif_pipe_path));
  for(i = strlen(notif_pipe_path) + 1 + 40 + 40; i < 1 + 40 + 40 + 40; i++)
  {
    msg_serv[i] = '\0';
  }

  if(write(serv, msg_serv, sizeof(char) * (1 + 40 + 40 + 40)) == -1)
  {
    return 1;
  }
  
  request = open(req_pipe_path, O_WRONLY);
  if(request == -1){
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  response = open(resp_pipe_path, O_RDONLY);
  if(response == -1){
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  notification = open(notif_pipe_path, O_RDONLY);
  if(notification == -1){
    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
    return 1;
  }

  

  


  return 0;
}
 
int kvs_disconnect(void) {
  // close pipes and unlink pipe files
  if(write(request, "2", sizeof(char)) == -1) {
    return 1;
  }

  close(request);
  close(response);
  close(notification);

  return 0;
}

int kvs_subscribe(const char* key) {
  // send subscribe message to request pipe and wait for response in response pipe
  return 0;
}

int kvs_unsubscribe(const char* key) {
    // send unsubscribe message to request pipe and wait for response in response pipe
  return 0;
}


