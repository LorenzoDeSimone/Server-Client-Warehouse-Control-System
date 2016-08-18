//Progetto di Laboratorio di sistemi operativi
//Lorenzo De Simone N86/1008

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>

//Funzione che controlla la validità dei comandi.
int controllo(char *cmd);

//Funzione sveglia.
void *sveglia();

//Handler per il messaggio di fine round.
void hand(int segnale);

//Socket descriptor della socket per i messaggi.
int sd;  
 
//Socket descriptor della socket per la sveglia.
int new_sd;

//Tid del thread principale.
pthread_t main_t;

int main(int argc, char *argv[])
{
  int l,test;
  char comando[3],porta[6],messaggio[10000];
  struct sockaddr_in server,client;
  socklen_t len;
  pthread_t tid;
  
//Controllo sul numero di input da linea di comando.
  if(argc<3||argc>3)
  {
    write(STDERR_FILENO,"Non sono stati passati in input due parametri\n",46*sizeof(char)); 
    exit(-1);
  }
//Assegno alla variabile main il valore del tid del thread principale.
  main_t=pthread_self();
//L'handler hand viene associato al segnale SIGUSR1 e al segnale SIGUSR2.
  signal(SIGUSR1,hand);
  signal(SIGUSR2,hand);
  
//Inizializzazione della struttura sockaddr_in.
  server.sin_family=AF_INET;
  server.sin_port=htons(atoi(argv[2]));
  inet_aton(argv[1],&server.sin_addr);
  sd=socket(AF_INET,SOCK_STREAM,0);
  
  if(connect(sd,(struct sockaddr *)&server,(socklen_t) sizeof(server))==-1)
  {
     write(STDERR_FILENO,"\nErrore di connessione al server\n",33*sizeof(char));
     exit(-1);
  }
  else
  {
//Prima Connessione avvenuta con successo.
//Creazione della seconda socket per la sveglia.
    
    new_sd=socket(AF_INET,SOCK_STREAM,0);
    if(new_sd<0)
    {
       write(STDERR_FILENO,"Errore nella creazione della seconda socket\n",44*sizeof(char));
       exit(-1);
    }
    
    len=(socklen_t)sizeof(client);
    listen(new_sd,1);

//Niente bind: verrà assegnata una porta effimera.
    if(getsockname(new_sd,(struct sockaddr*) &client, &len)<0)
    {
      write(STDERR_FILENO,"Errore recupero numero porta\n",29*sizeof(char));
      exit(-1);    
    } 
//Stringa con la porta.
    sprintf(porta,"%d",ntohs(client.sin_port));
    
//Scrittura  della porta su socket.
    write(sd,&porta,(strlen(porta)+1)*sizeof(char));
   
//Creo il thread sveglia.
   pthread_create(&tid,NULL,sveglia,NULL);
 
    write(STDOUT_FILENO,"\nInserire i comandi da inviare al server\n",41*sizeof(char));
    do
    {   
      do
      { 
        l=read(STDIN_FILENO,comando,3*sizeof(char));//Lettura del comando da standard input.
        if(comando[l-1]=='\n')
          comando[l-1]='\0';
        else
          while(getchar()!='\n'); //Pulisce il buffer della tastiera in caso di input errato.   
//Legenda dei valori assumibili dalla funzione controllo():
//0=Valido e !=FM     
//1=Non valido
//2=FM
        test=controllo(comando);
      }while(test==1);
    
      if(test==0)//Comando da inviare al server.
      {
        write(sd,comando,l*sizeof(char)); 
        l=read(sd,messaggio,10000*sizeof(char));
        if(l!=0)
          write(STDOUT_FILENO,messaggio,l*sizeof(char));
      }
      else if(test==2)//Comando FM.
      {
        pause();//Attesa di SIGUSR1 dalla sveglia.
        write(STDOUT_FILENO,"Fine round\n",11*sizeof(char));
      }
    }while(1);
  }
}

//Viene controllata la validità del comando in input.
int controllo(char *cmd)
{
  if (strcmp(cmd,"UP")!=0)
    if (strcmp(cmd,"DN")!=0)
      if (strcmp(cmd,"SX")!=0)
        if (strcmp(cmd,"DX")!=0)
          if (strcmp(cmd,"ST")!=0)
           if (strcmp(cmd,"PO")!=0)
             if (strcmp(cmd,"FM")!=0)
             {            
               write(STDOUT_FILENO,"Comando non riconosciuto\n",25*sizeof(char));
               return(1);//Comando non valido.
             }
             else
               return(2);//Comando FM.

return(0);//Comando valido != FM.
}

void *sveglia()
{
   int clock;
   char mex[2];

//Accept sulla seconda socket.
   if((clock=accept(new_sd,NULL,NULL))==-1)
   {
     write(STDERR_FILENO,"Errore dell'accept nella sveglia\n",33*sizeof(char));
     exit(-1);
   }

   while(1)
   {
//Read dalla seconda socket: da qui si riceveranno
//aggiornamenti istantanei sullo stato del container.
//Nel caso in cui il client debba terminare la propria esecuzione,
//si eseguono delle close() sulle socket non più necessarie.
     if((read(clock,&mex,2*sizeof(char)))!=0)
     {
       if(strcmp(mex,"R")==0)
         pthread_kill(main_t,SIGUSR1); //Segnale di fine round.
       else if(strcmp(mex,"K")==0)
         write(STDOUT_FILENO,"Il container è in una posizione stabile\n",41*sizeof(char));
       else if(strcmp(mex,"P")==0)
         write(STDOUT_FILENO,"Il container è in procinto di collisione\n",42*sizeof(char));
       else if(strcmp(mex,"C")==0)
       {
         write(STDOUT_FILENO,"Il container è stato coinvolto in una collisione\n",50*sizeof(char));
         close(clock);
//Questa write serve solo a sbloccare la read del thread gestore 
//e metterlo in attesa del lock:
//in questo modo il server capirà che non si è trattata di una 
//disconnessione anomala ma di una collisione subita
         write(sd,"C\0",2*sizeof(char));
         close(sd);
         pthread_kill(main_t,SIGUSR2);
         pthread_exit(NULL);
       } 
       else if(strcmp(mex,"F")==0)
       {
         write(STDOUT_FILENO,"Il container è uscito regolarmente dallo scalo\n",48*sizeof(char));
         close(clock);
         close(sd);
         pthread_kill(main_t,SIGUSR2);
         pthread_exit(NULL);
       }
     }
     else
     {
//La read restituisce 0 e quindi la socket è broken:
//ciò è causato da una terminazione anomala del server.
       write(STDERR_FILENO,"Disconnessione anomala del server\n",34*sizeof(char));
       close(clock);
       close(sd);
       exit(-1);
     }   
   }
}

void hand(int segnale)
{
  if(segnale==SIGUSR2)
    exit(-1);
}
