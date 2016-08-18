//Progetto di Laboratorio di sistemi operativi
//Lorenzo De Simone N86/1008

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>

int **scalo;
int N,T,collisioni_poss=0,collisioni_reg=0;
pthread_mutex_t acc_lista_in  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t acc_lista_tot = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t acc_comandi   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t acc_sveglia    = PTHREAD_COND_INITIALIZER;

//Struttura del nodo della lista di ingresso.
struct nodo
{
   int    info;
   struct nodo *link;
};

//Struttura del nodo della lista contenente tutti i container
//e se essi si sono spostati nel round corrente oppure no.
struct nodo_tot
{
   int info;
   int spostato; 
   int sock;
   struct nodo_tot *link;
};

//Struttura in input al thread gestore.
struct info_client
{
  int sd;
  char ip[16];
};

//Variabili globali delle due liste.
struct nodo* lista_in;
struct nodo_tot* lista_tot;

//Funzioni per la gestione della lista di ingresso.
struct nodo* inserisci_lista(struct nodo* head,int socket);
struct nodo* elimina_lista(struct nodo* head,int socket);

//Funzioni per la gestione della lista totale
struct nodo_tot* inserisci_lista_tot(struct nodo_tot* head, int socket,int clock);
struct nodo_tot* elimina_lista_tot(struct nodo_tot* head,int socket,int rig,int col);
void aggiorna_lista_tot(struct nodo_tot* head,int socket);
void resetta_lista_tot(struct nodo_tot* head);

//Funzione che conta gli elementi contenuti in una lista
//server per ottenere le info da mandare al client in risposta al
//comando ST.
int conta_lista();

//Thread che gestisce singolarmente un client.
void *gestore(void *info);

//Thread che scandisce il passare dei round.
void *sveglia();

//Funzione che analizza le possibili collisioni sulla matrice 
//prendendo in input le coordinate del container.
char *pre_collisione(int rig, int col);

//Handler per la gestione del segnale SIGPIPE
void hand();

int main (int argc, char *argv[])
{
  pthread_t tid;
  int sd,cd,i;
  socklen_t clilen;
  char temp[100];
  struct sockaddr_in server,client;
  struct info_client info;
 
//Gestione del segnale SIGPIPE: è necessario in caso di disconnessione 
//anomala del client mentre ha ancora comandi da gestire nel buffer input.
  signal(SIGPIPE,hand);

//Inizializzazione della struttura sockaddr_in.
  server.sin_family=AF_INET;
  server.sin_port=htons(5200);
  server.sin_addr.s_addr=htonl(INADDR_ANY);

  sd=socket(AF_INET,SOCK_STREAM,0);

//Controllo sul numero di input da linea di comando.
  if(argc<3||argc>3)
  {
    write(STDERR_FILENO,"Non sono stati passati in input due parametri\n",46*sizeof(char)); 
    exit(-1);
  }
//Leggo gli input da linea di comando e li assegno alle variabili N e T.
  sscanf(argv[1],"%i",&N);
  sscanf(argv[2],"%i",&T);

//Controlli sulla validità degli input da linea di comando.
  if(N<=1)
  {
    write(STDERR_FILENO,"Lo scalo deve essere almeno 2x2\n",32*sizeof(char));
    exit(-1);
  } 
  else if(T<=0)
  {
    write(STDERR_FILENO,"Il round deve essere di almeno 1 secondo\n",41*sizeof(char)); 
    exit(-1);
  }

//Allocazione dinamica della matrice che rappresenta lo scalo.
  scalo=(int**)calloc(N,sizeof(int*));

  for(i=0;i<N;i++)
  {
    scalo[i]=(int*)calloc(N,sizeof(int));
  }

//Verifica di errori sulla connessione.
  if((bind(sd,(struct sockaddr*)&server,(socklen_t)sizeof(server))==-1))
  {
    write(STDERR_FILENO,"\nSi è verificato un errore nell'esecuzione della bind\n",55*sizeof(char));
    exit(-1);
  }
  else
  { 
//Messaggio di benvenuto.
    write(STDOUT_FILENO,"\n||Sistema di monitoraggio della movimentazione merci||\n\n",57*sizeof(char));      
    pthread_create(&tid,NULL,sveglia,NULL); 
    do
    {      
      if((listen(sd,2))==-1)
      {
        write(STDERR_FILENO,"\nErrore nell'esecuzione della listen\n",37*sizeof(char));
        exit(-2);
      }
      else
      {
        clilen=sizeof(struct sockaddr);
        if((cd=accept(sd,(struct sockaddr*)&client,&clilen))==-1)
        {
          write(STDERR_FILENO,"\nErrore nell'esecuzione dell'accept\n",36*sizeof(char));
          exit(-3);
        }
        else
        {
          sprintf(temp,"_________________________________________\n\nConnessione stabilita con il client [%d]\n",cd);
          write(STDOUT_FILENO,temp,strlen(temp)*sizeof(char));
//Riempio la struttura da passare al thread gestore
//contenente ip e socket descriptor.
          strcpy(info.ip,inet_ntoa(client.sin_addr));
          info.sd=cd;
          pthread_create(&tid,NULL,gestore,(void*)&info);
        }
      }
    }while(1);
  }
}

