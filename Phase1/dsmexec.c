#include "common_impl.h"


/* variables globales */

/* un tableau gerant les infos d'identification des processus dsm */
dsm_proc_t *proc_array = NULL;

/* le nombre de processus effectivement crees */
volatile int num_procs_creat = 0;

void usage(void){
  fprintf(stdout,"Usage : dsmexec machine_file executable arg1 arg2 ...\n");
  fflush(stdout);
  exit(EXIT_FAILURE);
}

void sigchld_handler(int sig){
   /* on traite les fils qui se terminent pour eviter les zombies */
   wait(NULL);
}

/*******************************************************/
/*********** ATTENTION : BIEN LIRE LA STRUCTURE DU *****/
/*********** MAIN AFIN DE NE PAS AVOIR A REFAIRE *******/
/*********** PLUS TARD LE MEME TRAVAIL DEUX FOIS *******/
/*******************************************************/

int main(int argc, char *argv[]){
   if (argc < 3){
    usage();
   }
   else {
   pid_t pid;
   int num_procs = 0;
   int i;

   /* Mise en place d'un traitant pour recuperer les fils zombies*/
   /*struct sigaction act;
   act.sa_handler = sigchld_handler;
   sigaction(SIGCHLD,&act,NULL);*/


   /* lecture du fichier de machines */
   /* 1- on recupere le nombre de processus a lancer */
   FILE * file=fopen(argv[1],"r");
   int c,c2='\0';
   int last = '\n';
   while (EOF != (c = fgetc(file))) {
      if (c == '\n' && last != '\n')
         num_procs++;
      last = c;
      c2=c;
   }
   if(c2!='\n')
      num_procs++;
   /* 2- on recupere les noms des machines : le nom de */
   rewind(file);
   dsm_proc_t* proc_array=malloc(num_procs*sizeof(dsm_proc_t));
   char* line=NULL;
   size_t len=0;
   ssize_t read;
   int k=0;
   while((read=getline(&line,&len,file))!=-1){
      if(read!=1){
         strcpy(proc_array[k].connect_info.machine,strremove(line,"\n"));   /* la machine est un des elements d'identification */
         proc_array[k].connect_info.rank=k;
         proc_array[k].connect_info.port_num=-1;
         k++;
      }
   }
   fclose(file);


   /* creation de la socket d'ecoute */
   /* ecoute effective */
	int listen_fd = -1;
   char *port=malloc(5*sizeof(char));
   unsigned short newport=0;
   strcpy(port,"0");
	if (-1 == (listen_fd = socket_listen_and_bind(&newport))) {
		printf("Could not create, bind and listen properly\n");
		return 1;
	}

   /* creation des fils */
   int *fd_out=malloc(sizeof(int)*num_procs);
   int *fd_err=malloc(sizeof(int)*num_procs);
   pid_t *pid_tab=malloc(num_procs*sizeof(int));      //tableau contenant tous les pid fils

   for(i = 0; i < num_procs ; i++) {
      int fd_out_tmp[2];
      int fd_err_tmp[2];
      /* creation du tube pour rediriger stdout */
      pipe(fd_out_tmp);
      /* creation du tube pour rediriger stderr */
      pipe(fd_err_tmp);

      pid = fork();
      if(pid == -1) ERROR_EXIT("fork");

      if (pid == 0) { /* fils */
         // redirection stdout
         close(STDOUT_FILENO);
         close(fd_out_tmp[0]);
         dup(fd_out_tmp[1]);
         close(fd_out_tmp[1]);
         // redirection stderr
         close(STDERR_FILENO);
         close(fd_err_tmp[0]);
         dup(fd_err_tmp[1]);
         close(fd_err_tmp[1]);

         /* Creation du tableau d'arguments pour le ssh */
         // A REVERIFIER
         char hostname[MAX_STR];
         gethostname(hostname,sizeof(hostname));
         char ** arg = malloc((argc + 4)*sizeof(char)*MAX_STR);

         arg[0]="ssh";
         arg[1]=proc_array[i].connect_info.machine;
         char * path=getenv("DSM_BIN");
         char * exec=malloc(MAX_STR*sizeof(char));
         sprintf(exec,"%s/%s",path,"dsmwrap");
         arg[2]=exec;
         sprintf(port,"%hu",newport);
         //arg[2]="dsmwrap";
         arg[3]=port;
         arg[4]=hostname;
         int k;
         for(k=5;k<argc+3;k++){
            arg[k]=argv[k-3];
         }
         arg[argc+4]=NULL;

         /* jump to new prog : */
         execvp("ssh",arg);
      }
      else  if(pid > 0) { /* pere */
         pid_tab[i]=pid;
         /* fermeture des extremites des tubes non utiles */
         close(fd_out_tmp[1]);
         close(fd_err_tmp[1]);
         //remplissage tableaux
         fd_out[i]=fd_out_tmp[0];
         fd_err[i]=fd_err_tmp[0];

         num_procs_creat++;
      }
   }


   for(i = 0; i < num_procs ; i++){
      /* on accepte les connexions des processus dsm */
      struct sockaddr proc_addr;
      socklen_t size = sizeof(proc_addr);
      int proc_fd;
      if (-1 == (proc_fd = accept(listen_fd, &proc_addr, &size))) {
         perror("Accept");
      }
      /*  On recupere le nom de la machine distante */
      /* 1- d'abord la taille de la chaine */
      int size_name = 0;
      size_name = read_int_size(proc_fd);
      /* 2- puis la chaine elle-meme */
      char name[MAX_STR];
      memset(name,'\0',MAX_STR);
      read_from_socket(proc_fd, name, size_name);

      /* On recupere le pid du processus distant  (optionnel)*/
      /* On recupere le numero de port de la socket d'ecoute des processus distants */
         /* cf code de dsmwrap.c */
      unsigned short newport=0;
      read_from_socket(proc_fd, (void *)&newport, sizeof(newport));

      int j=0;
      while(j<num_procs){
         //On balaye la liste pour trouver le nom et on vérifie que le port n'est pas déjà alloué
         if(strcmp(name,proc_array[j].connect_info.machine)==0 && proc_array[j].connect_info.port_num==-1){
            proc_array[j].connect_info.port_num=(int)newport;
            proc_array[j].connect_info.fd=proc_fd;
         }
         j++;
      }
   }

      /***********************************************************/
      /********** ATTENTION : LE PROTOCOLE D'ECHANGE *************/
      /********** DECRIT CI-DESSOUS NE DOIT PAS ETRE *************/
      /********** MODIFIE, NI DEPLACE DANS LE CODE   *************/
      /***********************************************************/

      for(i = 0; i < num_procs ; i++){
         /* 1- envoi du nombre de processus aux processus dsm*/
         /* On envoie cette information sous la forme d'un ENTIER */
         /* (IE PAS UNE CHAINE DE CARACTERES */
         write_in_socket(proc_array[i].connect_info.fd, (void *)&num_procs, sizeof(int));

         /* 2- envoi des rangs aux processus dsm */
         /* chaque processus distant ne reçoit QUE SON numéro de rang */
         /* On envoie cette information sous la forme d'un ENTIER */
         /* (IE PAS UNE CHAINE DE CARACTERES */

         int rank=i;
         write_in_socket(proc_array[i].connect_info.fd, (void *)&rank, sizeof(int));

         /* 3- envoi des infos de connexion aux processus */
         /* Chaque processus distant doit recevoir un nombre de */
         /* structures de type dsm_proc_conn_t égal au nombre TOTAL de */
         /* processus distants, ce qui signifie qu'un processus */
         /* distant recevra ses propres infos de connexion */
         /* (qu'il n'utilisera pas, nous sommes bien d'accords). */

         for(k = 0; k < num_procs ; k++){
            write_in_socket(proc_array[i].connect_info.fd, (void *)&proc_array[k].connect_info, sizeof(proc_array[k].connect_info));
         }
      }

      /***********************************************************/
      /********** FIN DU PROTOCOLE D'ECHANGE DES DONNEES *********/
      /********** ENTRE DSMEXEC ET LES PROCESSUS DISTANTS ********/
      /***********************************************************/

      /* gestion des E/S : on recupere les caracteres */
      /* sur les tubes de redirection de stdout/stderr */

      // Declare array of struct pollfd
      int nfds = 2*num_procs;
      struct pollfd pollfds[nfds];
      char buffer[MAX_STR];
      char buff2[MAX_STR];
      int stop=0;
      int start=0;
      // Init first slot with listening socket
      for(i=0;i<num_procs;i++){
         pollfds[i].fd = fd_out[i];
         pollfds[i].events = POLLIN;
         pollfds[i].revents = 0;
      }
      for(i=num_procs;i<2*num_procs;i++){
         pollfds[i].fd = fd_err[i-num_procs];
         pollfds[i].events = POLLIN;
         pollfds[i].revents = 0;
      }
      while (1) {
         /*je recupere les infos sur les tubes de redirection
         jusqu'à ce qu'ils soient inactifs (ie fermes par les
         processus dsm ecrivains de l'autre cote ...)*/

         int n_active = 0;
         if (-1 == (n_active = poll(pollfds, nfds, -1))) {
            perror("Poll");
         }

         for(i = 0; i<nfds/2; i++){
               stop=0;
               k=0;
               start=0;
               memset(buffer,0,MAX_STR);
               if (i<num_procs && pollfds[i].revents & POLLIN){
                  read_from_socket(pollfds[i].fd,buffer,sizeof(buffer));
                  while (buffer[k]!='\0'){
                    if (buffer[k]=='\n'){
                      memset(buff2,0,MAX_STR);
                      strncpy(buff2,buffer+start,k-start+1);
                      printf("[Proc %i:%s:stdout] %s",i,proc_array[i].connect_info.machine,buff2);
                      start=k+1;
                    }
                    k++;
                  }
                  pollfds[i].revents = 0;
               }
               else if(i<num_procs && pollfds[i+num_procs].revents & POLLIN){
                  read_from_socket(pollfds[i+num_procs].fd,buffer,sizeof(buffer));
                  while (buffer[k]!='\0'){
                    if (buffer[k]=='\n'){
                      memset(buff2,0,MAX_STR);
                      strncpy(buff2,buffer+start,k-start+1);
                      printf("[Proc %i:%s:stderr] %s  \n",i,proc_array[i].connect_info.machine,buff2);
                      start=k;
                    }
                    k++;
                  }
                  pollfds[i+num_procs].revents = 0;
               }
               else if (pollfds[i].revents & POLLHUP){
                 stop++;
               }

            }
            if (stop==nfds){
              break;
            }
         }

      /* on attend les processus fils */

      /* on ferme les descripteurs proprement */
      for (i=0;i<num_procs;i++){
         close(fd_err[i]);
         close(fd_out[i]);
      }
      /* on ferme la socket d'ecoute */
      close(listen_fd);
  }
   exit(EXIT_SUCCESS);
}
