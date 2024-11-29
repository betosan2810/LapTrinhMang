#include <arpa/inet.h>  // inet_addr(), inet_ntoa(), htons()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX 1024

void menu() {
    printf("\n============MENU===========\n");
    printf("|1. LIST Files             |\n");
    printf("|2. Change Directory (CWD) |\n");
    printf("|3. Show Current Directory (PWD) |\n");
    printf("|4. Quit                   |\n");
    printf("===========================\n");
    printf("Enter your choice: ");
}

void send_command(int sockfd, const char *command) {
    // Gửi lệnh tới server
    send(sockfd, command, strlen(command) + 1, 0);
}

void receive_response(int sockfd) {
    char buffer[MAX];
    int bytes_received = recv(sockfd, buffer, MAX, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';  // Kết thúc chuỗi
        printf("Server: %s\n", buffer);
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[MAX];
    int choice;
    char username[MAX], password[MAX];

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
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));  // Cổng từ đối số
    server_addr.sin_addr.s_addr = inet_addr(argv[1]); // IP máy chủ

    // Kết nối đến server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sockfd);
        return 1;
    }

    // Nhận thông báo chào mừng từ server
    receive_response(sockfd);

    // Nhập lệnh USER: Tên người dùng
    printf("Enter Username: ");
    fgets(username, MAX, stdin);
    username[strcspn(username, "\n")] = '\0';  // Loại bỏ ký tự newline
    snprintf(buffer, MAX, "USER %s", username);
    send_command(sockfd, buffer);
    receive_response(sockfd);

    // Nhập lệnh PASS: Mật khẩu
    printf("Enter Password: ");
    fgets(password, MAX, stdin);
    password[strcspn(password, "\n")] = '\0';  // Loại bỏ ký tự newline
    snprintf(buffer, MAX, "PASS %s", password);
    send_command(sockfd, buffer);
    receive_response(sockfd);

    // Kiểm tra xem xác thực thành công không
    printf("Login successful.\n");

    // Menu và xử lý lệnh từ client
    while (1) {
        menu();
        scanf("%d", &choice);
        getchar();  // Đọc ký tự newline còn lại sau scanf

        switch (choice) {
            case 1: {
                // Nhập lệnh LIST
                printf("Enter command (LIST): ");
                fgets(buffer, MAX, stdin);
                buffer[strcspn(buffer, "\n")] = 0;  // Loại bỏ ký tự newline
                send_command(sockfd, buffer);
                receive_response(sockfd);
                break;
            }

            case 2: {
                // Nhập lệnh CWD để thay đổi thư mục
                printf("Enter command (CWD <directory>): ");
                fgets(buffer, MAX, stdin);
                buffer[strcspn(buffer, "\n")] = 0;  // Loại bỏ ký tự newline
                send_command(sockfd, buffer);
                receive_response(sockfd);
                break;
            }

            case 3: {
                // Nhập lệnh PWD để hiển thị thư mục làm việc hiện tại
                printf("Enter command (PWD): ");
                fgets(buffer, MAX, stdin);
                buffer[strcspn(buffer, "\n")] = 0;  // Loại bỏ ký tự newline
                send_command(sockfd, buffer);
                receive_response(sockfd);
                break;
            }

            case 4: {
                // Nhập lệnh QUIT để ngắt kết nối
                printf("Enter command (QUIT): ");
                fgets(buffer, MAX, stdin);
                buffer[strcspn(buffer, "\n")] = 0;  // Loại bỏ ký tự newline
                send_command(sockfd, buffer);
                receive_response(sockfd);

                // Đóng kết nối và thoát
                close(sockfd);
                return 0;
            }

            default:
                printf("Invalid choice. Please select again.\n");
                break;
        }
    }

    return 0;
}
