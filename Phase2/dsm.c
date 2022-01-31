#include "dsm.h"

int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */

int MASTER_FD;
int DSMEXEC_FD;

int * sockets_array;    //Tableau pour stocker les sockets des autres processus

sem_t mutex;   //Sémaphore nécessaire à la synchro dsm_comm_daemon/dsm_handler
int proc_active;


/* indique l'adresse de debut de la page de numero numpage */
static char *num2address( int numpage ){
   char *pointer = (char *)(BASE_ADDR+(numpage*(PAGE_SIZE)));

   if( pointer >= (char *)TOP_ADDR ){
      fprintf(stderr,"[%i] Invalid address !\n", DSM_NODE_ID);
      return NULL;
   }
   else return pointer;
}

/* cette fonction permet de recuperer un numero de page */
/* a partir  d'une adresse  quelconque */
static int address2num( char *addr ){
   return (((intptr_t)(addr - BASE_ADDR))/(PAGE_SIZE));
}

/* cette fonction permet de recuperer l'adresse d'une page */
/* a partir d'une adresse quelconque (dans la page)        */
static char *address2pgaddr( char *addr ){
   return  (char *)(((intptr_t) addr) & ~(PAGE_SIZE-1));
}

/* fonctions pouvant etre utiles */
static void dsm_change_info( int numpage, dsm_page_state_t state, dsm_page_owner_t owner){
   if ((numpage >= 0) && (numpage < PAGE_NUMBER)) {
	if (state != NO_CHANGE )
	table_page[numpage].status = state;
      if (owner >= 0 )
	table_page[numpage].owner = owner;
      return;
   }
   else {
	fprintf(stderr,"[%i] Invalid page number !\n", DSM_NODE_ID);
      return;
   }
}

static dsm_page_owner_t get_owner( int numpage){
   return table_page[numpage].owner;
}

static dsm_page_state_t get_status( int numpage){
   return table_page[numpage].status;
}

