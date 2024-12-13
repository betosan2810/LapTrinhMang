// client.c
#include <arpa/inet.h>     // inet_addr(), inet_ntoa(), htons(), inet_pton()
#include <errno.h>         // errno
#include <netinet/in.h>    // struct sockaddr_in
#include <stdio.h>
#include <stdlib.h>        // atoi()
#include <string.h>        // memset(), strlen(), strcmp(), strncpy(), strtok()
#include <sys/socket.h>    // socket(), connect(), send(), recv()
#include <sys/stat.h>      // stat()
#include <sys/types.h>
#include <unistd.h>        // close()
#include <fcntl.h>         // open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC

#define MAX 8192

// Hàm hiển thị menu
void menu() {
    printf("\n============MENU===========\n");
    printf("|1. LIST Files             |\n");
    printf("|2. Change Directory (CWD) |\n");
    printf("|3. Show Current Directory (PWD) |\n");
    printf("|4. Download File (RETR)   |\n");
    printf("|5. Upload File (STOR)     |\n");
    printf("|6. Quit                   |\n");
    printf("===========================\n");
    printf("Enter your choice: ");
}

// Hàm gửi lệnh tới server
int send_command(int sockfd, const char *command) {
    int total_sent = 0;
    int len = strlen(command);
    while (total_sent < len) {
        int sent = send(sockfd, command + total_sent, len - total_sent, 0);
        if (sent < 0) {
            perror("Failed to send command");
            return -1;
        }
        total_sent += sent;
    }
    return 0;
}

// Hàm nhận phản hồi từ server
int receive_response(int sockfd, char *buffer, size_t size) {
    memset(buffer, 0, size);
    int bytes_received = recv(sockfd, buffer, size - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Server: %s", buffer);
        return bytes_received;
    } else if (bytes_received == 0) {
        printf("Server closed the connection.\n");
        return 0;
    } else {
        perror("recv failed");
        return -1;
    }
}

// Hàm phân tích phản hồi PASV để lấy địa chỉ IP và cổng
int parse_pasv_response(const char *response, char *ip, int *port) {
    // Tìm vị trí '(' và ')'
    const char *start = strchr(response, '(');
    const char *end = strchr(response, ')');
    if (!start || !end) {
        return -1;
    }

    // Tách phần bên trong dấu ()
    char numbers[128];
    strncpy(numbers, start + 1, end - start - 1);
    numbers[end - start - 1] = '\0';

    // Tách các số bằng ','
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(numbers, "%d,%d,%d,%d,%d,%d", 
               &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return -1;
    }

    // Gộp địa chỉ IP
    snprintf(ip, 64, "%d.%d.%d.%d", h1, h2, h3, h4);

    // Tính cổng
    *port = p1 * 256 + p2;

    return 0;
}

// Hàm kết nối đến socket dữ liệu
int connect_data_socket(const char *ip, int port) {
    int data_sock;
    struct sockaddr_in data_addr;

    // Tạo socket dữ liệu
    data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0) {
        perror("Data socket creation failed");
        return -1;
    }

    // Thiết lập thông tin kết nối
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &data_addr.sin_addr) <= 0) {
        perror("Invalid data IP address");
        close(data_sock);
        return -1;
    }

    // Kết nối đến server trên cổng dữ liệu
    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("Data connection failed");
        close(data_sock);
        return -1;
    }

    return data_sock;
}

// Hàm tải xuống tệp tin
void download_file(int control_sock) {
    char buffer[MAX];
    char filename[MAX];
    char ip[64];
    int port;
    int data_sock;
    FILE *fp;

    printf("Enter filename to download: ");
    fgets(filename, MAX, stdin);
    filename[strcspn(filename, "\n")] = '\0';  // Loại bỏ ký tự newline

    // Gửi lệnh PASV
    if (send_command(control_sock, "PASV\r\n") < 0) return;

    // Nhận phản hồi PASV
    char pasv_response[MAX];
    int bytes = receive_response(control_sock, pasv_response, MAX);
    if (bytes <= 0) return;

    // Phân tích phản hồi PASV
    if (parse_pasv_response(pasv_response, ip, &port) != 0) {
        printf("Failed to parse PASV response.\n");
        return;
    }

    // Kết nối đến socket dữ liệu
    data_sock = connect_data_socket(ip, port);
    if (data_sock < 0) return;

    // Gửi lệnh RETR
    snprintf(buffer, MAX, "RETR %s\r\n", filename);
    if (send_command(control_sock, buffer) < 0) {
        close(data_sock);
        return;
    }

    // Nhận phản hồi cho lệnh RETR
    bytes = receive_response(control_sock, buffer, MAX);
    if (bytes <= 0) {
        close(data_sock);
        return;
    }

    // Kiểm tra phản hồi có bắt đầu bằng 150 không (150 Opening data connection)
    if (strncmp(buffer, "150", 3) != 0) {
        close(data_sock);
        return;
    }

    // Mở tệp tin để ghi dữ liệu
    fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to open local file for writing");
        close(data_sock);
        // Gửi lệnh QUIT để đóng kết nối dữ liệu nếu cần
        return;
    }

    // Nhận dữ liệu từ socket dữ liệu và ghi vào tệp tin local
    while ((bytes = recv(data_sock, buffer, MAX, 0)) > 0) {
        fwrite(buffer, sizeof(char), bytes, fp);
    }

    if (bytes < 0) {
        perror("Error receiving file data");
    } else {
        printf("Downloaded %s successfully.\n", filename);
    }

    fclose(fp);
    close(data_sock);

    // Nhận phản hồi hoàn tất từ server
    receive_response(control_sock, buffer, MAX);
}