void *gestore(void *info)
{
  int l,i,j,nwcd,clock,conta,rig=0,col=0,flag=0;
  char *stato;
  char comando[3],porta[6],ip[16],curr[100],messaggio[10000];
  struct sockaddr_in client;
  struct nodo *temp;
  struct info_client *new_info;
  
  stato=malloc(sizeof(char)*2);

  new_info= (struct info_client*) info;
  nwcd=new_info->sd;
  strcpy(ip,new_info->ip);
 
//Leggo la porta su cui connettere la socket secondaria dalla socket primaria.
  read(nwcd,&porta,6*sizeof(char));

//Riempio la struct client con le info ottenute.
  client.sin_family=AF_INET;
  client.sin_port=htons(atoi(porta));//Porta.
  inet_aton(ip,&client.sin_addr);//IP.
  sprintf(curr,"Porta ricevuta dal client: [%s]\nIP del client: [%s]\n_________________________________________\n",porta,ip);
  write(STDOUT_FILENO,curr,strlen(curr)*sizeof(char));

  clock=socket(AF_INET,SOCK_STREAM,0);

  if(clock<0)
  {
    write(STDERR_FILENO,"Errore socket\n",14*sizeof(char));
    pthread_exit(NULL);
  }  
  
  if((connect(clock,(struct sockaddr *) &client,sizeof(client)))<0)
  {
    write(STDERR_FILENO,"Errore di connessione al client\n",32*sizeof(char));
    pthread_exit(NULL);
  }

  pthread_mutex_lock(&acc_lista_in);
  lista_in=inserisci_lista(lista_in,nwcd);
  pthread_mutex_unlock(&acc_lista_in);  
  pthread_mutex_lock(&acc_lista_tot);
  lista_tot=inserisci_lista_tot(lista_tot,nwcd,clock);
  pthread_mutex_unlock(&acc_lista_tot);
   
  do
  { 
//Il server aggiorna lo stato del container.   
    stato=pre_collisione(rig,col);  
//Scrive K oppure P su socket clock.
    write(clock,stato,2*(sizeof(char)));
//Il server riceve da socket il comando del client.   
    if((read(nwcd,comando,3*sizeof(char)))==0)
    {
//Questo blocco di codice serve unicamente nel caso in cui il client sia 
//terminato in modo anomalo(es. CTRL+C,shutdown hardware,problemi di connessione): 
//il valore di ritorno 0 della read indica una socket broken.
//Si procede quindi all'eliminazione del container dallo scalo per
//preservare la coerenza dei dati all'interno del server e alla chiusura del thread gestore.
      sprintf(curr,"Disconnessione anomala del client [%d] dall'IP [%s]\n",nwcd,ip);
      write(STDERR_FILENO,curr,strlen(curr)*sizeof(char));
      pthread_mutex_lock(&acc_lista_tot);
      lista_tot=elimina_lista_tot(lista_tot,nwcd,rig,col);
      pthread_mutex_unlock(&acc_lista_tot);
      if(flag==0)//Verifica se il container sta uscendo dall'ingresso dello scalo.
      {
        pthread_mutex_lock(&acc_lista_in);
        lista_in=elimina_lista(lista_in,nwcd);
        pthread_mutex_unlock(&acc_lista_in);
      }
      else
      {
        pthread_mutex_lock(&acc_comandi);
        scalo[rig][col]=0;
        pthread_mutex_unlock(&acc_comandi);
      }
      pthread_exit(NULL);
    }
//Si aggiorna la lista totale segnalando che il client ha mandato un messaggio al server
//in questo round.
    pthread_mutex_lock(&acc_lista_tot);
    aggiorna_lista_tot(lista_tot,nwcd);
    pthread_mutex_unlock(&acc_lista_tot);

//il thread cerca di acquisite il lock e attende lo sblocco da parte della sveglia.
    pthread_mutex_lock(&acc_comandi);
    pthread_cond_wait(&acc_sveglia,&acc_comandi);
    
//Se il thread ha subito una collisione, si chiude e libera il lock.
    if((nwcd!=scalo[rig][col])&&((rig!=0)||(col!=0)))
    {
      pthread_mutex_unlock(&acc_comandi);
      pthread_exit(NULL);
    }
    
//Di seguito nei vari rami dell'if c'è il codice relativo al singolo comando:
//nel caso di collisioni o uscita dallo scalo viene modificata la stringa stato 
//e vengono inviati alla socket clock del client gli opportuni messaggi.
//Vengono inoltre gestiti gli spostamenti non validi con opportuni messaggi d'errore.
    if(strcmp(comando,"UP")==0)
    {  
      if(((rig-1)>=0)&&(((rig-1)!=0)||(col!=0)))
      { 
        if(scalo[rig-1][col]!=0)
        {
          sprintf(stato,"C");
          sprintf(curr,"Il container [%d] si è scontrato con il container [%d]\n",nwcd,scalo[rig-1][col]);
          write(STDOUT_FILENO,curr,strlen(curr)*sizeof(char));
          pthread_mutex_lock(&acc_lista_tot);
          lista_tot=elimina_lista_tot(lista_tot,scalo[rig-1][col],rig-1,col);
          lista_tot=elimina_lista_tot(lista_tot,nwcd,rig,col);
          pthread_mutex_unlock(&acc_lista_tot);
        }
        else
        {
          scalo[rig][col]=0;
          scalo[--rig][col]=nwcd;
        }
        write(nwcd,"Spostamento effettuato\n",23*sizeof(char));
      }
      else 
        write(nwcd,"Il carico non può essere spostato nella locazione di sopra\n",60*sizeof(char));
    } 
    else if (strcmp(comando,"DN")==0)
    {
      if((rig+1)<N)
      { 
        if(scalo[rig+1][col]!=0)
        {
          sprintf(stato,"C");
          sprintf(curr,"Il container [%d] si è scontrato con il container [%d]\n",nwcd,scalo[rig+1][col]);
          write(STDOUT_FILENO,curr,strlen(curr)*sizeof(char));
          pthread_mutex_lock(&acc_lista_tot);
          lista_tot=elimina_lista_tot(lista_tot,scalo[rig+1][col],rig+1,col);
          lista_tot=elimina_lista_tot(lista_tot,nwcd,rig,col);
          pthread_mutex_unlock(&acc_lista_tot);
        }
        else
        {      
          scalo[rig][col]=0;
          if(rig+1==N-1&&col==N-1)
          {
            sprintf(stato,"F");
            sprintf(curr,"Il container [%d] è uscito regolarmente dallo scalo\n",nwcd);
            write(STDOUT_FILENO,curr,strlen(curr)*sizeof(char));
            pthread_mutex_lock(&acc_lista_tot);
            lista_tot=elimina_lista_tot(lista_tot,nwcd,rig+1,col);
            pthread_mutex_unlock(&acc_lista_tot);
          }
          else 
            scalo[++rig][col]=nwcd;
        }
        if(flag==0)//Verifica se il container sta uscendo dall'ingresso dello scalo.
        {
          pthread_mutex_lock(&acc_lista_in);
          lista_in=elimina_lista(lista_in,nwcd);
          pthread_mutex_unlock(&acc_lista_in);
          flag=1;
        }
        write(nwcd,"Spostamento effettuato\n",23*sizeof(char));
      }
      else
        write(nwcd,"Il carico non può essere spostato nella locazione di sotto\n",60*sizeof(char));
    }
    else if (strcmp(comando,"SX")==0)
    {
      if(((col-1)>=0)&&(((col-1)!=0)||(rig!=0)))
      {
        if(scalo[rig][col-1]!=0)
        {
          sprintf(stato,"C");
          sprintf(curr,"Il container [%d] si è scontrato con il container [%d]\n",nwcd,scalo[rig][col-1]);
          write(STDOUT_FILENO,curr,strlen(curr)*sizeof(char));
          pthread_mutex_lock(&acc_lista_tot);
          lista_tot=elimina_lista_tot(lista_tot,scalo[rig][col-1],rig,col-1);
          lista_tot=elimina_lista_tot(lista_tot,nwcd,rig,col);
          pthread_mutex_unlock(&acc_lista_tot);      
        }
        else
        {
          scalo[rig][col]=0;
          scalo[rig][--col]=nwcd;
        }
        write(nwcd,"Spostamento effettuato\n",23*sizeof(char));
      }
      else
        write(nwcd,"Il carico non può essere spostato nella locazione di sinistra\n",63*sizeof(char));
    }
    else if (strcmp(comando,"DX")==0)
    {
      if((col+1)<N)
      {
        if(scalo[rig][col+1]!=0)
        {
          sprintf(stato,"C");
          sprintf(curr,"Il container [%d] si è scontrato con il container [%d]\n",nwcd,scalo[rig][col+1]);
          write(STDOUT_FILENO,curr,strlen(curr)*sizeof(char));
          pthread_mutex_lock(&acc_lista_tot);
          lista_tot=elimina_lista_tot(lista_tot,scalo[rig][col+1],rig,col+1);
          lista_tot=elimina_lista_tot(lista_tot,nwcd,rig,col);
          pthread_mutex_unlock(&acc_lista_tot);
        }
        else
        { 
          scalo[rig][col]=0;
          if(rig==N-1&&col+1==N-1)
          {
            sprintf(stato,"F");
            sprintf(curr,"Il container [%d] è uscito regolarmente dallo scalo\n",nwcd);
            write(STDOUT_FILENO,curr,strlen(curr)*sizeof(char));
            pthread_mutex_lock(&acc_lista_tot);
            lista_tot=elimina_lista_tot(lista_tot,nwcd,rig,col+1);
            pthread_mutex_unlock(&acc_lista_tot);
          }
          else
            scalo[rig][++col]=nwcd;
        }
        if(flag==0)//Verifica se il container sta uscendo dall'ingresso dello scalo.
        {
          pthread_mutex_lock(&acc_lista_in);
          lista_in=elimina_lista(lista_in,nwcd);
          pthread_mutex_unlock(&acc_lista_in);
          flag=1;
        }
        write(nwcd,"Spostamento effettuato\n",23*sizeof(char));
      } 
      else
        write(nwcd,"Il carico non può essere spostato nella locazione di destra\n",61*sizeof(char));
    }
    else if (strcmp(comando,"PO")==0)
    {
      sprintf(messaggio,"Container [%d] in posizione[%d][%d]\n",nwcd,rig,col);
      l=strlen(messaggio);
      messaggio[l]='\0';
      write(nwcd,messaggio,l*sizeof(char));
    }
    else if (strcmp(comando,"ST")==0)
    {  
//Dimensione dello scalo.
      sprintf(messaggio,"\nDimensione dello scalo: %d\n",N);

//Intervallo tra due round successivi.
      sprintf(curr,"Intervallo tra due round successivi: %d secondi\n",T);
      strcat(messaggio,curr);

//Tutti i container in posizione [0][0].    
      pthread_mutex_lock(&acc_lista_in);
      conta=0;
      if(lista_in!=NULL)
      {
        sprintf(curr,"Tutti i container in posizione [0][0]:\n");
        strcat(messaggio,curr);
        temp=lista_in;
        while(temp!=NULL)
        {
          sprintf(curr,"Container[%d]\n",temp->info);
          strcat(messaggio,curr);
          conta=conta+1;
          temp=temp->link;
        }
      }
      else
      {
        sprintf(curr,"Nessun container in posizione [0][0]\n");
        strcat(messaggio,curr);
      }  

//Posizione di tutti i container nel resto dello scalo.
      collisioni_poss=0;
      if(lista_in!=NULL)
        pre_collisione(0,0);

      pthread_mutex_unlock(&acc_lista_in);
      sprintf(curr,"Posizione di tutti i container nel resto dello scalo: \n");
      strcat(messaggio,curr);
      
      for(i=0;i<N;i++)
      {
        for(j=0;j<N;j++)
        {
          if(scalo[i][j]!=0) 
          {
            sprintf(curr,"Container [%d] in posizione: [%d][%d]\n",scalo[i][j],i,j);
            strcat(messaggio,curr);
            conta=conta+1;
            pre_collisione(i,j);
          }
        }
      }

//Numero di container presenti nello scalo.
      sprintf(curr,"Numero di container presenti nello scalo: %d\n",conta);
      strcat(messaggio,curr);

//Numero di possibili collisioni individuate.
      sprintf(curr,"Numero di possibili collisioni individuate: %d\n",collisioni_poss/2);
      strcat(messaggio,curr);

//Numero di collisioni registrate.
      sprintf(curr,"Numero di collisioni registrate: %d\n\n",collisioni_reg);
      strcat(messaggio,curr);
      write(nwcd,messaggio,strlen(messaggio)*sizeof(char));       
    } 
                 
//Nel caso in cui lo stato è "C", viene aumentato il contatore delle collisioni e 
//il thread termina dopo aver liberato il lock.
//Nel caso in cui lo stato è "F", il thread termina e libera il lock.
//Nel caso generale, viene semplicemente liberato il lock.
    if(strcmp(stato,"C")==0)
    {
      collisioni_reg=collisioni_reg+1;
      pthread_mutex_unlock(&acc_comandi);
      pthread_exit(NULL);
    }
    else if(strcmp(stato,"F")==0)
    {
      pthread_mutex_unlock(&acc_comandi);
      pthread_exit(NULL);
    } 
    pthread_mutex_unlock(&acc_comandi);
    
  }while(1);
}

