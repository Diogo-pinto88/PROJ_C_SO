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

int kvs_connect(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path,
                char const* notif_pipe_path, int* notif_pipe) {
  // create pipes and connect
  request = open(req_pipe_path, O_WRONLY);
  response = open(resp_pipe_path, O_RDONLY);
  notification = open(notif_pipe_path, O_RDONLY);
  return 0;
}
 
int kvs_disconnect(void) {
  // close pipes and unlink pipe files
  if(write(request, "2", sizeof(char) ))
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


