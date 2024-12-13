#include <strings.h>       // bzero(), etc.
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <errno.h>         // errno
#include <netinet/in.h>    // inet_addr(), bind()
#include <net/if.h>        // IFF_LOOPBACK
#include <signal.h>        // signal()
#include <stdbool.h>       // bool
#include <stdio.h>
#include <stdlib.h>        // strtol()
#include <string.h>        // bzero(), strlen(), strcmp(), strcpy(), strtok(), strrchr(), memcmp()
#include <sys/socket.h>    // socket(), inet_addr(), bind(), listen(), accept(), recv(), send()
#include <sys/types.h>     // socket()
#include <unistd.h>        // close()
#include <ctype.h>
#include <sys/stat.h>      // mkdir(), stat()
#include <sys/wait.h>      // waitpid()
#include <time.h>          // time(), localtime(), strftime()
#include <pthread.h>       // pthread_create(), pthread_detach()
#include <dirent.h>        // opendir(), readdir(), closedir()
#include <fcntl.h>         // open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC
#include <ifaddrs.h>       // getifaddrs()
#include <netdb.h>         // getnameinfo()
#include <pwd.h>           // getpwuid()
#include <grp.h>           // getgrgid()

#include "account.h"       // Giả sử account.h cung cấp các cấu trúc và hàm cần thiết

#define MaxClient 20
#define MAX_BUFFER_SIZE 8192
#define PASV_PORT_RANGE_START 30000
#define PASV_PORT_RANGE_END 31000

// Struct để truyền dữ liệu cho mỗi thread
typedef struct {
    int client_sock;
    node_a *account_list;
    char base_dir[MAX_BUFFER_SIZE];
    // Thông tin kết nối PASV
    int pasv_sock;
    bool pasv_active;
    // Chế độ truyền dữ liệu: 'A' - ASCII, 'I' - Binary
    char transfer_type;
} thread_data;

// Hàm hỗ trợ để lấy địa chỉ IP của server
void get_server_ip(char *ip_buffer, size_t buffer_size) {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        strncpy(ip_buffer, "127,0,0,1", buffer_size);
        ip_buffer[buffer_size - 1] = '\0';
        return;
    }

    // Lặp qua các interface
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        // Chỉ lấy địa chỉ IPv4 và không phải là loopback
        if (family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
            s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            if (s == 0) {
                // Thay thế dấu chấm bằng dấu phẩy
                for (size_t i = 0; i < strlen(host); i++) {
                    if (host[i] == '.') host[i] = ',';
                }
                strncpy(ip_buffer, host, buffer_size);
                ip_buffer[buffer_size - 1] = '\0';
                freeifaddrs(ifaddr);
                return;
            }
        }
    }

    // Nếu không tìm thấy địa chỉ phù hợp, sử dụng loopback
    strncpy(ip_buffer, "127,0,0,1", buffer_size);
    ip_buffer[buffer_size - 1] = '\0';
    freeifaddrs(ifaddr);
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
    char server_ip[64];
    get_server_ip(server_ip, sizeof(server_ip));

    // Chuyển địa chỉ IP sang định dạng phù hợp với FTP Passive Mode
    snprintf(response, response_size, "227 Entering Passive Mode (%s,%d,%d)\r\n",
             server_ip, high_byte, low_byte);

    printf("PASV response: %s", response); // Debug log
    data->pasv_active = true;
    return true;
}

