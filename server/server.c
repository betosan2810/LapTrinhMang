#include <arpa/inet.h>   // htons(), inet_ntoa()
#include <errno.h>       // errno
#include <netinet/in.h>  // inet_addr(), bind()
#include <signal.h>      // signal()
#include <stdbool.h>     // bool
#include <stdio.h>
#include <stdlib.h>      // strtol()
#include <string.h>      // bzero(), strlen(), strcmp(), strcpy(), strtok(), strrchr(), memcmp()
#include <sys/socket.h>  // socket(), inet_addr(), bind(), listen(), accept(), recv(), send()
#include <sys/types.h>   // socket()
#include <unistd.h>      // close()
#include <ctype.h>
#include <sys/stat.h>    // mkdir()
#include <sys/wait.h>    // waitpid()
#include <time.h>        // time(), localtime(), strftime()
#include <pthread.h>     // Thêm thư viện để quản lý nhiều client đồng thời
#include <dirent.h>      // Thêm thư viện này để sử dụng opendir, readdir, closedir

#include "account.h"

#define MaxClient 20
#define MAX 1024
#define FTP_PORT 21
#define PASV_PORT_RANGE_START 30000
#define PASV_PORT_RANGE_END 31000

// Struct để truyền dữ liệu cho mỗi thread
typedef struct {
    int client_sock;
    node_a *account_list;
    char base_dir[MAX];
    // Thêm thông tin cho kết nối PASV
    int pasv_sock;
    bool pasv_active;
} thread_data;

// Hàm hỗ trợ để lấy địa chỉ IP của server
void get_server_ip(char *ip_buffer, size_t buffer_size) {
    // Sử dụng địa chỉ loopback
    strncpy(ip_buffer, "127,0,0,1", buffer_size);
}

// Hàm để thiết lập kết nối PASV
bool setup_pasv(thread_data *data, char *response, size_t response_size) {
    // Tạo socket dữ liệu
    data->pasv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data->pasv_sock < 0) {
        perror("PASV socket creation failed");
        return false;
    }

    // Cấu hình địa chỉ và cổng cho kết nối dữ liệu
    struct sockaddr_in pasv_addr;
    memset(&pasv_addr, 0, sizeof(pasv_addr));
    pasv_addr.sin_family = AF_INET;
    pasv_addr.sin_addr.s_addr = INADDR_ANY;

    // Chọn ngẫu nhiên cổng trong khoảng PASV_PORT_RANGE_START đến PASV_PORT_RANGE_END
    srand(time(NULL));
    int port;
    bool bound = false;
    for (int i = 0; i < 100; ++i) {
        port = PASV_PORT_RANGE_START + rand() % (PASV_PORT_RANGE_END - PASV_PORT_RANGE_START + 1);
        pasv_addr.sin_port = htons(port);
        if (bind(data->pasv_sock, (struct sockaddr *)&pasv_addr, sizeof(pasv_addr)) == 0) {
            bound = true;
            break;
        }
    }
    if (!bound) {
        perror("Binding PASV socket failed");
        close(data->pasv_sock);
        return false;
    }

    // Lắng nghe kết nối trên cổng dữ liệu
    if (listen(data->pasv_sock, 1) < 0) {
        perror("PASV listen failed");
        close(data->pasv_sock);
        return false;
    }

    // Lấy địa chỉ và cổng đã được gán cho socket
    socklen_t addr_len = sizeof(pasv_addr);
    if (getsockname(data->pasv_sock, (struct sockaddr *)&pasv_addr, &addr_len) < 0) {
        perror("getsockname failed");
        close(data->pasv_sock);
        return false;
    }

    // Chuyển cổng sang dạng phù hợp với FTP Passive Mode (công thức cổng: (high_byte * 256 + low_byte))
    int pasv_port_final = ntohs(pasv_addr.sin_port);
    int high_byte = pasv_port_final / 256;
    int low_byte = pasv_port_final % 256;

    // Lấy địa chỉ IP của server
    char server_ip[16];
    get_server_ip(server_ip, sizeof(server_ip));

    // Chuyển địa chỉ IP sang định dạng phù hợp với FTP Passive Mode
    // Địa chỉ IP đã ở dạng 127,0,0,1
    snprintf(response, response_size, "227 Entering Passive Mode (%s,%d,%d)\r\n",
             server_ip, high_byte, low_byte);

    data->pasv_active = true;
    return true;
}

