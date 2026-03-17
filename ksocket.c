#include"ksocket.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/shm.h>
#include<time.h>

int ktp_err=0;

Shared_Memory *sm=NULL;

void attach_shm()
{
    if (sm == NULL) {
        int shmid = shmget(5678, sizeof(Shared_Memory), 0666);
        if (shmid < 0) {
            exit(1);
        }
        sm = (Shared_Memory *)shmat(shmid, NULL, 0);
        if (sm == (void *)-1) {
            exit(1);
        }
    }
}

int dropMessage(float p)
{
    float r= (float)rand()/(float)RAND_MAX;
    if(r<p)
    return 1;

    return 0;
}

int k_socket(int domain,int type,int protocol)
{
    if(type!=SOCK_KTP)
    return -1;
    attach_shm();

    for(int i=0;i<MAX_KTP_SOCK;i++)
    {
        pthread_mutex_lock(&sm->sockets[i].mutex);
        if(!sm->sockets[i].is_allocated)
        {
            sm->sockets[i].is_allocated=true;
            sm->sockets[i].pid=getpid();
            sm->sockets[i].udp_sockfd = -1;
            sm->sockets[i].is_bound = false;
            sm->sockets[i].swnd.size=MAX_WIN_SIZE;
            sm->sockets[i].rwnd.size=MAX_WIN_SIZE;
            sm->sockets[i].nospace = false;

            sm->sockets[i].rwnd.expected_seq[0] = 1; 
            sm->sockets[i].rwnd.expected_seq[1] = 1; 
            sm->sockets[i].swnd.unacked_seq[0] = 1;

            pthread_mutex_unlock(&sm->sockets[i].mutex);
            return i;
        }
        pthread_mutex_unlock(&sm->sockets[i].mutex);
    }

    ktp_err=ENOSPACE;
    return -1;
}

int k_bind(int sockfd,const struct sockaddr *srcaddr,socklen_t src_len,const struct sockaddr *destaddr,socklen_t dest_len)
{
    attach_shm();
    pthread_mutex_lock(&sm->sockets[sockfd].mutex);

    memcpy(&sm->sockets[sockfd].local_addr, srcaddr, src_len);
    memcpy(&sm->sockets[sockfd].remote_addr, destaddr, dest_len);
    sm->sockets[sockfd].is_bound = true;

    pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
    
    while (sm->sockets[sockfd].udp_sockfd == -1) 
    {
        usleep(10000); 
    }
    return 0;
}

int k_sendto(int sockfd,const void *buf,size_t len,int flag,const struct sockaddr *dest_addr,socklen_t dest_len)
{
    attach_shm();
    pthread_mutex_lock(&sm->sockets[sockfd].mutex);

    struct sockaddr_in *dest=(struct sockaddr_in *)dest_addr;
    if(dest->sin_addr.s_addr != sm->sockets[sockfd].remote_addr.sin_addr.s_addr || dest->sin_port != sm->sockets[sockfd].remote_addr.sin_port)
    {
        ktp_err=ENOTBOUND;
        pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
        return -1;
    }

    bool space_found=false;
    for(int i=0;i<MAX_WIN_SIZE;i++)
    {
        if(!sm->sockets[sockfd].send_buffer[i].is_valid)
        {
            memcpy(sm->sockets[sockfd].send_buffer[i].payload,buf,len>MSG_SIZE ? MSG_SIZE : len);
            sm->sockets[sockfd].send_buffer[i].is_valid=true;
            sm->sockets[sockfd].send_buffer[i].send_time=0;
            sm->sockets[sockfd].send_buffer[i].header.seq_num = sm->sockets[sockfd].swnd.unacked_seq[0]++;
            if(sm->sockets[sockfd].swnd.unacked_seq[0] == 0) sm->sockets[sockfd].swnd.unacked_seq[0] = 1;
            space_found=true;
            break;
        }
    }

    if(!space_found)
    {
        ktp_err=ENOSPACE;
        pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
        return -1;
    }

    pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
    return len;
}

int k_recvfrom(int sockfd, void *buf,size_t len ,int flags,struct sockaddr *srcaddr,socklen_t *src_len)
{
    attach_shm();
    pthread_mutex_lock(&sm->sockets[sockfd].mutex);
    uint8_t exp_seq = sm->sockets[sockfd].rwnd.expected_seq[1];

    for(int i=0;i<MAX_WIN_SIZE;i++)
    {
        if(sm->sockets[sockfd].recv_buffer[i].is_valid && sm->sockets[sockfd].recv_buffer[i].header.seq_num == exp_seq)
        {
            memcpy(buf,sm->sockets[sockfd].recv_buffer[i].payload,len>MSG_SIZE ? MSG_SIZE:len);
            sm->sockets[sockfd].recv_buffer[i].is_valid=false;
            sm->sockets[sockfd].rwnd.size++;
            sm->sockets[sockfd].rwnd.expected_seq[1]++;
            if(sm->sockets[sockfd].rwnd.expected_seq[1] == 0) sm->sockets[sockfd].rwnd.expected_seq[1] = 1;

            pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
            return len;
        }
    }
    ktp_err=ENOTMESSAGE;
    pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
    return -1;
}

int k_close(int sockfd)
{
   bool pending = true;
    while (pending) {
        pending = false;
        pthread_mutex_lock(&sm->sockets[sockfd].mutex);
        for (int i = 0; i < MAX_WIN_SIZE; i++) {
            if (sm->sockets[sockfd].send_buffer[i].is_valid) {
                pending = true;
                break;
            }
        }
        pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
        if (pending) usleep(10000); 
    }

    pthread_mutex_lock(&sm->sockets[sockfd].mutex);
    sm->sockets[sockfd].is_allocated = false;
    pthread_mutex_unlock(&sm->sockets[sockfd].mutex);
    return 0;
}