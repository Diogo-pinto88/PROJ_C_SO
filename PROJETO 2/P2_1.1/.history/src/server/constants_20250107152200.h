#define MAX_WRITE_SIZE 256
#define MAX_STRING_SIZE 40
#define MAX_JOB_FILE_NAME_SIZE 256
#define MAX_SESSION_COUNT 1
#define PROD_CONS_SIZE 20

typedef struct thread_args
{
	char req[40];
	char resp[40];
} t_args;