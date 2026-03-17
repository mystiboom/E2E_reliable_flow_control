#include "ksocket.h"
#include <stdio.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>

int main() {
    int fd = k_socket(AF_INET, SOCK_KTP, 0);
    if (fd < 0) {
        return 1;
    }

    struct sockaddr_in s1, s2;
    s1.sin_family = AF_INET;
    s1.sin_port = htons(30041);
    inet_pton(AF_INET, "127.0.0.1", &s1.sin_addr);

    s2.sin_family = AF_INET;
    s2.sin_port = htons(30040);
    inet_pton(AF_INET, "127.0.0.1", &s2.sin_addr);

    if (k_bind(fd, (struct sockaddr*)&s1, sizeof(s1), (struct sockaddr*)&s2, sizeof(s2)) < 0) {
        return 1;
    }

    int f = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
    if (f < 0) {
        return 1;
    }
    char buf[512];
    socklen_t len = sizeof(s2);
    while (1) {
        int r = k_recvfrom(fd, buf, 512, 0, (struct sockaddr*)&s2, &len);
        if (r > 0) {
            write(f, buf, r);
        } else {
            usleep(10000);
        }
    }
    close(f);
    k_close(fd);
    return 0;
}