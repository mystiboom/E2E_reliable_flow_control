#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <time.h>
#include <string.h>

extern Shared_Memory *sm;

void *thread_R(void *arg) {
    fd_set fds;
    struct timeval tmo;
    
    while(1) {
        FD_ZERO(&fds);
        int mfd = -1;
        
        for (int i = 0; i < MAX_KTP_SOCK; i++) {
            if (sm->sockets[i].is_allocated && sm->sockets[i].is_bound) {
                if (sm->sockets[i].udp_sockfd == -1) {
                    pthread_mutex_lock(&sm->sockets[i].mutex);
                    int sfd = socket(AF_INET, SOCK_DGRAM, 0);
                    bind(sfd, (struct sockaddr *)&sm->sockets[i].local_addr, sizeof(struct sockaddr_in));
                    sm->sockets[i].udp_sockfd = sfd;
                    pthread_mutex_unlock(&sm->sockets[i].mutex);
                }
                if (sm->sockets[i].udp_sockfd != -1) {
                    FD_SET(sm->sockets[i].udp_sockfd, &fds);
                    if (sm->sockets[i].udp_sockfd > mfd) {
                        mfd = sm->sockets[i].udp_sockfd;
                    }
                }
            }
        }
        
        if (mfd == -1) {
            usleep(100000);
            continue;
        }
        
        tmo.tv_sec = 1;
        tmo.tv_usec = 0;
        int act = select(mfd + 1, &fds, NULL, NULL, &tmo);
        
        if (act > 0) {
            for (int i = 0; i < MAX_KTP_SOCK; i++) {
                if (sm->sockets[i].is_allocated && sm->sockets[i].udp_sockfd != -1 && FD_ISSET(sm->sockets[i].udp_sockfd, &fds)) {
                    KTP_msg ims;
                    struct sockaddr_in sad;
                    socklen_t sln = sizeof(sad);
                    
                    recvfrom(sm->sockets[i].udp_sockfd, &ims, sizeof(KTP_msg), 0, (struct sockaddr *)&sad, &sln);
                    if (dropMessage(PROB) == 1) continue;
                    
                    pthread_mutex_lock(&sm->sockets[i].mutex);
                    if (!ims.header.is_ack) {
                        uint8_t nxp = sm->sockets[i].rwnd.expected_seq[0];
                        uint8_t seq = ims.header.seq_num;
                        
                        uint8_t dist = seq - nxp;
                        if (seq < nxp) dist--;
                        
                        if (dist < MAX_WIN_SIZE) {
                            bool dup = false;
                            for (int j = 0; j < MAX_WIN_SIZE; j++) {
                                if (sm->sockets[i].recv_buffer[j].is_valid && sm->sockets[i].recv_buffer[j].header.seq_num == seq) {
                                    dup = true; 
                                    break;
                                }
                            }
                            if (!dup && sm->sockets[i].rwnd.size > 0) {
                                sm->sockets[i].nospace = false;
                                for(int j = 0; j < MAX_WIN_SIZE; j++) {
                                    if(!sm->sockets[i].recv_buffer[j].is_valid) {
                                        memcpy(&sm->sockets[i].recv_buffer[j], &ims, sizeof(KTP_msg));
                                        sm->sockets[i].recv_buffer[j].is_valid = true;
                                        
                                        printf("Packet new received: seq %d\n", seq);
                                        fflush(stdout); // Force write to log.txt
                                        
                                        break;
                                    }
                                }
                                sm->sockets[i].rwnd.size--;
                                if (sm->sockets[i].rwnd.size == 0) sm->sockets[i].nospace = true;
                            }
                            
                            bool adv = true;
                            while(adv) {
                                adv = false;
                                for(int j = 0; j < MAX_WIN_SIZE; j++) {
                                    if (sm->sockets[i].recv_buffer[j].is_valid && sm->sockets[i].recv_buffer[j].header.seq_num == sm->sockets[i].rwnd.expected_seq[0]) {
                                        sm->sockets[i].rwnd.expected_seq[0]++;
                                        if(sm->sockets[i].rwnd.expected_seq[0] == 0) sm->sockets[i].rwnd.expected_seq[0] = 1;
                                        adv = true;
                                        break;
                                    }
                                }
                            }
                        }
                        
                        KTP_msg ack;
                        memset(&ack, 0, sizeof(KTP_msg));
                        ack.header.is_ack = true;
                        ack.header.seq_num = sm->sockets[i].rwnd.expected_seq[0] - 1;
                        if(ack.header.seq_num == 0) ack.header.seq_num = 255;
                        ack.header.rwnd_size = sm->sockets[i].rwnd.size;
                        sendto(sm->sockets[i].udp_sockfd, &ack, sizeof(KTP_msg), 0, (struct sockaddr *)&sm->sockets[i].remote_addr, sizeof(struct sockaddr_in));
                    } else {
                        uint8_t aseq = ims.header.seq_num;
                        sm->sockets[i].swnd.size = ims.header.rwnd_size;
                        
                        printf("ACK received: seq %d\n", aseq);
                        fflush(stdout); // Force write to log.txt
                        
                        for (int j = 0; j < MAX_WIN_SIZE; j++) {
                            if (sm->sockets[i].send_buffer[j].is_valid) {
                                uint8_t ssq = sm->sockets[i].send_buffer[j].header.seq_num;
                                uint8_t ack_dist = aseq - ssq;
                                if (aseq < ssq) ack_dist--;
                                
                                if (ack_dist < 128) {
                                    sm->sockets[i].send_buffer[j].is_valid = false;
                                }
                            }
                        }
                    }
                    pthread_mutex_unlock(&sm->sockets[i].mutex);
                }
            }
        } else if (act == 0) {
            for (int i = 0; i < MAX_KTP_SOCK; i++) {
                if (sm->sockets[i].is_allocated && sm->sockets[i].udp_sockfd != -1) {
                    pthread_mutex_lock(&sm->sockets[i].mutex);
                    if (sm->sockets[i].nospace && sm->sockets[i].rwnd.size > 0) {
                        KTP_msg dak;
                        memset(&dak, 0, sizeof(KTP_msg));
                        dak.header.is_ack = true;
                        dak.header.seq_num = sm->sockets[i].rwnd.expected_seq[0] - 1;
                        if(dak.header.seq_num == 0) dak.header.seq_num = 255;
                        dak.header.rwnd_size = sm->sockets[i].rwnd.size;
                        sendto(sm->sockets[i].udp_sockfd, &dak, sizeof(KTP_msg), 0, (struct sockaddr *)&sm->sockets[i].remote_addr, sizeof(struct sockaddr_in));
                        sm->sockets[i].nospace = false;
                    }
                    pthread_mutex_unlock(&sm->sockets[i].mutex);
                }
            }
        }
    }
    return NULL;
}

