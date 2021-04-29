#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <pthread.h>

#include <errno.h>
#include <fcntl.h>

#define BUF_SIZE 4	//for many times read data not read data one time
#define EPOLL_SIZE 1023

int clnt_cnt=0;
int clnt_socks[EPOLL_SIZE];
pthread_mutex_t mutex;

struct socket_node{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr;
	socklen_t adr_sz;
};

struct epoll_node{
	struct epoll_event *ep_events;
	struct epoll_event event;
	int epfd, event_cnt;
};

int setnonblockingmode(int fd)
{
	int flag=fcntl(fd, F_GETFL, 0);
	if(flag<0) return -1;
	if(0>fcntl(fd, F_SETFL, flag|O_NONBLOCK)) return -1;
	return 0;
}

void error_handling(char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
}


int epoll_init(struct epoll_node *epo_node, int serv_sock)
{	
	epo_node->epfd=epoll_create(EPOLL_SIZE);
	if(NULL==(epo_node->ep_events=malloc(sizeof(struct epoll_event)*EPOLL_SIZE))) return -1;

	if(0>setnonblockingmode(serv_sock)) return -1;//not blocking mode
	epo_node->event.events=EPOLLIN;
	epo_node->event.data.fd=serv_sock;
	if(0>epoll_ctl(epo_node->epfd, EPOLL_CTL_ADD, serv_sock, &epo_node->event)) return -1;

	return 0;
}

int serv_init(struct socket_node *sock_node, const char *port)
{
	if(0>(sock_node->serv_sock=socket(PF_INET, SOCK_STREAM, 0))) return -1;
	memset(&sock_node->serv_adr, 0, sizeof(sock_node->serv_adr));
	sock_node->serv_adr.sin_family=AF_INET;
	sock_node->serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
	sock_node->serv_adr.sin_port=htons(atoi(port));

	if(0>bind(sock_node->serv_sock, (struct sockaddr *)&sock_node->serv_adr, sizeof(sock_node->serv_adr)))
	{
		error_handling("bind() error"); 
		close(sock_node->serv_sock);
		return -1;
	}

	if(0>listen(sock_node->serv_sock, 5)) 
	{
		error_handling("listen() error");
		close(sock_node->serv_sock);
		return -1;
	}	
	
	return 0;
}

void send_msg(char *msg, int len)
{
	int i;
	pthread_mutex_lock(&mutex);
	for(i=0;i<clnt_cnt;++i) write(clnt_socks[i], msg, len);
	pthread_mutex_unlock(&mutex);
}

int main(int argc, char *argv[])
{
	if(2!=argc) 
	{
		printf("Usage: %s <port>\n", argv[0]);
		return -1;
	}
	
	struct socket_node sock_node;
	struct epoll_node epo_node;
	if(0>serv_init(&sock_node, argv[1])) exit(-1);
	if(0>epoll_init(&epo_node, sock_node.serv_sock)) exit(-1); 

	int str_len, i;
	char buf[BUF_SIZE];
	
	while(1)
	{
		epo_node.event_cnt=epoll_wait(epo_node.epfd, epo_node.ep_events, EPOLL_SIZE, -1);
		if(-1==epo_node.event_cnt)
		{
			puts("epoll_wait() error");
			break;
		}

		for(i=0;i<epo_node.event_cnt; ++i)
		{
			if(epo_node.ep_events[i].data.fd==sock_node.serv_sock)
			{
				sock_node.adr_sz=sizeof(sock_node.clnt_adr);
				sock_node.clnt_sock=accept(sock_node.serv_sock, (struct sockaddr *)&sock_node.clnt_adr, &sock_node.adr_sz);

				pthread_mutex_lock(&mutex);
				clnt_socks[clnt_cnt++]=sock_node.clnt_sock;
				pthread_mutex_unlock(&mutex);

				setnonblockingmode(sock_node.clnt_sock);//not blocking mode
				epo_node.event.events=EPOLLIN|EPOLLET;//edge trigger mode
				epo_node.event.data.fd=sock_node.clnt_sock;
				epoll_ctl(epo_node.epfd, EPOLL_CTL_ADD, sock_node.clnt_sock, &epo_node.event);
				printf("connected client: %d\n", sock_node.clnt_sock);
			}
			else
			{
				while(1)//edge trigger mode so need read data many times
				{
					str_len=read(epo_node.ep_events[i].data.fd, buf, BUF_SIZE);
					if(str_len == 0)
					{
						epoll_ctl(epo_node.epfd, EPOLL_CTL_DEL, epo_node.ep_events[i].data.fd, NULL);
						close(epo_node.ep_events[i].data.fd);
						printf("closed clinet:%d\n", epo_node.ep_events[i].data.fd);

						pthread_mutex_lock(&mutex);	//remove diconnected client
						for(int j=0; j<clnt_cnt; ++j)
						{
							if(epo_node.ep_events[i].data.fd==clnt_socks[j])
							{
								while(j++<clnt_cnt-1)
								clnt_socks[j]=clnt_socks[j+1];
								break;
							}
						}
						clnt_cnt--;
						pthread_mutex_unlock(&mutex);


						break;
					}
					else if(str_len<0)
					{
						if(errno==EAGAIN) break;
					}
					else
					{
						//write(epo_node.ep_events[i].data.fd, buf, str_len);
						send_msg(buf, str_len);
					}

				}
				
			}
		}
	}	



	free(epo_node.ep_events);
	close(sock_node.serv_sock);
	close(epo_node.epfd);
	return 0;
}
