#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>

#define BUF_SIZE 100
#define MAX_CLNT 256
#define MAX_THREAD 4

struct my_fd_set {
	fd_set fs;
	int fd_cnt;
	int max_fd; 
} reads[MAX_THREAD];

pthread_mutex_t mutex;

int fd_isempty(struct my_fd_set *pfs)
{
	if(NULL==pfs) return -1;
	int i;
	unsigned int myset[sizeof(fd_set) / sizeof(int)];

	memcpy(myset, &pfs->fs, sizeof(fd_set));
	for (i = 0; i < sizeof(fd_set) / sizeof(int); i++)
		if (myset[i])
			return 0;
	return 1; 
}

void *handle_clnt(void *arg)
{
	int str_len=0, i;
	char buf[BUF_SIZE];
	struct my_fd_set cpy_read;
	struct timeval timeout;
	int fd_num;

	while(1)
	{
		memcpy(&cpy_read, ((struct my_fd_set*)arg), sizeof(struct my_fd_set));
		timeout.tv_sec=5;
		timeout.tv_usec=5000;

		if(-1==(fd_num=select(cpy_read.max_fd+1, &cpy_read.fs, NULL, NULL, &timeout)))
		{
			break;//error
		}
		if(0==fd_num) continue;//time out

		for(i=0;i<cpy_read.max_fd+1;++i)//something happen
		{
			if(FD_ISSET(i, &cpy_read.fs))	//connect
			{
				str_len=read(i, buf, BUF_SIZE);
				if(str_len==0)
				{
					pthread_mutex_lock(&mutex);
					FD_CLR(i, &(((struct my_fd_set*)arg)->fs));
					close(i);
					printf("closed client: %d\n", i);
					--((struct my_fd_set*)arg)->fd_cnt;
					int j=((struct my_fd_set*)arg)->max_fd-1;
					if(0==((struct my_fd_set*)arg)->fd_cnt) continue;
					if(i==((struct my_fd_set*)arg)->max_fd)					
						while(j>-2) 
						{
							if(FD_ISSET(j, &cpy_read.fs)) ((struct my_fd_set*)arg)->max_fd=j;
							--j;
							if(-1==j) return NULL;
						}
					pthread_mutex_unlock(&mutex);
				}
				else
				{
					send_msg(buf, str_len);
				}
				
			}
			
		}
	}

	return NULL;
}

void send_msg(char *msg, int len)
{
	int i;
	pthread_mutex_lock(&mutex);
	for(i=0;i<MAX_THREAD;++i) 
	{
		int j=0;
		for(;j<reads[i].max_fd+1;++j)
			if(FD_ISSET(j, &reads[i].fs)) 
				write(j, msg, len);
	}
	pthread_mutex_unlock(&mutex);
}

void error_handling(char *msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr;
	int clnt_adr_sz;
	pthread_t t_id;

	if(2!=argc)
	{
		printf("Usage: %s <port>\n", argv[0]);
		exit(1);
	}

	pthread_mutex_init(&mutex, NULL);
	serv_sock=socket(PF_INET, SOCK_STREAM, 0);

	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family=AF_INET;
	serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
	serv_adr.sin_port=htons(atoi(argv[1]));

	if(-1==bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)))
		error_handling("bind() error");
	if(-1==listen(serv_sock, 5)) error_handling("listen() error");

	int i;
	for(i=0;i<MAX_THREAD;++i)
	{
		FD_ZERO(&reads[i].fs);
		reads[i].fd_cnt=0;
		reads[i].max_fd=-1;
	}

	while(1)
	{
		clnt_adr_sz=sizeof(clnt_adr);
		clnt_sock=accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
		//printf("accept:clnt_sock:%d\n",clnt_sock);
		pthread_mutex_lock(&mutex);
		for(i=0;i<MAX_THREAD;++i)
		{
			if(-1==reads[i].max_fd && 0==reads[i].fd_cnt) 
			{
				//printf("main while thread\n");
				FD_SET(clnt_sock, &reads[i].fs);
				++reads[i].fd_cnt;
				if(clnt_sock>reads[i].max_fd) reads[i].max_fd=clnt_sock;
				printf("Connected client IP: %s\n", inet_ntoa(clnt_adr.sin_addr));
				pthread_create(&t_id, NULL, handle_clnt, (void*)&reads[i]);
				pthread_detach(t_id);
				break;
			}
			if(reads[i].fd_cnt<MAX_CLNT) 
			{
				//printf("main while\n");
				FD_SET(clnt_sock, &reads[i].fs);
				++reads[i].fd_cnt;
				if(clnt_sock>reads[i].max_fd) reads[i].max_fd=clnt_sock;
				printf("Connected client IP: %s\n", inet_ntoa(clnt_adr.sin_addr));
				break;
			}
		}
		pthread_mutex_unlock(&mutex);

		
	}

	close(serv_sock);

	return 0;
}