// Hàm xử lý client
void *handle_client(void *arg)
{
    thread_data *data = (thread_data *)arg;
    int client_sock = data->client_sock;
    node_a *account_list = data->account_list;
    char base_dir[MAX];
    strncpy(base_dir, data->base_dir, MAX);
    data->pasv_sock = -1;
    data->pasv_active = false;

    free(arg); // Giải phóng bộ nhớ đã cấp phát cho thread_data

    char buffer[MAX];
    int bytes_received;
    char username[MAX], password[MAX];
    int authenticated = 0;
    node_a *current_user = NULL; // Biến này lưu thông tin người dùng đã đăng nhập
    char current_dir_relative[MAX] = "/"; // Thư mục làm việc hiện tại của client
    char current_dir_absolute[MAX] = ""; // Đường dẫn tuyệt đối trên hệ thống

    // Gửi thông báo kết nối thành công đến client
    const char *welcome_message = "220 Welcome to FTP Server!\r\n";
    send(client_sock, welcome_message, strlen(welcome_message), 0);

    // Lắng nghe lệnh từ client và phản hồi lại
    while (1)
    {
        bzero(buffer, MAX);
        bytes_received = recv(client_sock, buffer, MAX - 1, 0);
        if (bytes_received <= 0)
        {
            break; // Nếu không nhận được dữ liệu, thoát khỏi vòng lặp
        }

        buffer[bytes_received] = '\0'; // Đảm bảo chuỗi kết thúc đúng cách
        printf("Received command: %s\n", buffer);

        // Loại bỏ ký tự xuống dòng nếu có
        char *newline = strstr(buffer, "\r\n");
        if (newline)
        {
            *newline = '\0';
        }

        // Xử lý lệnh USER - yêu cầu tên người dùng
        if (strncmp(buffer, "USER", 4) == 0)
        {
            if (authenticated)
            {
                send(client_sock, "331 User already authenticated.\r\n", 33, 0);
            }
            else
            {
                sscanf(buffer, "USER %s", username);
                printf("Received USER: %s\n", username);
                send(client_sock, "331 Password please.\r\n", 22, 0);
            }
        }
        // Xử lý lệnh PASS - xác thực mật khẩu
        else if (strncmp(buffer, "PASS", 4) == 0)
        {
            if (authenticated)
            {
                send(client_sock, "503 Already logged in.\r\n", 23, 0);
            }
            else
            {
                sscanf(buffer, "PASS %s", password);
                printf("Received PASS: %s\n", password);

                // Kiểm tra thông tin đăng nhập từ danh sách tài khoản
                current_user = validate_user(account_list, username, password);
                if (current_user != NULL)
                {
                    send(client_sock, "230 User logged in, proceed.\r\n", 30, 0);
                    authenticated = 1;
                    // Thiết lập thư mục làm việc hiện tại cho user
                    snprintf(current_dir_relative, sizeof(current_dir_relative), "/%s", current_user->folder);
                    snprintf(current_dir_absolute, sizeof(current_dir_absolute), "%s%s", base_dir, current_dir_relative);
                }
                else
                {
                    send(client_sock, "530 Not logged in, invalid credentials.\r\n", 41, 0);
                }
            }
        }
        // Xử lý lệnh TYPE - chuyển chế độ truyền dữ liệu (ASCII hoặc Binary)
        else if (strncmp(buffer, "TYPE", 4) == 0)
        {
            char type[3];
            sscanf(buffer, "TYPE %s", type);

            if (strcmp(type, "I") == 0)
            {
                send(client_sock, "200 Type set to I.\r\n", 19, 0);
            }
            else if (strcmp(type, "A") == 0)
            {
                send(client_sock, "200 Type set to A.\r\n", 19, 0);
            }
            else
            {
                send(client_sock, "504 Command not implemented for that parameter.\r\n", 47, 0);
            }
        }
        // Xử lý lệnh FEAT - yêu cầu các tính năng được hỗ trợ bởi server
        else if (strncmp(buffer, "FEAT", 4) == 0)
        {
            const char *feat_response = "211-Features:\r\n"
                                        " SIZE\r\n"
                                        " MDTM\r\n"
                                        " UTF8\r\n"
                                        "211 End\r\n";
            send(client_sock, feat_response, strlen(feat_response), 0);
        }
        // Xử lý lệnh SYST - yêu cầu hệ thống của server
        else if (strncmp(buffer, "SYST", 4) == 0)
        {
            send(client_sock, "215 UNIX Type: L8\r\n", 18, 0);
        }
        // Xử lý lệnh OPTS UTF8
        else if (strncmp(buffer, "OPTS UTF8", 9) == 0)
        {
            send(client_sock, "200 UTF8 option accepted.\r\n", 28, 0);
        }
        // Xử lý lệnh PWD
        else if (strncmp(buffer, "PWD", 3) == 0)
        {
            if (authenticated && current_user)
            {
                snprintf(buffer, sizeof(buffer), "257 \"%s\" is the current directory.\r\n", current_dir_relative);
                send(client_sock, buffer, strlen(buffer), 0);
            }
            else
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
            }
        }
        // Xử lý lệnh PASV
        else if (strncmp(buffer, "PASV", 4) == 0)
        {
            if (!authenticated || current_user == NULL)
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                continue;
            }

            if (data->pasv_active)
            {
                send(client_sock, "421 Already in Passive Mode.\r\n", 30, 0);
                continue;
            }

            char pasv_response[MAX];
            if (!setup_pasv(data, pasv_response, sizeof(pasv_response)))
            {
                send(client_sock, "550 PASV setup failed.\r\n", 23, 0);
                continue;
            }

            send(client_sock, pasv_response, strlen(pasv_response), 0);
        }
        // Xử lý lệnh LIST - liệt kê các tệp trong thư mục người dùng
        else if (strncmp(buffer, "LIST", 4) == 0)
        {
            if (!authenticated || current_user == NULL)
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                continue;
            }

            if (!data->pasv_active)
            {
                send(client_sock, "425 Use PASV or PORT first.\r\n", 28, 0);
                continue;
            }

            // Chấp nhận kết nối dữ liệu từ client
            struct sockaddr_in data_client_addr;
            socklen_t data_client_len = sizeof(data_client_addr);
            int data_sock = accept(data->pasv_sock, (struct sockaddr *)&data_client_addr, &data_client_len);
            if (data_sock < 0)
            {
                perror("Data connection accept failed");
                send(client_sock, "425 Cannot open data connection.\r\n", 34, 0);
                close(data->pasv_sock);
                data->pasv_active = false;
                continue;
            }

            // Gửi phản hồi cho lệnh LIST
            send(client_sock, "150 Opening data connection.\r\n", 30, 0);

            // Gửi danh sách thư mục qua data_sock
            DIR *dir = opendir(current_dir_absolute);
            if (dir == NULL)
            {
                perror("Failed to open directory");
                send(client_sock, "550 Failed to open directory.\r\n", 31, 0);
            }
            else
            {
                struct dirent *entry;
                char file_info[MAX];
                struct stat st;
                struct tm *tm_info;
                char time_str[20];

                while ((entry = readdir(dir)) != NULL)
                {
                    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                    {
                        // Lấy thông tin tệp tin
                        char filepath[MAX];
                        snprintf(filepath, sizeof(filepath), "%s/%s", current_dir_absolute, entry->d_name);
                        if (stat(filepath, &st) == -1)
                        {
                            perror("stat failed");
                            continue;
                        }

                        tm_info = localtime(&st.st_mtime);
                        strftime(time_str, sizeof(time_str), "%b %d %H:%M", tm_info);

                        // Định dạng giống như lệnh `ls -l`
                        snprintf(file_info, sizeof(file_info), 
                                 "drwxr-xr-x 1 user group %ld %s %s\r\n",
                                 st.st_size, time_str, entry->d_name);
                        send(data_sock, file_info, strlen(file_info), 0);
                    }
                }
                closedir(dir);
                send(client_sock, "226 Transfer complete.\r\n", 24, 0);
            }

            // Đóng kết nối dữ liệu và PASV socket
            close(data_sock);
            close(data->pasv_sock);
            data->pasv_active = false;
        }
        // Lệnh CWD - Thay đổi thư mục làm việc
        else if (strncmp(buffer, "CWD", 3) == 0)
        {
            if (!authenticated || current_user == NULL)
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                continue;
            }

            char path[MAX];
            sscanf(buffer, "CWD %s", path);

            char new_dir_relative[MAX];
            if (path[0] == '/')
            {
                // Nếu path bắt đầu bằng '/', tính từ thư mục gốc của user
                snprintf(new_dir_relative, sizeof(new_dir_relative), "%s", path);
            }
            else
            {
                // Nếu path không bắt đầu bằng '/', tính từ thư mục hiện tại
                if (strcmp(current_dir_relative, "/") == 0)
                    snprintf(new_dir_relative, sizeof(new_dir_relative), "/%s", path);
                else
                    snprintf(new_dir_relative, sizeof(new_dir_relative), "%s/%s", current_dir_relative, path);
            }

            // Giữ nguyên thư mục nếu path chứa ".." để tránh truy cập ngoài thư mục người dùng
            if (strstr(new_dir_relative, ".."))
            {
                send(client_sock, "550 Failed to change directory: Access denied.\r\n", 47, 0);
                continue;
            }

            // Xây dựng đường dẫn tuyệt đối
            char new_dir_absolute[MAX];
            snprintf(new_dir_absolute, sizeof(new_dir_absolute), "%s%s", base_dir, new_dir_relative);

            // Kiểm tra sự tồn tại của thư mục
            struct stat statbuf;
            if (stat(new_dir_absolute, &statbuf) != 0)
            {
                perror("stat failed");
                send(client_sock, "550 Failed to change directory.\r\n", 33, 0);
                continue;
            }

            // Kiểm tra thư mục có phải là thư mục không
            if (!S_ISDIR(statbuf.st_mode))
            {
                send(client_sock, "550 Failed to change directory: Not a directory.\r\n", 49, 0);
                continue;
            }

            // Cập nhật thư mục làm việc hiện tại
            strncpy(current_dir_relative, new_dir_relative, sizeof(current_dir_relative) - 1);
            current_dir_relative[sizeof(current_dir_relative) - 1] = '\0';
            strncpy(current_dir_absolute, new_dir_absolute, sizeof(current_dir_absolute) - 1);
            current_dir_absolute[sizeof(current_dir_absolute) - 1] = '\0';

            send(client_sock, "250 Directory changed.\r\n", 24, 0);
        }
        // Lệnh QUIT - ngắt kết nối
        else if (strncmp(buffer, "QUIT", 4) == 0)
        {
            send(client_sock, "221 Goodbye!\r\n", 14, 0);
            break;
        }
        // Lệnh không xác định
        else
        {
            send(client_sock, "500 Command not recognized.\r\n", 30, 0);
        }
    }

    // Đóng kết nối khi client ngắt
    close(client_sock);
    return NULL;
}

