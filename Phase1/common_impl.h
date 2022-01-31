#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <err.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>


/* autres includes (eventuellement) */

#define ERROR_EXIT(str) {perror(str);exit(EXIT_FAILURE);}

/**************************************************************/
/****************** DEBUT DE PARTIE NON MODIFIABLE ************/
/**************************************************************/

#define MAX_STR  (1024)
typedef char maxstr_t[MAX_STR];

/* definition du type des infos */
/* de connexion des processus dsm */
struct dsm_proc_conn  {
   int      rank;
   maxstr_t machine;
   int      port_num;
   int      fd;
   int      fd_for_exit; /* special */
};

typedef struct dsm_proc_conn dsm_proc_conn_t;

/**************************************************************/
/******************* FIN DE PARTIE NON MODIFIABLE *************/
/**************************************************************/

/* definition du type des infos */
/* d'identification des processus dsm */
struct dsm_proc {
  pid_t pid;
  dsm_proc_conn_t connect_info;
};
typedef struct dsm_proc dsm_proc_t;

//PROTOTYPES
char *strremove(char *str, const char *sub);

int socket_listen_and_bind(unsigned short * port);
int socket_and_connect(char *hostname, char *port);

void write_int_size(int fd, void *ptr);
int read_int_size(int fd);
int read_from_socket(int fd, void *buf, int size);
int write_in_socket(int fd, void *buf, int size);