void *thread_S(void *arg) {
    struct timespec tsp;
    tsp.tv_sec = T / 2;
    tsp.tv_nsec = (T % 2) * 500000000; 
    
    while(1) {
        nanosleep(&tsp, NULL);
        time_t ctm = time(NULL);
        
        for (int i = 0; i < MAX_KTP_SOCK; i++) {
            if (sm->sockets[i].is_allocated && sm->sockets[i].udp_sockfd != -1) {
                pthread_mutex_lock(&sm->sockets[i].mutex);
                
                int ifl = 0;
                for (int j = 0; j < MAX_WIN_SIZE; j++) {
                    if (sm->sockets[i].send_buffer[j].is_valid && sm->sockets[i].send_buffer[j].send_time > 0) {
                        ifl++;
                        if (ctm - sm->sockets[i].send_buffer[j].send_time >= T) {
                            sendto(sm->sockets[i].udp_sockfd, &sm->sockets[i].send_buffer[j], sizeof(KTP_msg), 0, (struct sockaddr *)&sm->sockets[i].remote_addr, sizeof(struct sockaddr_in));
                            sm->sockets[i].send_buffer[j].send_time = time(NULL);
                            
                            printf("Packet retransmitted: seq %d\n", sm->sockets[i].send_buffer[j].header.seq_num);
                            fflush(stdout); // Force write to log.txt
                        }
                    }
                }
                
                int csd = sm->sockets[i].swnd.size - ifl;
                while (csd > 0) {
                    int next_to_send = -1;
                    uint8_t min_seq = 0;
                    bool found = false;
                    
                    for (int j = 0; j < MAX_WIN_SIZE; j++) {
                        if (sm->sockets[i].send_buffer[j].is_valid && sm->sockets[i].send_buffer[j].send_time == 0) {
                            uint8_t seq = sm->sockets[i].send_buffer[j].header.seq_num;
                            if (!found) {
                                min_seq = seq;
                                next_to_send = j;
                                found = true;
                            } else {
                                uint8_t d = seq - min_seq;
                                if (seq < min_seq) d--;
                                if (d > 128) {
                                    min_seq = seq;
                                    next_to_send = j;
                                }
                            }
                        }
                    }
                    
                    if (found) {
                        sm->sockets[i].send_buffer[next_to_send].header.is_ack = false;
                        sm->sockets[i].send_buffer[next_to_send].send_time = time(NULL);
                        sendto(sm->sockets[i].udp_sockfd, &sm->sockets[i].send_buffer[next_to_send], sizeof(KTP_msg), 0, (struct sockaddr *)&sm->sockets[i].remote_addr, sizeof(struct sockaddr_in));
                        csd--;
                        
                        printf("Packet transmitted: seq %d\n", sm->sockets[i].send_buffer[next_to_send].header.seq_num);
                        fflush(stdout); // Force write to log.txt
                        
                    } else {
                        break;
                    }
                }
                
                pthread_mutex_unlock(&sm->sockets[i].mutex);
            }
        }
    }
    return NULL;
}