// Hàm khởi động server
void start_server(int port, node_a *account_list)
{
    int sockfd, client_sock;
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

    // Cấu hình địa chỉ server
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

    // Lấy đường dẫn thư mục gốc của server (FTP root)
    char base_dir[MAX];
    if (getcwd(base_dir, sizeof(base_dir)) == NULL)
    {
        perror("getcwd failed");
        close(sockfd);
        exit(1);
    }

    printf("Server started on port %d. Waiting for connections...\n", port);

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

        printf("Client connected from %s:%d.\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Tạo một thread mới để xử lý client
        thread_data *data = malloc(sizeof(thread_data));
        if (data == NULL)
        {
            perror("Malloc failed");
            close(client_sock);
            continue;
        }
        data->client_sock = client_sock;
        data->account_list = account_list;
        strncpy(data->base_dir, base_dir, MAX);
        data->pasv_sock = -1;
        data->pasv_active = false;

        if (pthread_create(&client_thread, NULL, handle_client, (void *)data) < 0)
        {
            perror("Could not create thread");
            close(client_sock);
            free(data);
            continue;
        }

        // Tách thread để tránh rò rỉ tài nguyên
        pthread_detach(client_thread);
    }

    close(sockfd);
}

int main(int argc, const char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: %s <port>\n\n", argv[0]);
        return 0;
    }

    char username[MAX], pass[MAX], folder[MAX];
    char filename[] = "account.txt";

    // Load dữ liệu tài khoản từ file
    node_a *account_list = loadData(filename);
    if (account_list == NULL)
    {
        printf("Failed to load account data.\n");
        return 1;
    }

    // Menu quản lý tài khoản
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
        while (getchar() != '\n'); // Xóa buffer

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
                        printf("The root directory does not allow write.\n");
                        break;
                    case EEXIST:
                        printf("Folder %s already exists.\n", folder);
                        c = 1;
                        break;
                    case ENAMETOOLONG:
                        printf("Pathname is too long.\n");
                        break;
                    default:
                        printf("mkdir failed.\n");
                        break;
                    }
                }
                else
                {
                    printf("Created: %s\n", folder);
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

    // Giải phóng danh sách tài khoản
    while (account_list != NULL)
    {
        node_a *temp = account_list;
        account_list = account_list->next;
        free(temp);
    }

    return 0;
}
