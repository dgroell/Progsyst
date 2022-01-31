#include "common_impl.h"

int main(int argc, char **argv)
{
	/* processus intermediaire pour "nettoyer" */
	/* la liste des arguments qu'on va passer */
	/* a la commande a executer finalement  */
	int i;
	/* creation d'une socket pour se connecter au */
	/* au lanceur et envoyer/recevoir les infos */
	/* necessaires pour la phase dsm_init */
	int sock_launcher=socket_and_connect(argv[1],argv[2]);
	setbuf(stdout,NULL);
	printf("Connexion réussie\n");
	/* Envoi du nom de machine au lanceur */
	char hostname[MAX_STR];
	gethostname(hostname,sizeof(hostname));
	int size = strlen(hostname) + 1;
	printf("[DSMWRAP] hostname=%s,size=%i\n",hostname,size);
	write_int_size(sock_launcher, (void *)&size);
  	write_in_socket(sock_launcher, (void *)&hostname, size);


	/* Envoi du pid au lanceur (optionnel) */

	/* Creation de la socket d'ecoute pour les */
	/* connexions avec les autres processus dsm */
	int listen_dsm_fd = -1;
	char *port=malloc(8*sizeof(char));
	unsigned short newport=0;
	strcpy(port,"0");
	if (-1 == (listen_dsm_fd  = socket_listen_and_bind(&newport))) {
		printf("Could not create, bind and listen properly\n");
		return 1;
	}
	
	/* Envoi du numero de port au lanceur */
	/* pour qu'il le propage à tous les autres */
		/* processus dsm */
	printf("[DSMWRAP] port=%hu,size=%li\n",newport,sizeof(newport));
	write_in_socket(sock_launcher, (void *)&newport, sizeof(newport));

	//On stocke la valeur des fd des sockets dans des variables d'environnement
	char master_fd[MAX_STR];
	sprintf(master_fd,"%i",listen_dsm_fd);
	setenv("MASTER_FD",master_fd,1);

	char dsmexec_fd[MAX_STR];
	sprintf(dsmexec_fd,"%i",sock_launcher);
	setenv("DSMEXEC_FD",dsmexec_fd,1);

	/* on execute la bonne commande */
	/* attention au chemin à utiliser ! */
	char ** arg = malloc((argc -2)*sizeof(char));
	/*char * path=getenv("DSM_BIN");
	char * exec=malloc(MAX_STR*sizeof(char));
	sprintf(exec,"%s/%s",path,argv[3]);
	printf("path=%s\n",path);
	printf("arg=%s\n",exec);
	arg[0]=exec;*/
	for(i=0;i<argc-3;i++){
		arg[i]=argv[i+3];
	}
	arg[argc-2]=NULL;

	execvp(arg[0],arg);

	/************** ATTENTION **************/
	/* vous remarquerez que ce n'est pas   */
	/* ce processus qui récupère son rang, */
	/* ni le nombre de processus           */
	/* ni les informations de connexion    */
	/* (cf protocole dans dsmexec)         */
	/***************************************/

	return 0;
}