// Hàm để xử lý lệnh RETR (download)
bool handle_retr(thread_data *data, const char *current_dir_absolute, const char *filename) {
    // Xây dựng đường dẫn tệp tin
    char filepath[MAX_BUFFER_SIZE];
    snprintf(filepath, sizeof(filepath), "%s/%s", current_dir_absolute, filename);

    // Kiểm tra sự tồn tại của tệp tin
    struct stat st;
    if (stat(filepath, &st) != 0) {
        perror("stat failed");
        send(data->client_sock, "550 File not found.\r\n", 21, 0);
        return false;
    }

    // Kiểm tra xem có phải là thư mục không
    if (S_ISDIR(st.st_mode)) {
        send(data->client_sock, "550 Requested action not taken. File is a directory.\r\n", 52, 0);
        return false;
    }

    // Mở tệp tin để đọc
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        perror("Failed to open file for RETR");
        send(data->client_sock, "550 Failed to open file.\r\n", 25, 0);
        return false;
    }

    // Gửi phản hồi cho lệnh RETR
    send(data->client_sock, "150 Opening data connection.\r\n", 30, 0);
    printf("Sent 150 Opening data connection.\r\n"); // Debug log

    // Chấp nhận kết nối dữ liệu từ client
    struct sockaddr_in data_client_addr;
    socklen_t data_client_len = sizeof(data_client_addr);
    int data_sock = accept(data->pasv_sock, (struct sockaddr *)&data_client_addr, &data_client_len);
    if (data_sock < 0) {
        perror("Data connection accept failed");
        send(data->client_sock, "425 Cannot open data connection.\r\n", 34, 0);
        close(file_fd);
        close(data->pasv_sock);
        data->pasv_active = false;
        return false;
    }

    // Gửi dữ liệu tệp tin qua data_sock
    ssize_t bytes_read;
    char file_buffer[MAX_BUFFER_SIZE];
    while ((bytes_read = read(file_fd, file_buffer, sizeof(file_buffer))) > 0) {
        // Nếu chế độ ASCII, chuyển đổi kết thúc dòng
        if (data->transfer_type == 'A') {
            // Chuyển \n thành \r\n
            char ascii_buffer[MAX_BUFFER_SIZE * 2];
            int ascii_len = 0;
            for (ssize_t i = 0; i < bytes_read; ++i) {
                if (file_buffer[i] == '\n') {
                    ascii_buffer[ascii_len++] = '\r';
                }
                ascii_buffer[ascii_len++] = file_buffer[i];
                if (ascii_len >= sizeof(ascii_buffer)) break;
            }
            if (send(data_sock, ascii_buffer, ascii_len, 0) < 0) {
                perror("Failed to send file data");
                break;
            }
        } else {
            // Binary mode
            if (send(data_sock, file_buffer, bytes_read, 0) < 0) {
                perror("Failed to send file data");
                break;
            }
        }
    }

    if (bytes_read < 0) {
        perror("Failed to read file data");
    }

    close(file_fd);
    close(data_sock);
    close(data->pasv_sock);
    data->pasv_active = false;

    // Gửi phản hồi hoàn tất
    send(data->client_sock, "226 Transfer complete.\r\n", 24, 0);
    printf("Sent 226 Transfer complete.\r\n"); // Debug log
    return true;
}

// Hàm để xử lý lệnh STOR (upload)
bool handle_stor(thread_data *data, const char *current_dir_absolute, const char *filename) {
    // Xây dựng đường dẫn tệp tin
    char filepath[MAX_BUFFER_SIZE];
    snprintf(filepath, sizeof(filepath), "%s/%s", current_dir_absolute, filename);

    // Mở tệp tin để ghi (tạo mới hoặc ghi đè)
    int file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        perror("Failed to open file for STOR");
        send(data->client_sock, "550 Failed to create file.\r\n", 27, 0);
        return false;
    }

    // Gửi phản hồi cho lệnh STOR
    send(data->client_sock, "150 Opening data connection.\r\n", 30, 0);
    printf("Sent 150 Opening data connection.\r\n"); // Debug log

    // Chấp nhận kết nối dữ liệu từ client
    struct sockaddr_in data_client_addr;
    socklen_t data_client_len = sizeof(data_client_addr);
    int data_sock = accept(data->pasv_sock, (struct sockaddr *)&data_client_addr, &data_client_len);
    if (data_sock < 0) {
        perror("Data connection accept failed");
        send(data->client_sock, "425 Cannot open data connection.\r\n", 34, 0);
        close(file_fd);
        close(data->pasv_sock);
        data->pasv_active = false;
        return false;
    }

    // Nhận dữ liệu tệp tin qua data_sock
    ssize_t bytes_received;
    char file_buffer[MAX_BUFFER_SIZE];
    while ((bytes_received = recv(data_sock, file_buffer, sizeof(file_buffer), 0)) > 0) {
        // Nếu chế độ ASCII, chuyển đổi kết thúc dòng
        if (data->transfer_type == 'A') {
            // Chuyển \r\n thành \n
            char ascii_buffer[MAX_BUFFER_SIZE];
            int ascii_len = 0;
            for (ssize_t i = 0; i < bytes_received; ++i) {
                if (file_buffer[i] == '\r') {
                    if (i + 1 < bytes_received && file_buffer[i + 1] == '\n') {
                        i++; // Bỏ qua \n
                    }
                }
                ascii_buffer[ascii_len++] = file_buffer[i];
                if (ascii_len >= sizeof(ascii_buffer)) break;
            }
            if (write(file_fd, ascii_buffer, ascii_len) < 0) {
                perror("Failed to write to file");
                break;
            }
        } else {
            // Binary mode
            if (write(file_fd, file_buffer, bytes_received) < 0) {
                perror("Failed to write to file");
                break;
            }
        }
    }

    if (bytes_received < 0) {
        perror("Failed to receive file data");
    }

    close(file_fd);
    close(data_sock);
    close(data->pasv_sock);
    data->pasv_active = false;

    // Gửi phản hồi hoàn tất
    send(data->client_sock, "226 Transfer complete.\r\n", 24, 0);
    printf("Sent 226 Transfer complete.\r\n"); // Debug log
    return true;
}

