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
#include <sys/stat.h> // mkdir()
#include <sys/wait.h> // waitpid()
#include <time.h>     // time(), localtime(), strftime()
#include <pthread.h>  // Thêm thư viện để quản lý nhiều client đồng thời
#include <dirent.h>   // Thêm thư viện này để sử dụng opendir, readdir, closedir

#include "account.h"

#define MaxClient 20
#define MAX 1024

// Struct để truyền dữ liệu cho mỗi thread

// Hàm xử lý client
void *handle_client(void *arg)
{
  char file[] = "account.txt";
  node_a *account_list = loadData(file);
  int client_sock = *(int *)arg;

  free(arg); // Giải phóng bộ nhớ đã cấp phát cho thread_data

  char buffer[MAX];
  int bytes_received;
  char username[MAX], password[MAX];
  int authenticated = 0;
  node_a *current_user = NULL; // Biến này lưu thông tin người dùng đã đăng nhập

  // Gửi thông báo kết nối thành công đến client
  const char *welcome_message = "Welcome to FTP Server!";
  send(client_sock, welcome_message, strlen(welcome_message), 0);

  // Lắng nghe lệnh từ client và phản hồi lại
  while (1)
  {
    bzero(buffer, MAX);
    bytes_received = recv(client_sock, buffer, MAX, 0);
    if (bytes_received <= 0)
    {
      break; // Nếu không nhận được dữ liệu, thoát khỏi vòng lặp
    }

    printf("Received command: %s\n", buffer);

    // Xử lý lệnh USER - yêu cầu tên người dùng
    if (strncmp(buffer, "USER", 4) == 0)
    {
      if (authenticated)
      {
        send(client_sock, "User already authenticated.", 27, 0);
      }
      else
      {
        sscanf(buffer, "USER %s", username);
        printf("Received USER: %s\n", username);
        send(client_sock, "Password please.", 17, 0);
      }
    }
    // Xử lý lệnh PASS - xác thực mật khẩu
    // Xử lý lệnh PASS - xác thực mật khẩu
    else if (strncmp(buffer, "PASS", 4) == 0)
    {
      if (authenticated)
      {
        send(client_sock, "Already logged in.", 19, 0);
      }
      else
      {
        sscanf(buffer, "PASS %s", password);
        printf("Received PASS: %s\n", password);

        // Kiểm tra thông tin đăng nhập từ danh sách tài khoản
        current_user = validate_user(account_list, username, password);
        if (current_user != NULL)
        {
          send(client_sock, "230 User logged in, proceed.", 27, 0);
          authenticated = 1;
        }
        else
        {
          send(client_sock, "530 Not logged in, invalid credentials.", 40, 0);
        }
      }
    }

    else if (strncmp(buffer, "PWD", 3) == 0)
    {
      if (authenticated && current_user)
      {
        char cwd[MAX];

        // Lấy thư mục làm việc hiện tại
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
          // Gửi thư mục làm việc cho client
          snprintf(buffer, sizeof(buffer), "257 \"%s\" is the current directory.", cwd);
          send(client_sock, buffer, strlen(buffer), 0);
        }
        else
        {
          // Nếu không thể lấy thư mục làm việc
          perror("getcwd failed");
          send(client_sock, "550 Failed to get current directory.", 35, 0);
        }
      }
      else
      {
        send(client_sock, "530 Please login first.", 23, 0);
      }
    }

    // Lệnh LIST - liệt kê các tệp trong thư mục người dùng
    else if (strncmp(buffer, "LIST", 4) == 0) {
    if (authenticated && current_user) {
        // Lấy thư mục của người dùng từ current_user
        char user_folder[MAX];
        snprintf(user_folder, sizeof(user_folder), "%s", current_user->folder);  // Lấy thư mục người dùng từ current_user

        // Chuyển tới thư mục của người dùng
        if (chdir(user_folder) != 0) {
            // Nếu không thể chuyển thư mục (do thư mục không tồn tại hoặc quyền truy cập sai)
            perror("Failed to change directory");
            send(client_sock, "550 Failed to change directory.", 31, 0);
        } else {
            // Mở thư mục của người dùng
            DIR *dir = opendir(".");
            if (dir == NULL) {
                perror("Failed to open directory");
                send(client_sock, "550 Failed to open directory.", 27, 0);
            } else {
                struct dirent *entry;
                char file_info[MAX];

                // Liệt kê các tệp tin và thư mục trong thư mục của người dùng
                while ((entry = readdir(dir)) != NULL) {
                    // Loại bỏ thư mục "." và ".."
                    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                        snprintf(file_info, sizeof(file_info), "%s\n", entry->d_name);
                        int bytes_sent = send(client_sock, file_info, strlen(file_info), 0);
                        if (bytes_sent == -1) {
                            perror("Error sending file info");
                            break;
                        }
                        bzero(file_info, MAX);  // Đảm bảo buffer không có dữ liệu thừa
                    }
                }
                closedir(dir);  // Đóng thư mục

                // Gửi thông báo kết thúc lệnh LIST
                send(client_sock, "226 Transfer complete.", 24, 0);
            }
        }
    } else {
        send(client_sock, "530 Please login first.", 23, 0);
    }
}
    
    // Lenh CWD
    else if (strncmp(buffer, "CWD", 3) == 0) {
    if (authenticated && current_user) {
        char path[MAX];
        sscanf(buffer, "CWD %s", path);

        // Lấy thư mục gốc của server
        char cwd[MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd failed");
            send(client_sock, "550 Failed to get current directory.", 35, 0);
            return;
        }

        // Kiểm tra giá trị của cwd và current_user->folder
        printf("CWD: %s\n", cwd);  // Debug
        printf("User folder: %s\n", current_user->folder);  // Debug

        // Xây dựng full_path để thay đổi thư mục
        char full_path[MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s/%s", cwd, current_user->folder, path);

        // In ra full_path để kiểm tra
        printf("Full path: %s\n", full_path);

        // Kiểm tra sự tồn tại của thư mục
        struct stat statbuf;
        if (stat(full_path, &statbuf) != 0) {
            perror("stat failed");
            send(client_sock, "550 Failed to change directory.", 31, 0);
            return;
        }

        // Kiểm tra thư mục có phải là thư mục không
        if (!S_ISDIR(statbuf.st_mode)) {
            send(client_sock, "550 Failed to change directory: Not a directory.", 47, 0);
            return;
        }

        // Thay đổi thư mục
        if (chdir(full_path) == 0) {
            send(client_sock, "250 Directory changed.", 22, 0);  // Thành công
        } else {
            perror("Failed to change directory");
            send(client_sock, "550 Failed to change directory.", 31, 0);
        }
    } else {
        send(client_sock, "530 Please login first.", 23, 0);  // Chưa đăng nhập
    }
}

    // Lệnh QUIT - ngắt kết nối
    else if (strncmp(buffer, "QUIT", 4) == 0)
    {
      send(client_sock, "221 Goodbye!", 12, 0);
      break;
    }
    // Lệnh không xác định
    else
    {
      send(client_sock, "500 Command not recognized.", 27, 0);
    }
  }

  // Đóng kết nối khi client ngắt
  close(client_sock);
  return NULL;
}