void *sveglia()
{
  struct nodo_tot* temp;
  char round[20];
  //Numero di round passati. Viene inizializzata a 0.
  int n_round=0;

 do
   {
     sleep(T);
     sprintf(round,"Fine round [%d]\n",++n_round); 
     write(STDOUT_FILENO,round,strlen(round)*sizeof(char));
     pthread_mutex_lock(&acc_lista_tot);
     temp=lista_tot;

//Manda il messaggio F a tutti i client che non hanno mandato messaggi.
     while(temp!=NULL)
     {
       if(temp->spostato==0)
         write(temp->sock,"R\0",2*sizeof(char));
       temp=temp->link;
     }
     pthread_mutex_unlock(&acc_lista_tot);

//Acquisisce il lock per l'accesso ai comandi, segnala l'inizio del round e
//rilascia il lock.
     pthread_mutex_lock(&acc_comandi);
     pthread_cond_broadcast(&acc_sveglia);
     pthread_mutex_unlock(&acc_comandi);

//Resetta lo spostamento di tutti i container a 0.
     pthread_mutex_lock(&acc_lista_tot);
     resetta_lista_tot(lista_tot);
     pthread_mutex_unlock(&acc_lista_tot);

   }while(1);
   
}

//Inserisce un nuovo container nella lista d'ingresso.
struct nodo* inserisci_lista(struct nodo* head, int socket)
{
  struct nodo *nuovo,*temp;
  nuovo=malloc(sizeof(struct nodo));
  nuovo->info=socket;
  nuovo->link=NULL;
  
