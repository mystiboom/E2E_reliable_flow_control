#include "ksocket.h"
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

int main()
{
    int fd = k_socket(AF_INET, SOCK_KTP, 0);
    if (fd < 0)
    {
        return 1;
    }

    struct sockaddr_in s1, s2;
    s1.sin_family = AF_INET;
    s1.sin_port = htons(30040);
    inet_pton(AF_INET, "127.0.0.1", &s1.sin_addr);

    s2.sin_family = AF_INET;
    s2.sin_port = htons(30041);
    inet_pton(AF_INET, "127.0.0.1", &s2.sin_addr);

    if (k_bind(fd, (struct sockaddr*)&s1, sizeof(s1), (struct sockaddr*)&s2, sizeof(s2)) < 0)
        return 1;

    int f = open("test.txt", O_RDONLY);
    if (f < 0)
        return 1;

    char buf[512];
    int r;
    while ((r = read(f, buf, 512)) > 0)
    {
        while (k_sendto(fd, buf, r, 0, (struct sockaddr*)&s2, sizeof(s2)) < 0)
        {
            usleep(100000);
        }
    }
    close(f);
    k_close(fd);
    return 0;
}