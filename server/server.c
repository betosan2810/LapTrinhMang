#include <arpa/inet.h>  // htons(), inet_addr()
#include <errno.h>      // errno
#include <netinet/in.h> // inet_addr(), bind()
#include <signal.h>     // signal()
#include <stdbool.h>    // bool
#include <stdio.h> 
#include <stdlib.h>     // strtol()
#include <string.h>     // bzero(), strlen(), strcmp(), strcpy(), strtok(), strrchr(), memcmp()
#include <sys/socket.h> // socket(), inet_addr(), bind(), listen(), accept(), recv(), send()
#include <sys/types.h>  // socket()
#include <unistd.h>     // close()
#include <ctype.h>
#include <sys/stat.h>   //mkdir()
#include <sys/wait.h>   //waitpid();
#include <time.h>       // time(), localtime(), strftime()

#include "account.h"

#define MaxClient 20
#define MAX 1024

int dem(char *s,char t);
void sig_chld(int signo);
int begin_with(const char *str, const char *pre);
int respond(int recfd, char response[]);

int main(int argc, const char *argv[]) {
  // check argument
 	if(argc != 2){
 		printf("Invalid argument\n\n");
 		return 0;
 	}

  char username[MAX],pass[MAX],folder[MAX], *reply;
  int bytes_sent, bytes_received;
  char filename[] = "account.txt";

  // Initialize Addresses
  int sock;
  struct sockaddr_in server_addr;
  bzero(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(argv[1]));
  server_addr.sin_addr.s_addr = INADDR_ANY;

  node_a *account_list = loadData(filename);

  // menu
  int chon = 0;
  do
  {
      printf("\n============MENU===========\n");
      printf("|1. Show all clients       |\n");
      printf("|2. Create a client        |\n");
      printf("|3. Update clients         |\n");
      printf("|4. Start server           |\n");
      printf("|5. Delete client          |\n");
      printf("|6. Exit                   |\n");
      printf("===========================\n");
      printf("Enter your choice: ");
      scanf("%d", &chon);
      switch(chon)
      {
          case 1:
            printf("\n");
            printf("Username\tPass\t\tFolder\n");
            for(node_a *p = account_list; p != NULL; p = p->next){
                printf("%s\t\t%s\t\t%s\n", p->username, p->pass, p->folder);
            }
            printf("\n");  
            break;
          case 2:
          {
            while (getchar() != '\n');
            printf("New Client Username: ");
            fgets(username,MAX,stdin);
            username[strcspn(username,"\n")] = '\0';
            while (findNode(account_list,username) != NULL) {
              printf("Client existed. Please try again\n");
              printf("New Client Username: ");
              fgets(username,MAX,stdin);
              username[strcspn(username,"\n")] = '\0';
            }        
            
            printf("New Client Password: ");
            fgets(pass,MAX,stdin);
            pass[strcspn(pass,"\n")] = '\0';

            int c = 0;
            do
            {
              printf("New Client Folder Name: ");
              fgets(folder,MAX,stdin);
              folder[strcspn(folder,"\n")] = '\0';

              errno = 0;
              int ret = mkdir(folder, S_IRWXU);
              if (ret == -1) {
                switch (errno) {
                  case EACCES :
                      printf("The root directory does not allow write. ");
                      break;
                  case EEXIST:
                      printf("Folder %s already exists. \n%s used for client.",folder, folder);
                      c = 1;
                      break;
                  case ENAMETOOLONG:
                      printf("Pathname is too long");
                      break;
                  default:
                      printf("mkdir");
                      break;
                }
              }
              else
              {
                printf("Created: %s\n", folder);
                printf("Folder %s is created",folder);
                c = 1;
              }
            } while (c == 0);
            
            account_list = AddTail(account_list,username,pass,folder);
            saveData(account_list,filename);
            printf("\n");
          }
            break;
          case 3:
          {
            while (getchar() != '\n');
            printf("Client Username: ");
            fgets(username,MAX,stdin);
            username[strcspn(username,"\n")] = '\0'; 
            node_a *found = findNode(account_list,username);
            if (found == NULL)
            {
              printf("Username \"%s\" not exist!\n",username);
            }
            else
            {
              account_list = updateNode(account_list,found);
              saveData(account_list,filename);
            }
          }   
            break;
          
          case 5:
              while (getchar() != '\n');
              printf("Client Username: ");
              fgets(username,MAX,stdin);
              username[strcspn(username,"\n")] = '\0';
              if (findNode(account_list,username) == NULL)
              {
                printf("Username \"%s\" not exist!\n",username);
              }
              else if (account_list == findNode(account_list,username))
              {
                account_list = deleteHead(account_list);
                printf("Client \"%s\" deleted",username);
              }
              else
              {
                account_list = deleteAt(account_list,username);
                printf("Client \"%s\" deleted",username);
              }
              break;
          case 6:
              break;
          default:
              printf("Invalid choice. Please select again!\n");
              break;
      }
  }while(chon!=6); 
  free(account_list); 
  return 0;
}

int begin_with(const char *str, const char *pre) {
  size_t lenpre = strlen(pre), lenstr = strlen(str);
  return lenstr < lenpre ? 0 : memcmp(pre, str, lenpre) == 0;
}

int respond(int recfd, char response[]) {
  if ((send(recfd, response, strlen(response) + 1, 0)) == -1) {
    fprintf(stderr, "Can't send packet\n");
    return errno;
  }
  return 0;
}

void sig_chld(int signo){
	pid_t pid;
	int stat;
	while((pid = waitpid(-1, &stat, WNOHANG))>0)
		printf("\nChild %d terminated\n",pid);
}

int dem(char *s,char t)
{
 int dem=0;
 for(int i=0;i<=strlen(s);i++)
 {
 if(s[i]==t) dem=dem+1;  
 }
 return dem;
}