  if(head==NULL)
    head=nuovo;  
  else
  {
    temp=head;
    while(temp->link!=NULL)
    {
      temp=temp->link;
    }
    temp->link=nuovo;
  }
  return(head);
}

//Elimina un nodo dalla lista d'ingresso.
struct nodo* elimina_lista(struct nodo* head,int socket)
{
  struct nodo *temp,*prec;
  temp=head;
  prec=temp;
  
  while((temp!=NULL)&&(temp->info!=socket))
  {
   prec=temp;
   temp=temp->link;
  }
  if(head->info==socket)
    head=head->link;
  else
    prec->link=temp->link;

  free(temp);
  return(head);  
}

//Controlla se ci sono container nelle zone adiacenti
//e restituisce il nuovo stato.
char *pre_collisione(int rig, int col)
{
  char *stato; 
  stato=malloc(2*(sizeof(char)));
  sprintf(stato,"K");

  if(col!=N-1)//Controllo a destra(DX).
  {
    if(scalo[rig][col+1]!=0)     
    {	
      sprintf(stato,"P");
      collisioni_poss=collisioni_poss+1;
    }
  }
  
  if(rig!=N-1)//Controllo giù(DN).
  {
    if(scalo[rig+1][col]!=0)
    {
      sprintf(stato,"P");
      collisioni_poss=collisioni_poss+1;
    }
  }
                             
  if(rig-1>=0)//Controllo su(UP).
  {
    if((scalo[rig-1][col]!=0)||((rig-1==0&&col==0)&&(lista_in!=NULL)))  
    {
      sprintf(stato,"P");
      collisioni_poss=collisioni_poss+1;
    }
  }

  if(col-1>=0)//Controllo a sinistra(SX).
  {
    if((scalo[rig][col-1]!=0)||((rig==0&&col-1==0)&&(lista_in!=NULL)))
    {
      sprintf(stato,"P");
      collisioni_poss=collisioni_poss+1;
    }
  }
  return stato;
}