// Hàm xử lý client
void *handle_client(void *arg)
{
    thread_data *data = (thread_data *)arg;
    int client_sock = data->client_sock;
    node_a *account_list = data->account_list;
    char base_dir[MAX_BUFFER_SIZE];
    strncpy(base_dir, data->base_dir, MAX_BUFFER_SIZE);
    data->pasv_sock = -1;
    data->pasv_active = false;
    data->transfer_type = 'I'; // Mặc định là Binary

    free(arg); // Giải phóng bộ nhớ đã cấp phát cho thread_data

    char buffer[MAX_BUFFER_SIZE];
    int bytes_received;
    char username[MAX_BUFFER_SIZE], password[MAX_BUFFER_SIZE];
    int authenticated = 0;
    node_a *current_user = NULL; // Biến này lưu thông tin người dùng đã đăng nhập
    char current_dir_relative[MAX_BUFFER_SIZE] = "/"; // Thư mục làm việc hiện tại của client
    char current_dir_absolute[MAX_BUFFER_SIZE] = ""; // Đường dẫn tuyệt đối trên hệ thống

    // Gửi thông báo kết nối thành công đến client
    const char *welcome_message = "220 Welcome to FTP Server!\r\n";
    send(client_sock, welcome_message, strlen(welcome_message), 0);
    printf("Sent welcome message: %s", welcome_message); // Debug log

    // Lấy đường dẫn tuyệt đối của thư mục gốc
    if (current_dir_absolute[0] == '\0') {
        strncpy(current_dir_absolute, base_dir, sizeof(current_dir_absolute) - 1);
        current_dir_absolute[sizeof(current_dir_absolute) - 1] = '\0';
    }

    // Lắng nghe lệnh từ client và phản hồi lại
    while (1)
    {
        bzero(buffer, MAX_BUFFER_SIZE);
        bytes_received = recv(client_sock, buffer, MAX_BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0)
        {
            printf("Client disconnected.\n");
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
        if (strncasecmp(buffer, "USER", 4) == 0)
        {
            if (authenticated)
            {
                send(client_sock, "332 User not logged in, need account for login.\r\n", 48, 0);
                printf("Sent: 332 User not logged in, need account for login.\r\n"); // Debug log
            }
            else
            {
                sscanf(buffer, "USER %s", username);
                printf("Received USER: %s\n", username);
                send(client_sock, "331 Username ok, need password.\r\n", 32, 0);
                printf("Sent: 331 Username ok, need password.\r\n"); // Debug log
            }
        }
        // Xử lý lệnh PASS - xác thực mật khẩu
        else if (strncasecmp(buffer, "PASS", 4) == 0)
        {
            if (authenticated)
            {
                send(client_sock, "230 User already logged in.\r\n", 29, 0);
                printf("Sent: 230 User already logged in.\r\n"); // Debug log
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
                    printf("Sent: 230 User logged in, proceed.\r\n"); // Debug log
                    authenticated = 1;
                    // Thiết lập thư mục làm việc hiện tại cho user
                    snprintf(current_dir_relative, sizeof(current_dir_relative), "/%s", current_user->folder);
                    snprintf(current_dir_absolute, sizeof(current_dir_absolute), "%s%s", base_dir, current_dir_relative);
                }
                else
                {
                    send(client_sock, "530 Not logged in, invalid credentials.\r\n", 41, 0);
                    printf("Sent: 530 Not logged in, invalid credentials.\r\n"); // Debug log
                }
            }
        }
        // Xử lý lệnh TYPE - chuyển chế độ truyền dữ liệu (ASCII hoặc Binary)
        else if (strncasecmp(buffer, "TYPE", 4) == 0)
        {
            char type[3];
            sscanf(buffer, "TYPE %s", type);

            if (strcmp(type, "I") == 0)
            {
                send(client_sock, "200 Type set to I.\r\n", 19, 0);
                printf("Sent: 200 Type set to I.\r\n"); // Debug log
                data->transfer_type = 'I';
            }
            else if (strcmp(type, "A") == 0)
            {
                send(client_sock, "200 Type set to A.\r\n", 19, 0);
                printf("Sent: 200 Type set to A.\r\n"); // Debug log
                data->transfer_type = 'A';
            }
            else
            {
                send(client_sock, "504 Command not implemented for that parameter.\r\n", 47, 0);
                printf("Sent: 504 Command not implemented for that parameter.\r\n"); // Debug log
            }
        }
        // Xử lý lệnh FEAT - yêu cầu các tính năng được hỗ trợ bởi server
        else if (strncasecmp(buffer, "FEAT", 4) == 0)
        {
            const char *feat_response = "211-Features:\r\n"
                                        " SIZE\r\n"
                                        " MDTM\r\n"
                                        " UTF8\r\n"
                                        "211 End\r\n";
            send(client_sock, feat_response, strlen(feat_response), 0);
            printf("Sent FEAT response:\n%s", feat_response); // Debug log
        }
        // Xử lý lệnh SYST - yêu cầu hệ thống của server
        else if (strncasecmp(buffer, "SYST", 4) == 0)
        {
            send(client_sock, "215 UNIX Type: L8\r\n", 18, 0);
            printf("Sent: 215 UNIX Type: L8\r\n"); // Debug log
        }
        // Xử lý lệnh OPTS UTF8
        else if (strncasecmp(buffer, "OPTS UTF8", 9) == 0)
        {
            send(client_sock, "200 UTF8 option accepted.\r\n", 28, 0);
            printf("Sent: 200 UTF8 option accepted.\r\n"); // Debug log
        }
        // Xử lý lệnh PWD
        else if (strncasecmp(buffer, "PWD", 3) == 0)
        {
            if (authenticated && current_user)
            {
                snprintf(buffer, sizeof(buffer), "257 \"%s\" is the current directory.\r\n", current_dir_relative);
                send(client_sock, buffer, strlen(buffer), 0);
                printf("Sent: %s", buffer); // Debug log
            }
            else
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                printf("Sent: 530 Please login first.\r\n"); // Debug log
            }
        }
        // Xử lý lệnh PASV
        else if (strncasecmp(buffer, "PASV", 4) == 0)
        {
            if (!authenticated || current_user == NULL)
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                printf("Sent: 530 Please login first.\r\n"); // Debug log
                continue;
            }

            if (data->pasv_active)
            {
                send(client_sock, "421 Already in Passive Mode.\r\n", 30, 0);
                printf("Sent: 421 Already in Passive Mode.\r\n"); // Debug log
                continue;
            }

            char pasv_response[MAX_BUFFER_SIZE];
            if (!setup_pasv(data, pasv_response, sizeof(pasv_response)))
            {
                send(client_sock, "550 PASV setup failed.\r\n", 23, 0);
                printf("Sent: 550 PASV setup failed.\r\n"); // Debug log
                continue;
            }

            send(client_sock, pasv_response, strlen(pasv_response), 0);
            printf("Sent PASV response: %s", pasv_response); // Debug log
        }
        // Xử lý lệnh LIST - liệt kê các tệp trong thư mục người dùng
        else if (strncasecmp(buffer, "LIST", 4) == 0)
        {
            if (!authenticated || current_user == NULL)
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                printf("Sent: 530 Please login first.\r\n"); // Debug log
                continue;
            }

            if (!data->pasv_active)
            {
                send(client_sock, "425 Use PASV or PORT first.\r\n", 28, 0);
                printf("Sent: 425 Use PASV or PORT first.\r\n"); // Debug log
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
            printf("Sent 150 Opening data connection.\r\n"); // Debug log

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
                char file_info[MAX_BUFFER_SIZE];
                struct stat st;
                struct tm *tm_info;
                char time_str[20];
                struct passwd *pw;
                struct group *gr;

                while ((entry = readdir(dir)) != NULL)
                {
                    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                    {
                        // Lấy thông tin tệp tin
                        char filepath[MAX_BUFFER_SIZE];
                        snprintf(filepath, sizeof(filepath), "%s/%s", current_dir_absolute, entry->d_name);
                        if (stat(filepath, &st) == -1)
                        {
                            perror("stat failed");
                            continue;
                        }

                        tm_info = localtime(&st.st_mtime);
                        strftime(time_str, sizeof(time_str), "%b %d %H:%M", tm_info);

                        // Lấy thông tin chủ sở hữu và nhóm
                        pw = getpwuid(st.st_uid);
                        gr = getgrgid(st.st_gid);
                        char *owner = pw ? pw->pw_name : "unknown";
                        char *group = gr ? gr->gr_name : "unknown";

                        // Xác định loại tệp tin và quyền truy cập
                        char permissions[11];
                        snprintf(permissions, sizeof(permissions), "%c%c%c%c%c%c%c%c%c%c",
                                 S_ISDIR(st.st_mode) ? 'd' : '-',
                                 (st.st_mode & S_IRUSR) ? 'r' : '-',
                                 (st.st_mode & S_IWUSR) ? 'w' : '-',
                                 (st.st_mode & S_IXUSR) ? 'x' : '-',
                                 (st.st_mode & S_IRGRP) ? 'r' : '-',
                                 (st.st_mode & S_IWGRP) ? 'w' : '-',
                                 (st.st_mode & S_IXGRP) ? 'x' : '-',
                                 (st.st_mode & S_IROTH) ? 'r' : '-',
                                 (st.st_mode & S_IWOTH) ? 'w' : '-',
                                 (st.st_mode & S_IXOTH) ? 'x' : '-');

                        // Định dạng giống như lệnh `ls -l`
                        snprintf(file_info, sizeof(file_info), 
                                 "%s %lu %s %s %ld %s %s\r\n",
                                 permissions,
                                 st.st_nlink,
                                 owner,
                                 group,
                                 st.st_size,
                                 time_str,
                                 entry->d_name);
                        send(data_sock, file_info, strlen(file_info), 0);
                        printf("Sent directory entry: %s", file_info); // Debug log
                    }
                }
                closedir(dir);
                send(client_sock, "226 Transfer complete.\r\n", 24, 0);
                printf("Sent 226 Transfer complete.\r\n"); // Debug log
            }

            // Đóng kết nối dữ liệu và PASV socket
            close(data_sock);
            close(data->pasv_sock);
            data->pasv_active = false;
        }
        // Xử lý lệnh RETR - tải xuống tệp tin
        else if (strncasecmp(buffer, "RETR", 4) == 0)
        {
            if (!authenticated || current_user == NULL)
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                printf("Sent: 530 Please login first.\r\n"); // Debug log
                continue;
            }

            if (!data->pasv_active)
            {
                send(client_sock, "425 Use PASV or PORT first.\r\n", 28, 0);
                printf("Sent: 425 Use PASV or PORT first.\r\n"); // Debug log
                continue;
            }

            // Lấy tên tệp tin từ lệnh RETR
            char filename[MAX_BUFFER_SIZE];
            sscanf(buffer, "RETR %s", filename);
            printf("Retrieving file: %s\n", filename);

            // Xử lý RETR
            if (!handle_retr(data, current_dir_absolute, filename)) {
                // Nếu xử lý RETR thất bại, đã gửi phản hồi lỗi trong hàm handle_retr
                continue;
            }
        }
        // Xử lý lệnh STOR - tải lên tệp tin
        else if (strncasecmp(buffer, "STOR", 4) == 0)
        {
            if (!authenticated || current_user == NULL)
            {
                send(client_sock, "530 Please login first.\r\n", 25, 0);
                printf("Sent: 530 Please login first.\r\n"); // Debug log
                continue;
            }

            if (!data->pasv_active)
            {
                send(client_sock, "425 Use PASV or PORT first.\r\n", 28, 0);
                printf("Sent: 425 Use PASV or PORT first.\r\n"); // Debug log
                continue;
            }

            // Lấy tên tệp tin từ lệnh STOR
            char filename[MAX_BUFFER_SIZE];
            sscanf(buffer, "STOR %s", filename);
            printf("Storing file: %s\n", filename);

            // Xử lý STOR
            if (!handle_stor(data, current_dir_absolute, filename)) {
                // Nếu xử lý STOR thất bại, đã gửi phản hồi lỗi trong hàm handle_stor
                continue;
            }
        }
        // Lệnh CWD - Thay đổi thư mục làm việc
        // Xử lý lệnh CWD
