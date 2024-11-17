#include <arpa/inet.h>  // htons(), inet_addr()
#include <errno.h>      // errno
#include <netinet/in.h> // inet_addr()
#include <stdbool.h>    // bool
#include <stdio.h>
#include <stdlib.h> // strtol()
#include <string.h> // bzero(), strlen(), strcmp(), strcpy(), strtok(), memcmp()
#include <sys/socket.h> // socket(), inet_addr(), connect(), recv(), send()
#include <sys/types.h>  // socket()
#include <unistd.h>     // close()
#include <sys/stat.h>
#include <time.h>       // time(), localtime(), strftime()

#define MAX 1024

int begin_with(const char *str, const char *pre) {
  size_t lenpre = strlen(pre), lenstr = strlen(str);
  return lenstr < lenpre ? 0 : memcmp(pre, str, lenpre) == 0;
}

int main(int argc, const char *argv[]) {
  if (argc != 3) {
    printf("Invalid argument\n\n");
    return 0;
  }

  // Create Socket
  int sock; // clientfd for client
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("Can't create socket");
    exit(errno);
  }

  // Initialize Address, Buffer, Path
  struct sockaddr_in server_addr;
  char buffer[1024];
  char buff[MAX];
  bzero(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(argv[2]));
  server_addr.sin_addr.s_addr = inet_addr(argv[1]);
  char username[MAX], password[MAX];
  int bytes_sent, bytes_received;
  char *path = malloc(2);
  strcpy(path, ".");
  int LOGIN = 0;

  // Connecting to Server
  if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
    perror("Connect Failed");
    close(sock);
    exit(errno);
  }

  while (LOGIN == 0) {
    puts("Please enter username and password: ");
    printf("Username: ");
    fgets(username, MAX, stdin);
    username[strcspn(username, "\n")] = '\0';

    // send username to server
    if (0 >= (bytes_sent = send(sock, username, strlen(username), 0))) {
      printf("\n Connection closed\n");
      return 0;
    }

    // receive server reply
    if (0 >= (bytes_received = recv(sock, buff, MAX - 1, 0))) {
      printf("\nError! Cannot receive data from server!\n");
      return 0;
    }
    buff[bytes_received] = '\0';

    if (strcmp(buff, "0") == 0) {
      puts("\nAccount not existed. Please try again\n");
      continue;
    }

    /* get password */
    printf("Password: ");
    fgets(password, MAX, stdin);
    password[strcspn(password, "\n")] = '\0';

    // send password to server
    if (0 >= (bytes_sent = send(sock, password, strlen(password), 0))) {
      printf("\nConnection closed!\n");
      return 0;
    }

    memset(buff, '\0', MAX);
    if (0 >= (bytes_received = recv(sock, buff, MAX - 1, 0))) {
      printf("\nError! Cannot receive data from server!\n");
      return 0;
    }
    buff[bytes_received] = '\0';

    if (strcmp(buff, "0") == 0) {
      puts("\nPassword is not correct. Please try again\n");
      continue;
    } else if (strcmp(buff, "1") == 0) {
      puts("\nLogin is successful\n");
    }
    LOGIN = 1;
  }

  printf("Session Terminated\n");
  close(sock);
  free(path);
}
