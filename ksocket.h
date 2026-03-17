#ifndef KSOCKET_H
#define KSOCKET_H

#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdbool.h>

#define SOCK_KTP 3
#define MSG_SIZE 512
#define MAX_WIN_SIZE 10
#define MAX_KTP_SOCK 10

#define T 5
#define PROB 0.05
#define ENOSPACE 1001
#define ENOTBOUND 1002
#define ENOTMESSAGE 1003

extern int ktp_err;

typedef struct {
    uint8_t seq_num;
    bool is_ack;
    uint8_t rwnd_size;
}KTP_header;

typedef struct {
    KTP_header header;
    char payload[MSG_SIZE];
    time_t send_time;
    bool is_valid;
}KTP_msg;

typedef struct {
    uint8_t size;
    uint8_t unacked_seq[MAX_WIN_SIZE];
}Senderwnd;

typedef struct {
    uint8_t size;
    uint8_t expected_seq[MAX_WIN_SIZE];
}Receiverwnd;

typedef struct {
    bool is_allocated;
    pid_t pid;
    int udp_sockfd;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    bool is_bound;
    
    KTP_msg send_buffer[MAX_WIN_SIZE];
    KTP_msg recv_buffer[MAX_WIN_SIZE];
    
    Senderwnd swnd;
    Receiverwnd rwnd;
    
    bool nospace;
    pthread_mutex_t mutex;
} KTP_Socket_Entry;

typedef struct {
    KTP_Socket_Entry sockets[MAX_KTP_SOCK];
} Shared_Memory;

int k_socket(int domain,int type,int protocol);
int k_bind(int sockfd,const struct sockaddr *srcaddr,socklen_t src_len,const struct sockaddr *destaddr,socklen_t dest_len);
int k_sendto(int sockfd,const void *buf,size_t len,int flag,const struct sockaddr *destaddr,socklen_t dest_len);
int k_recvfrom(int sockfd,void *buf,size_t len,int flag,struct sockaddr *srcaddr,socklen_t *src_len);
int k_close(int sockfd);
int dropMessage(float p);

#endif