else if (strncasecmp(buffer, "CWD", 3) == 0)
{
    if (!authenticated || current_user == NULL)
    {
        send(client_sock, "530 Please login first.\r\n", 25, 0);
        printf("Sent: 530 Please login first.\r\n"); // Debug log
        continue;
    }

    char path[MAX_BUFFER_SIZE];
    sscanf(buffer, "CWD %s", path);

    // Nếu lệnh là 'CWD ..', xử lý di chuyển đến thư mục cha
    if (strcmp(path, "..") == 0)
    {
        if (strcmp(current_dir_relative, "/") == 0||strcmp(current_dir_relative, "/folder1") == 0)
        {
            // Nếu đã ở thư mục gốc, không thể di chuyển lên trên
            send(client_sock, "550 Already at root directory.\r\n", 32, 0);
            printf("Sent: 550 Already at root directory.\r\n"); // Debug log
            continue;
        }
        else
        {
            // Tìm vị trí dấu '/' cuối cùng trong đường dẫn hiện tại
            char *last_slash = strrchr(current_dir_relative, '/');
            if (last_slash != NULL)
            {
                if (last_slash == current_dir_relative)
                {
                    // Nếu dấu '/' đầu tiên là thư mục gốc
                    strncpy(current_dir_relative, "/", sizeof(current_dir_relative) - 1);
                    current_dir_relative[sizeof(current_dir_relative) - 1] = '\0';
                }
                else
                {
                    // Cắt bỏ phần thư mục con cuối cùng
                    *last_slash = '\0';
                }

                // Cập nhật đường dẫn tuyệt đối
                snprintf(current_dir_absolute, sizeof(current_dir_absolute), "%s%s", base_dir, current_dir_relative);

                // Kiểm tra xem đường dẫn mới có hợp lệ không
                struct stat statbuf;
                if (stat(current_dir_absolute, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
                {
                    // Nếu không tồn tại hoặc không phải thư mục, từ chối yêu cầu
                    send(client_sock, "550 Failed to change directory.\r\n", 33, 0);
                    printf("Sent: 550 Failed to change directory.\r\n"); // Debug log
                    continue;
                }

                send(client_sock, "250 Directory changed.\r\n", 24, 0);
                printf("Sent: 250 Directory changed.\r\n"); // Debug log
            }
            else
            {
                // Nếu không tìm thấy dấu '/', từ chối yêu cầu
                send(client_sock, "550 Failed to change directory.\r\n", 33, 0);
                printf("Sent: 550 Failed to change directory.\r\n"); // Debug log
                continue;
            }
        }
    }
    // Nếu đường dẫn chứa '..' nhưng không phải là '..' đơn thuần, từ chối
    else if (strstr(path, "..") != NULL)
    {
        send(client_sock, "550 Failed to change directory: Access denied.\r\n", 47, 0);
        printf("Sent: 550 Failed to change directory: Access denied.\r\n"); // Debug log
        continue;
    }
    else
    {
        // Xử lý các lệnh CWD khác (chuyển đến thư mục con hoặc tuyệt đối)
        char new_dir_relative[MAX_BUFFER_SIZE];
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

        // Xây dựng đường dẫn tuyệt đối
        char new_dir_absolute[MAX_BUFFER_SIZE];
        snprintf(new_dir_absolute, sizeof(new_dir_absolute), "%s%s", base_dir, new_dir_relative);

        // Kiểm tra sự tồn tại của thư mục
        struct stat statbuf;
        if (stat(new_dir_absolute, &statbuf) != 0)
        {
            perror("stat failed");
            send(client_sock, "550 Failed to change directory.\r\n", 33, 0);
            printf("Sent: 550 Failed to change directory.\r\n"); // Debug log
            continue;
        }

        // Kiểm tra thư mục có phải là thư mục không
        if (!S_ISDIR(statbuf.st_mode))
        {
            send(client_sock, "550 Failed to change directory: Not a directory.\r\n", 49, 0);
            printf("Sent: 550 Failed to change directory: Not a directory.\r\n"); // Debug log
            continue;
        }

        // Cập nhật thư mục làm việc hiện tại
        strncpy(current_dir_relative, new_dir_relative, sizeof(current_dir_relative) - 1);
        current_dir_relative[sizeof(current_dir_relative) - 1] = '\0';
        strncpy(current_dir_absolute, new_dir_absolute, sizeof(current_dir_absolute) - 1);
        current_dir_absolute[sizeof(current_dir_absolute) - 1] = '\0';

        send(client_sock, "250 Directory changed.\r\n", 24, 0);
        printf("Sent: 250 Directory changed.\r\n"); // Debug log
    }
}
        // Lệnh QUIT - ngắt kết nối
        else if (strncasecmp(buffer, "QUIT", 4) == 0)
        {
            send(client_sock, "221 Goodbye!\r\n", 14, 0);
            printf("Sent: 221 Goodbye!\r\n"); // Debug log
            break;
        }
        // Lệnh không xác định
        else
        {
            send(client_sock, "500 Command not recognized.\r\n", 30, 0);
            printf("Sent: 500 Command not recognized.\r\n"); // Debug log
        }
    }

    // Đóng kết nối khi client ngắt
    close(client_sock);
    printf("Closed connection with client.\n");
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
    char base_dir[MAX_BUFFER_SIZE];
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
        strncpy(data->base_dir, base_dir, MAX_BUFFER_SIZE);
        data->pasv_sock = -1;
        data->pasv_active = false;
        data->transfer_type = 'I'; // Mặc định là Binary

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

    char username[MAX_BUFFER_SIZE], pass[MAX_BUFFER_SIZE], folder[MAX_BUFFER_SIZE];
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
            fgets(username, MAX_BUFFER_SIZE, stdin);
            username[strcspn(username, "\n")] = '\0';
            while (findNode(account_list, username) != NULL)
            {
                printf("Client existed. Please try again\n");
                printf("New Client Username: ");
                fgets(username, MAX_BUFFER_SIZE, stdin);
                username[strcspn(username, "\n")] = '\0';
            }

            printf("New Client Password: ");
            fgets(pass, MAX_BUFFER_SIZE, stdin);
            pass[strcspn(pass, "\n")] = '\0';

            int c = 0;
            do
            {
                printf("New Client Folder Name: ");
                fgets(folder, MAX_BUFFER_SIZE, stdin);
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
                        c = 1; // Cho phép chọn lại nếu thư mục đã tồn tại
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
            fgets(username, MAX_BUFFER_SIZE, stdin);
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
            fgets(username, MAX_BUFFER_SIZE, stdin);
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