/* Allocation d'une nouvelle page */
static void dsm_alloc_page( int numpage ){
   char *page_addr = num2address( numpage );
   mmap(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   return ;
}

/* Changement de la protection d'une page */
static void dsm_protect_page( int numpage , int prot){
   char *page_addr = num2address( numpage );
   mprotect(page_addr, PAGE_SIZE, prot);
   return;
}

static void dsm_free_page( int numpage ){
   char *page_addr = num2address( numpage );
   munmap(page_addr, PAGE_SIZE);
   return;
}

int dsm_recv(int fd, void *buf, int size){
	int ret = 0;
	int offset = 0;
	while (offset != size) {
		fflush(stdout);
		ret = read(fd, buf + offset, size - offset);
		if (-1 == ret) {
			perror("Reading");
			exit(EXIT_FAILURE);
		}
		if (0 == ret) {
			close(fd);
			return -1;
			break;
		}
		offset += ret;
	}
	return offset;
}

int dsm_send(int fd, void *buf, int size){
	int ret = 0, offset = 0;
	while (offset != size) {
		if (-1 == (ret = write(fd, buf + offset, size - offset))) {
			perror("Writing");
			return -1;
		}
		offset += ret;
	}
	return offset;
}


static void *dsm_comm_daemon( void *arg){
	fflush(stdout);
   printf("\n----DSM COMM DAEMON----\n");

   struct pollfd pollfds[DSM_NODE_NUM];
   int i;
   char addr[PAGE_SIZE];
   for (i=0;i<DSM_NODE_NUM;i++){
      pollfds[i].fd = sockets_array[i];
      pollfds[i].events = POLLIN;
      pollfds[i].revents = 0;
   }

   while(proc_active){           //Tant qu'on a des processus actifs on regarde nos ports
      int n_active = 0;
      if (-1 == (n_active = poll(pollfds, DSM_NODE_NUM, -1))) {
         perror("Poll");
      }
      for(i=0;i<DSM_NODE_NUM;i++){
         if (i!=DSM_NODE_ID && (pollfds[i].revents & POLLIN) == POLLIN){
            printf("Récéption de message de %i\n",i);
            printf("Lecture sur : %i\n",pollfds[i].fd);

            dsm_req_type_t request_type=-5;
            dsm_req_t request;

            dsm_recv(pollfds[i].fd,(void*)&request_type,sizeof(dsm_req_type_t));
            printf("Type de requete reçue : %i\n\n",request_type);
            dsm_recv(pollfds[i].fd,(void*)&request,sizeof(dsm_req_t));



            //switch case en fct du type de requete
            switch (request_type){
               case DSM_REQ:
                  memset(addr,0,PAGE_SIZE);
                  strcpy(addr,num2address(request.page_num));

                  request_type=DSM_PAGE;
                  printf("Envoi de la requete : %i\n\n",request_type);

                  dsm_send(sockets_array[(int) request.source],(void *)&request_type,sizeof(dsm_req_type_t));
                  dsm_send(sockets_array[(int) request.source],(void *)&request,sizeof(dsm_req_t));
                  dsm_send(sockets_array[(int) request.source],(void *)&addr,PAGE_SIZE);
                  dsm_free_page(request.page_num); // La page ne va plus lui appartenir
                  break;

               case DSM_PAGE:
                  memset(addr,0,PAGE_SIZE);
                  dsm_alloc_page(request.page_num);
                  strcpy(addr,num2address(request.page_num));
                  dsm_recv(pollfds[i].fd,(void *)&addr,PAGE_SIZE);
                  dsm_change_info( request.page_num, WRITE, (dsm_page_owner_t) DSM_NODE_ID);

                  request_type=DSM_NREQ;

                  request.source=DSM_NODE_ID;
                  //printf("Envoi de la requete : %i à tous les processus\n\n",request_type);
                  for (i=0;i<DSM_NODE_NUM;i++){
                     if (i!=DSM_NODE_ID){
                        dsm_send(sockets_array[i],(void *)&request_type,sizeof(dsm_req_type_t));
                        dsm_send(sockets_array[i],(void *)&request,sizeof(dsm_req_t));
                     }
                  }
                  sem_post(&mutex);
                  printf("Jeton rendu DSMNREQ\n");
                  break;

               case DSM_NREQ:
                  dsm_change_info(request.page_num,WRITE,(dsm_page_owner_t) request.source);
                  break;

               case DSM_FINALIZE:
                  proc_active--;
                  printf("proc_active=%i",proc_active);
                  break;
            }
            pollfds[i].revents = 0;
         }
      }
      sleep(2);
    }
   return NULL;
}


static void dsm_handler( void * addr){
   printf("-----DSM HANDLER-----\n");
   // en déduire le numéro de page concernée
   int numpage=address2num((char*)addr);
   // en déduire le processus actuellement propriétaire
   dsm_page_owner_t owner = get_owner(numpage);

   printf("Missing page number %i at owner %i\n",numpage, owner);
   printf("Socket sur laquelle envoyer : %i\n",sockets_array[(int) owner]);
   //envoyer une requête à ce processus pour qu’il envoie
   //la page au processus demandeur, qui devient donc
   //le nouveau propriétaire de la page mémoire concernée
   dsm_req_type_t request_type = DSM_REQ;

   dsm_req_t request;
   request.source=DSM_NODE_ID;
   request.page_num=numpage;

   printf("request_type init = %i\n",request_type);
   dsm_send(sockets_array[(int) owner], (void*)&request_type, sizeof(dsm_req_type_t));
   dsm_send(sockets_array[(int) owner], (void*)&request, sizeof(dsm_req_t));

   // sémaphore nécessaire pour éviter que le programme se termine
   printf("Attente jeton dsm_handler\n");
   sem_wait(&mutex);
   printf("Reception jeton dsm_handler\n");
}


/* traitant de signal adequat */
static void segv_handler(int sig, siginfo_t *info, void *context){
   /* adresse qui a provoque une erreur */
   void  *addr = info->si_addr;

  /* Si ceci ne fonctionne pas, utiliser a la place :*/
  /*
   #ifdef __x86_64__
   void *addr = (void *)(context->uc_mcontext.gregs[REG_CR2]);
   #elif __i386__
   void *addr = (void *)(context->uc_mcontext.cr2);
   #else
   void  addr = info->si_addr;
   #endif
   */
   /*
   pour plus tard (question ++):
   dsm_access_t access  = (((ucontext_t *)context)->uc_mcontext.gregs[REG_ERR] & 2) ? WRITE_ACCESS : READ_ACCESS;
  */
   /* adresse de la page dont fait partie l'adresse qui a provoque la faute */
   void  *page_addr  = (void *)(((unsigned long) addr) & ~(PAGE_SIZE-1));

   if ((addr >= (void *)BASE_ADDR) && (addr < (void *)TOP_ADDR)){
	   dsm_handler(addr);
   }

   else{
	/* SIGSEGV normal : ne rien faire*/
   }
}


int socket_and_connect(char *port,char *hostname){
	int sock_fd = -1;
	// Création de la socket
	if (-1 == (sock_fd = socket(AF_INET, SOCK_STREAM, 0))) {
		perror("Socket");
		exit(EXIT_FAILURE);
	}
	struct addrinfo hints, *res, *tmp;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICSERV;
	int error;
	error = getaddrinfo(hostname, port, &hints, &res);
	if (error) {
		errx(1, "%s", gai_strerror(error));
		exit(EXIT_FAILURE);
	}
	tmp = res;
	while (tmp != NULL) {
      if (tmp->ai_addr->sa_family == AF_INET) {
         while(-1 == connect(sock_fd, tmp->ai_addr, tmp->ai_addrlen) && errno==ECONNREFUSED){
         }
      }
      return sock_fd;
		tmp = tmp->ai_next;
   }
	return -1;
}


/* Seules ces deux dernieres fonctions sont visibles et utilisables */
/* dans les programmes utilisateurs de la DSM                       */
char *dsm_init(int argc, char *argv[]){
   dsm_proc_conn_t *connect_info = NULL;
   struct sigaction act;
   int index;

   /* Récupération de la valeur des variables d'environnement */
   /* DSMEXEC_FD et MASTER_FD                                 */
   DSMEXEC_FD = atoi(getenv("DSMEXEC_FD"));
   MASTER_FD = atoi(getenv("MASTER_FD"));
   printf("MASTER_FD=%i et DSMEXEC_FD=%i\n",MASTER_FD,DSMEXEC_FD);
   /* reception du nombre de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_NUM) */
   dsm_recv(DSMEXEC_FD,&DSM_NODE_NUM,sizeof(int));
   dsm_proc_t* proc_array=malloc(DSM_NODE_NUM*sizeof(dsm_proc_t));

   /* reception de mon numero de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_ID)      */
   dsm_recv(DSMEXEC_FD,&DSM_NODE_ID,sizeof(int));
   printf("DSM_NODE_ID=%i et DSM_NODE_NUM=%i\n",DSM_NODE_ID,DSM_NODE_NUM);
   /* reception des informations de connexion des autres */
   /* processus envoyees par le lanceur :                */
   /* nom de machine, numero de port, etc.               */
   printf("\n----Liste Machines----\n");
   int k;
   for(k = 0; k < DSM_NODE_NUM ; k++){
      dsm_recv(DSMEXEC_FD,&proc_array[k].connect_info,sizeof(dsm_proc_conn_t));
      printf("machine %s avec port %i\n",proc_array[k].connect_info.machine,proc_array[k].connect_info.port_num);
   }

   /* initialisation des connexions              */
   /* avec les autres processus : connect/accept */
   int i;
   sockets_array=malloc((DSM_NODE_NUM)*sizeof(int));  //La liste des fd sur lesquels il faudra recv
   memset(sockets_array,0,DSM_NODE_NUM*sizeof(int));
   printf("\n----Connexion----\n");

   /* on accepte les connexions des autres processus dsm */
   for (i=0;i<DSM_NODE_ID;i++){
      //Listen
      if (-1 == listen(MASTER_FD, 20)) {
         perror("Listen");
      }
      //Accept
      struct sockaddr proc_addr;
      memset(&proc_addr,0,sizeof(proc_addr));
      socklen_t size = sizeof(proc_addr);
      int socket_accept=-1;
      printf("Accepting...\n");
      while(socket_accept==-1){
         if (-1 == (socket_accept = accept(MASTER_FD, &proc_addr, &size))) {
            perror("Accept in dsm");
         }
         fflush(stdout);
      }
      printf("--> socket accept = %i\n",socket_accept);
      int proc_connected_id=-1;
      dsm_recv(socket_accept, (void *)&proc_connected_id, sizeof(int));
      printf("--> proc_connected_id = %i\n",proc_connected_id);
      sockets_array[proc_connected_id]=socket_accept;
   }

   /* on se connecte aux processus dsm */
   for (i=DSM_NODE_ID+1;i<DSM_NODE_NUM;i++){
      char * port=malloc(6*sizeof(char));
      sprintf(port,"%i",proc_array[i].connect_info.port_num);
      printf("Connexion à machine %s sur le port %s\n",proc_array[i].connect_info.machine,port);
      proc_array[i].connect_info.fd=socket_and_connect(port,proc_array[i].connect_info.machine);
      printf("--> Connexion réussie, fd = %i\n\n",proc_array[i].connect_info.fd);
      dsm_send(proc_array[i].connect_info.fd, (void *)&DSM_NODE_ID, sizeof(int));
      sockets_array[i]=proc_array[i].connect_info.fd;
   }
   sockets_array[DSM_NODE_ID]=-1;         //Valeur par défaut


   /* Allocation des pages en tourniquet */
   for(index = 0; index < PAGE_NUMBER; index ++){
      if ((index % DSM_NODE_NUM) == DSM_NODE_ID)
         dsm_alloc_page(index);
      dsm_change_info( index, WRITE, index % DSM_NODE_NUM);
   }

   /* mise en place du traitant de SIGSEGV */
   act.sa_flags = SA_SIGINFO;
   act.sa_sigaction = segv_handler;
   sigaction(SIGSEGV, &act, NULL);

   proc_active=DSM_NODE_NUM; //Variable pour terminer proprement tous les threads

   /* creation du thread de communication           */
   /* ce thread va attendre et traiter les requetes */
   /* des autres processus                          */
   sem_init(&mutex, 0, 0);
   pthread_create(&comm_daemon, NULL, dsm_comm_daemon, NULL);

   /* Adresse de début de la zone de mémoire partagée */
   return ((char *)BASE_ADDR);
}

void dsm_finalize( void ){
   printf("----DSM_FINALIZE----\n");
   //On envoie à chaque processus une requete pour dire qu'on s'est terminé
   dsm_req_type_t request_type = DSM_FINALIZE;

   dsm_req_t request;
   request.source=DSM_NODE_ID;
   request.page_num=0;

   printf("Envoi de DSM_FINALIZE à tous les processus\n");
   int i;
   for(i=0;i<DSM_NODE_NUM;i++){
      if(i!=DSM_NODE_ID){
         dsm_send(sockets_array[i], (void*)&request_type, sizeof(dsm_req_type_t));
         dsm_send(sockets_array[i], (void*)&request, sizeof(dsm_req_t));
      }
   }

   proc_active--;

   printf("proc_active=%i\n",proc_active);

   //Tant que tous les processus ne sont pas terminés
   while(1){
      sleep(0.5);
      if(proc_active==0){
         break;
      }
   }

   /* fermer proprement les connexions avec les autres processus */
   for(i=0;i<DSM_NODE_NUM;i++){
         if(i!=DSM_NODE_ID){
            close(sockets_array[i]);
         }
   }
   /* terminer correctement le thread de communication */
   /* pour le moment, on peut faire :                  */
   pthread_cancel(comm_daemon);


   printf("DSM_FINALIZE FAIT\n");
  return;
}