void garbage_collector() {
    pid_t gpid = fork();
    if (gpid == 0) {
        while(1) {
            sleep(10);
            for (int i = 0; i < MAX_KTP_SOCK; i++) {
                if (sm->sockets[i].is_allocated) {
                    if (kill(sm->sockets[i].pid, 0) == -1) {
                        pthread_mutex_lock(&sm->sockets[i].mutex);
                        if (sm->sockets[i].udp_sockfd != -1) {
                            close(sm->sockets[i].udp_sockfd);
                        }
                        sm->sockets[i].is_allocated = false;
                        pthread_mutex_unlock(&sm->sockets[i].mutex);
                    }
                }
            }
        }
    }
}

int main() {
    int sid = shmget(5678, sizeof(Shared_Memory), IPC_CREAT | 0666);
    if (sid < 0) exit(1);
    
    sm = (Shared_Memory *)shmat(sid, NULL, 0);
    if (sm == (void *)-1) exit(1);
    
    pthread_mutexattr_t mtr;
    pthread_mutexattr_init(&mtr);
    pthread_mutexattr_setpshared(&mtr, PTHREAD_PROCESS_SHARED);
    
    for (int i = 0; i < MAX_KTP_SOCK; i++) {
        sm->sockets[i].is_allocated = false;
        sm->sockets[i].udp_sockfd = -1;
        pthread_mutex_init(&sm->sockets[i].mutex, &mtr);
        for(int j = 0; j < MAX_WIN_SIZE; j++) {
            sm->sockets[i].send_buffer[j].is_valid = false;
            sm->sockets[i].send_buffer[j].send_time = 0;
            sm->sockets[i].recv_buffer[j].is_valid = false;
        }
    }
    
    garbage_collector();
    srand(time(NULL));
    
    pthread_t rtd, std;
    pthread_create(&rtd, NULL, thread_R, NULL);
    pthread_create(&std, NULL, thread_S, NULL);
    
    pthread_join(rtd, NULL);
    pthread_join(std, NULL);
    
    return 0;
}