// Hàm tải lên tệp tin
void upload_file(int control_sock) {
    char buffer[MAX];
    char filename[MAX];
    char ip[64];
    int port;
    int data_sock;
    FILE *fp;

    printf("Enter filename to upload: ");
    fgets(filename, MAX, stdin);
    filename[strcspn(filename, "\n")] = '\0';  // Loại bỏ ký tự newline

    // Kiểm tra xem tệp tin local có tồn tại không
    fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open local file for reading");
        return;
    }

    // Gửi lệnh PASV
    if (send_command(control_sock, "PASV\r\n") < 0) {
        fclose(fp);
        return;
    }

    // Nhận phản hồi PASV
    char pasv_response[MAX];
    int bytes = receive_response(control_sock, pasv_response, MAX);
    if (bytes <= 0) {
        fclose(fp);
        return;
    }

    // Phân tích phản hồi PASV
    if (parse_pasv_response(pasv_response, ip, &port) != 0) {
        printf("Failed to parse PASV response.\n");
        fclose(fp);
        return;
    }

    // Kết nối đến socket dữ liệu
    data_sock = connect_data_socket(ip, port);
    if (data_sock < 0) {
        fclose(fp);
        return;
    }

    // Gửi lệnh STOR
    snprintf(buffer, MAX, "STOR %s\r\n", filename);
    if (send_command(control_sock, buffer) < 0) {
        close(data_sock);
        fclose(fp);
        return;
    }

    // Nhận phản hồi cho lệnh STOR
    bytes = receive_response(control_sock, buffer, MAX);
    if (bytes <= 0) {
        close(data_sock);
        fclose(fp);
        return;
    }

    // Kiểm tra phản hồi có bắt đầu bằng 150 không (150 Opening data connection)
    if (strncmp(buffer, "150", 3) != 0) {
        close(data_sock);
        fclose(fp);
        return;
    }

    // Gửi dữ liệu từ tệp tin local qua socket dữ liệu
    while ((bytes = fread(buffer, sizeof(char), MAX, fp)) > 0) {
        if (send(data_sock, buffer, bytes, 0) < 0) {
            perror("Failed to send file data");
            break;
        }
    }

    printf("Uploaded %s successfully.\n", filename);

    fclose(fp);
    close(data_sock);

    // Nhận phản hồi hoàn tất từ server
    receive_response(control_sock, buffer, MAX);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[MAX];
    int choice;
    char username[MAX];
    char password[MAX];

    if (argc != 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    // Tạo socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Thiết lập thông tin máy chủ
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));  // Cổng từ đối số
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        return 1;
    }

    // Kết nối đến server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sockfd);
        return 1;
    }

    // Nhận thông báo chào mừng từ server
    if (receive_response(sockfd, buffer, MAX) <= 0) {
        close(sockfd);
        return 1;
    }

    // Nhập lệnh USER: Tên người dùng
    printf("Enter Username: ");
    fgets(username, MAX, stdin);
    username[strcspn(username, "\n")] = '\0';  // Loại bỏ ký tự newline
    snprintf(buffer, MAX, "USER %s\r\n", username);
    if (send_command(sockfd, buffer) < 0) {
        close(sockfd);
        return 1;
    }

    // Nhận phản hồi cho lệnh USER
    if (receive_response(sockfd, buffer, MAX) <= 0) {
        close(sockfd);
        return 1;
    }

    // Nhập lệnh PASS: Mật khẩu
    printf("Enter Password: ");
    fgets(password, MAX, stdin);
    password[strcspn(password, "\n")] = '\0';  // Loại bỏ ký tự newline
    snprintf(buffer, MAX, "PASS %s\r\n", password);
    if (send_command(sockfd, buffer) < 0) {
        close(sockfd);
        return 1;
    }

    // Nhận phản hồi cho lệnh PASS
    if (receive_response(sockfd, buffer, MAX) <= 0) {
        close(sockfd);
        return 1;
    }

    // Kiểm tra xem đăng nhập thành công hay không
    if (strncmp(buffer, "230", 3) != 0) {
        printf("Login failed. Exiting.\n");
        close(sockfd);
        return 1;
    }

    // Menu và xử lý lệnh từ client
    while (1) {
        menu();
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            // Xóa buffer nhập sai
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }
        // Xóa ký tự newline còn lại sau scanf
        while (getchar() != '\n');

        switch (choice) {
            case 1: {
                // Gửi lệnh LIST
                // Gửi lệnh PASV trước
                if (send_command(sockfd, "PASV\r\n") < 0) {
                    break;
                }

                // Nhận phản hồi PASV
                char pasv_response[MAX];
                int bytes = receive_response(sockfd, pasv_response, MAX);
                if (bytes <= 0) {
                    break;
                }

                // Phân tích phản hồi PASV
                char ip[64];
                int port;
                if (parse_pasv_response(pasv_response, ip, &port) != 0) {
                    printf("Failed to parse PASV response.\n");
                    break;
                }

                // Kết nối đến socket dữ liệu
                int data_sock = connect_data_socket(ip, port);
                if (data_sock < 0) {
                    break;
                }

                // Gửi lệnh LIST
                if (send_command(sockfd, "LIST\r\n") < 0) {
                    close(data_sock);
                    break;
                }

                // Nhận phản hồi cho lệnh LIST
                bytes = receive_response(sockfd, buffer, MAX);
                if (bytes <= 0) {
                    close(data_sock);
                    break;
                }

                // Kiểm tra phản hồi có bắt đầu bằng 150 không (150 Opening data connection)
                if (strncmp(buffer, "150", 3) != 0) {
                    close(data_sock);
                    break;
                }

                // Nhận dữ liệu từ socket dữ liệu và hiển thị
                printf("Files:\n");
                while ((bytes = recv(data_sock, buffer, MAX - 1, 0)) > 0) {
                    buffer[bytes] = '\0';
                    printf("%s", buffer);
                }

                if (bytes < 0) {
                    perror("Error receiving LIST data");
                } else {
                    printf("\nLIST completed successfully.\n");
                }

                close(data_sock);

                // Nhận phản hồi hoàn tất từ server
                receive_response(sockfd, buffer, MAX);
                break;
            }

            case 2: {
                // Gửi lệnh CWD để thay đổi thư mục
                char directory[MAX];
                printf("Enter directory to change to: ");
                fgets(directory, MAX, stdin);
                directory[strcspn(directory, "\n")] = '\0';  // Loại bỏ ký tự newline
                snprintf(buffer, MAX, "CWD %s\r\n", directory);
                if (send_command(sockfd, buffer) < 0) {
                    break;
                }

                // Nhận phản hồi cho lệnh CWD
                if (receive_response(sockfd, buffer, MAX) <= 0) {
                    break;
                }

                break;
            }

            case 3: {
                // Gửi lệnh PWD để hiển thị thư mục làm việc hiện tại
                if (send_command(sockfd, "PWD\r\n") < 0) {
                    break;
                }

                // Nhận phản hồi cho lệnh PWD
                if (receive_response(sockfd, buffer, MAX) <= 0) {
                    break;
                }

                break;
            }

            case 4: {
                // Tải xuống tệp tin (RETR)
                download_file(sockfd);
                break;
            }

            case 5: {
                // Tải lên tệp tin (STOR)
                upload_file(sockfd);
                break;
            }

            case 6: {
                // Gửi lệnh QUIT để ngắt kết nối
                if (send_command(sockfd, "QUIT\r\n") < 0) {
                    break;
                }

                // Nhận phản hồi cho lệnh QUIT
                receive_response(sockfd, buffer, MAX);

                // Đóng kết nối và thoát
                close(sockfd);
                printf("Disconnected from server.\n");
                return 0;
            }

            default:
                printf("Invalid choice. Please select again.\n");
                break;
        }
    }

    // Đóng kết nối khi kết thúc chương trình
    close(sockfd);
    return 0;
}