// Hàm khởi động server
void start_server(int port, node_a *account_list)
{
  int sockfd, client_sock, *new_sock;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_len;
  pthread_t client_thread;

  // Tạo socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("Unable to create socket");
    exit(1);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = INADDR_ANY;

  // Gắn địa chỉ và cổng vào socket
  if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    perror("Bind failed");
    close(sockfd);
    exit(1);
  }

  // Lắng nghe kết nối từ client
  if (listen(sockfd, MaxClient) < 0)
  {
    perror("Listen failed");
    close(sockfd);
    exit(1);
  }

  printf("Server started. Waiting for connections...\n");

  // Chấp nhận kết nối và tạo một thread mới để xử lý từng client
  while (1)
  {
    client_len = sizeof(client_addr);
    client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
    if (client_sock < 0)
    {
      perror("Accept failed");
      continue;
    }

    printf("Client connected.\n");

    // Tạo một thread mới để xử lý client
    new_sock = malloc(sizeof(int));
    *new_sock = client_sock;
    if (pthread_create(&client_thread, NULL, handle_client, (void *)new_sock) < 0)
    {
      perror("Could not create thread");
      close(client_sock);
    }
  }

  close(sockfd);
}

int main(int argc, const char *argv[])
{
  if (argc != 2)
  {
    printf("Invalid argument\n\n");
    return 0;
  }

  char username[MAX], pass[MAX], folder[MAX];
  int bytes_sent, bytes_received;
  char filename[] = "account.txt";

  // Initialize Addresses
  int sock;
  struct sockaddr_in server_addr;
  bzero(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(argv[1]));
  server_addr.sin_addr.s_addr = INADDR_ANY;

  node_a *account_list = loadData(filename); // Load data from the account file

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

    switch (chon)
    {
    case 1:
      printf("\n");
      printf("Username\tPass\t\tFolder\n");
      for (node_a *p = account_list; p != NULL; p = p->next)
      {
        printf("%s\t\t%s\t\t%s\n", p->username, p->pass, p->folder);
      }
      printf("\n");
      break;
    case 2:
    {
      while (getchar() != '\n')
        ;
      printf("New Client Username: ");
      fgets(username, MAX, stdin);
      username[strcspn(username, "\n")] = '\0';
      while (findNode(account_list, username) != NULL)
      {
        printf("Client existed. Please try again\n");
        printf("New Client Username: ");
        fgets(username, MAX, stdin);
        username[strcspn(username, "\n")] = '\0';
      }

      printf("New Client Password: ");
      fgets(pass, MAX, stdin);
      pass[strcspn(pass, "\n")] = '\0';

      int c = 0;
      do
      {
        printf("New Client Folder Name: ");
        fgets(folder, MAX, stdin);
        folder[strcspn(folder, "\n")] = '\0';

        errno = 0;
        int ret = mkdir(folder, S_IRWXU);
        if (ret == -1)
        {
          switch (errno)
          {
          case EACCES:
            printf("The root directory does not allow write. ");
            break;
          case EEXIST:
            printf("Folder %s already exists. \n%s used for client.", folder, folder);
            c = 1;
            break;
          case ENAMETOOLONG:
            printf("Pathname is too long");
            break;
          default:
            printf("mkdir failed\n");
            break;
          }
        }
        else
        {
          printf("Created: %s\n", folder);
          printf("Folder %s is created", folder);
          c = 1;
        }
      } while (c == 0);

      account_list = AddTail(account_list, username, pass, folder);
      saveData(account_list, filename); // Save updated data to the file
      printf("\n");
    }
    break;
    case 4:
      start_server(atoi(argv[1]), account_list); // Start server on port specified in argv[1]
      break;
    case 3:
    {
      while (getchar() != '\n')
        ;
      printf("Client Username: ");
      fgets(username, MAX, stdin);
      username[strcspn(username, "\n")] = '\0';
      node_a *found = findNode(account_list, username);
      if (found == NULL)
      {
        printf("Username \"%s\" not exist!\n", username);
      }
      else
      {
        account_list = updateNode(account_list, found);
        saveData(account_list, filename); // Save updated data to the file
      }
    }
    break;

    case 5:
      while (getchar() != '\n')
        ;
      printf("Client Username: ");
      fgets(username, MAX, stdin);
      username[strcspn(username, "\n")] = '\0';
      if (findNode(account_list, username) == NULL)
      {
        printf("Username \"%s\" not exist!\n", username);
      }
      else if (account_list == findNode(account_list, username))
      {
        account_list = deleteHead(account_list);
        printf("Client \"%s\" deleted\n", username);
      }
      else
      {
        account_list = deleteAt(account_list, username);
        printf("Client \"%s\" deleted\n", username);
      }
      saveData(account_list, filename); // Save updated data to the file
      break;
    case 6:
      break;
    default:
      printf("Invalid choice. Please select again!\n");
      break;
    }
  } while (chon != 6);

  free(account_list);
  return 0;
}