//Inserisce un nuovo nodo nella lista totale.
struct nodo_tot* inserisci_lista_tot(struct nodo_tot* head,int socket,int clock)
{
  struct nodo_tot *nuovo,*temp;

  nuovo=malloc(sizeof(struct nodo_tot));
  nuovo->info=socket;
  nuovo->link=NULL;
  nuovo->spostato=0;
  nuovo->sock=clock;

  if(head==NULL)
    head=nuovo;
  else
  {
    temp=head;
    while(temp->link!=NULL)
    {
      temp=temp->link;
    }
    temp->link=nuovo;
  }
  return(head);
}

//Elimina un nodo dalla lista totale e scrive l'avvenuta collisione sul socket clock del container.
//Viene inoltre eseguita una close() su entrambe le socket legate al container.
struct nodo_tot* elimina_lista_tot(struct nodo_tot* head,int socket,int rig, int col)
{
  struct nodo_tot *temp,*prec;

  temp=head;
  prec=temp;

  while((temp!=NULL)&&(temp->info!=socket))
  {
   prec=temp;
   temp=temp->link;
  }
  
  if(temp!=NULL)
  {
    if((rig==N-1)&&(col==N-1))
      write(temp->sock,"F\0",2*sizeof(char));
    else
      write(temp->sock,"C\0",2*sizeof(char));

    close(temp->info);
    close(temp->sock);
  }

  if((head!=NULL)&&(head->info==socket))
    head=head->link;
  else if(temp!=NULL)
  {
    prec->link=temp->link;
    free(temp);
  }
   
  scalo[rig][col]=0;

  return(head);
}

//Setta ad 1 lo spostamento del container dato in input.
void aggiorna_lista_tot(struct nodo_tot* head,int socket)
{
  struct nodo_tot *temp;
  temp=head;

  while((temp!=NULL)&&(temp->info!=socket))
  {
   temp=temp->link;
  }
  if(temp!=NULL)
    temp->spostato=1;        
}

//Resetta tutta gli spostamenti dei container settandoli a 0.
void resetta_lista_tot(struct nodo_tot* head)
{
   struct nodo_tot* temp;
   temp=head;
   
   while(temp!=NULL)
   {
     temp->spostato=0;
     temp=temp->link;
   }
}

//Handler utile nel remoto caso in cui il client si disconnette in modo anomalo
//mentre ha ancora comandi da gestire nel buffer input.
void hand ()
{
  write(STDERR_FILENO,"Alcuni comandi del client non sono stati gestiti\n",49*sizeof(char));
}
