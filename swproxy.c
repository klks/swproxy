#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/stat.h>

//Defines
#define FALSE                   0
#define TRUE                    1
#define BUF_SIZE 1024

#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))

//Logging stuff
pthread_mutex_t swproxy_log_mutex;
pthread_mutex_t swproxy_status_mutex;

//Structures
struct _MATCH_FILE{
	char *	pMatchString;
	char *	pReplaceFile;
	void *	pContentReplaceBuffer;
	long 	pContentReplaceBufferLen;
	struct 	_MATCH_FILE *next;
};
typedef struct _MATCH_FILE *PMATCH_FILE;

struct _MASTER_TINFO{
	char uplink_addr[50];
	int  uplink_port;
	char listen_addr[50];
	int  listen_port;
};
typedef struct _MASTER_TINFO *PMASTER_TINFO;

struct _SLAVE_TINFO{
	int client_socket;
	PMASTER_TINFO pmi;
};
typedef struct _SLAVE_TINFO *PSLAVE_TINFO;

//Function declaration
void swproxy_print(char *fmt, ...);
void hexdump_data(char *cData, int iDataSize);

//Global Variables
int time_to_quit;
PMATCH_FILE pMatchFile_head = NULL;
int iMThreadCount=0, iSThreadCount=0;

void renderErrorPage(char* buf, char* errMsg){
}

int fileExists(char* cFileName){
	int i = access(cFileName, R_OK);
	if(i == 0)	//File exists
		return 1;
	return 0;
}

void loadMatchingText(){
	const char seperators[] = ",";
	char cTemp[1024];
	char* pMatchString;
	char* pReplaceFile;
	FILE *fp = fopen("match.txt", "rb");
	if(fp == NULL){
		swproxy_print("Unable to open match.txt for reading\n");
		return;
	}

	while(!feof(fp)){
		bzero(cTemp, sizeof(cTemp));
		if(fgets(cTemp, sizeof(cTemp)-1, fp)){
			if(strlen(cTemp) <2) continue;
			if(cTemp[0] == '#') continue;
			if(cTemp[strlen(cTemp)-1] == '\n') cTemp[strlen(cTemp)-1] = 0;	//Remove linefeed
			pMatchString = pReplaceFile = NULL;
			
			char *token = strtok(cTemp, seperators);
			if(token == NULL || strlen(token) == 0) continue;
			pMatchString = (char*) malloc(strlen(token)+10);
			if(pMatchString == NULL){
				swproxy_print("Unable to allocate memory for pMatchString");
				continue;
			}
			bzero(pMatchString, strlen(token)+10);
			strncpy(pMatchString, token, strlen(token));
			
			token = strtok(NULL, seperators);
			if(token == NULL || strlen(token) == 0) continue;
			pReplaceFile = malloc(strlen(token)+10);
			if(pReplaceFile == NULL){
				swproxy_print("Unable to allocate memory for pReplaceFile");
				free(pMatchString);
				continue;
			}
			bzero(pReplaceFile, strlen(token)+10);
			strncpy(pReplaceFile, token, strlen(token));
			
			if(!fileExists(pReplaceFile)){
				swproxy_print("Unable to locate %s", pReplaceFile);
				free(pMatchString);
				free(pReplaceFile);
				continue;
			}

			FILE *fp = fopen(pReplaceFile, "rb");
			if( fp == NULL){
				swproxy_print("Unable to read contents of file %s", pReplaceFile);
				free(pMatchString);
				free(pReplaceFile);
				continue;
			}

			fseek(fp, 0, SEEK_END);
			long size = ftell(fp);
			if(size == 0){
				swproxy_print("Unable to get file size of %s", pReplaceFile);
				free(pMatchString);
				free(pReplaceFile);
				fclose(fp);
                continue;
            }
			rewind(fp);

			char *pContentReplaceBuffer = malloc(size+10);
			if(pContentReplaceBuffer == NULL){
				swproxy_print("Unable to allocate memory for pContentReplaceBuffer");
				free(pMatchString);
				free(pReplaceFile);
				fclose(fp);
				continue;
			}

			fread(pContentReplaceBuffer, 1, size, fp);
			fclose(fp);

			PMATCH_FILE pmf_tmp = (PMATCH_FILE)malloc(sizeof(struct _MATCH_FILE));
			pmf_tmp->pMatchString = pMatchString;
			pmf_tmp->pReplaceFile = pReplaceFile;
			pmf_tmp->pContentReplaceBufferLen = size;
			pmf_tmp->pContentReplaceBuffer = pContentReplaceBuffer;

			//Update linked list
			pmf_tmp->next = pMatchFile_head;
			pMatchFile_head = pmf_tmp;

			//swproxy_print("%s", pMatchFile);
			//swproxy_print("%s", pReplaceFile);
		}
	}	
	fclose(fp);

	return;
}

PMATCH_FILE findMatch(char* cPath){
	PMATCH_FILE pmf_tmp = pMatchFile_head;

	while(pmf_tmp){
		char *pch = strstr(cPath, pmf_tmp->pMatchString);
		if(pch != NULL){	//We have a match
			return pmf_tmp;
		}
		pmf_tmp = pmf_tmp->next;
	}
	return NULL;
}

void hexdump_data(char *cData, int iDataSize){
	int i; // index in data...
	int j; // index in line...
	char temp[8];
	char buffer[128];
	char *ascii;

	memset(buffer, 0, 128);

	printf("        +0          +4          +8          +c            0   4   8   c   \n");

	ascii = buffer + 58;
	memset(buffer, ' ', 58 + 16);
	buffer[58 + 16] = '\n';
	buffer[58 + 17] = '\0';
	buffer[0] = '+';
	buffer[1] = '0';
	buffer[2] = '0';
	buffer[3] = '0';
	buffer[4] = '0';

	for (i = 0, j = 0; i < iDataSize; i++, j++)
	{
		if (j == 16)
		{
			printf("%s", buffer);
			memset(buffer, ' ', 58 + 16);

			sprintf(temp, "+%04x", i);
			memcpy(buffer, temp, 5);

			j = 0;
		}

		sprintf(temp, "%02x", 0xff & cData[i]);
		memcpy(buffer + 8 + (j * 3), temp, 2);
		if ((cData[i] > 31) && (cData[i] < 127))
			ascii[j] = cData[i];
		else
			ascii[j] = '.';
	}

	if (j != 0)
		printf("%s", buffer);
}

ssize_t update_host_header(unsigned char* pMem, ssize_t recv_len){
	//Find Host : 127.0.0.1 in pMem
	return recv_len;
}

void increase_master_thread_count(){
	pthread_mutex_lock(&swproxy_status_mutex);
	iMThreadCount++;
    pthread_mutex_unlock(&swproxy_status_mutex);
}

void increase_slave_thread_count(){
	pthread_mutex_lock(&swproxy_status_mutex);
	iSThreadCount++;
    pthread_mutex_unlock(&swproxy_status_mutex);
}

void decrease_slave_thread_count(){
	pthread_mutex_lock(&swproxy_status_mutex);
	iSThreadCount--;
	pthread_mutex_unlock(&swproxy_status_mutex);
}

void* slave_thread(void* ptr){
	PSLAVE_TINFO psi = ptr;
	PMASTER_TINFO pmi = psi->pmi;
	int uplink_socket = -1;
	long bytes_sent=0, bytes_uplink=0, bytes_intercept=0;

	time_t t_start, t_end;
    time(&t_start);

	increase_slave_thread_count();

	int client_socket = psi->client_socket;
	free(ptr);

	struct sockaddr_in serv_addr;

	pthread_t tid;
	tid = pthread_self();

	//Allocate 1MB buffer
	unsigned char* pMem = malloc(100000);
	if (pMem == NULL){
		swproxy_print("(%d)slave_thread malloc() failed!", tid);
		decrease_slave_thread_count();
		return -1;
	}
	unsigned char* pMem2 = malloc(100000);
	if (pMem2 == NULL){
		swproxy_print("slave_thread malloc() failed!");
		free(pMem);
		decrease_slave_thread_count();
		return -1;
	}

	struct timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof tv)){
		swproxy_print("slave_thread client_socket setsockopt(SO_RCVTIMEO) failed!");
	}

	while(1){
		//Read socket
		bzero(pMem, 100000);
		ssize_t recv_len = recv(client_socket, pMem, 100000, 0);
		bytes_sent += recv_len;

		//swproxy_print("slave_thread read %d bytes", recv_len);
		if(recv_len <= 0){ //Nothing? close uplink socket and break from loop
			//swproxy_print("slave_thread recv() no data, shutting down!");
			break;
		}
		//hexdump_data(pMem, recv_len);
		//recv_len = update_host_header(pMem, recv_len); //TODO
		//hexdump_data(pMem, recv_len);

		//Check if this is something we should intercept
		PMATCH_FILE pMatchPtr = findMatch((char*)pMem);
		if(pMatchPtr == NULL){	//Forward request
			if(uplink_socket == -1){
				if((uplink_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
					swproxy_print("slave_thread uplink socket() failed!");
					//Render an error to the screen //TODO
					close(client_socket);
					free(pMem);
					free(pMem2);
					decrease_slave_thread_count();
				        return -1;
				}
				memset(&serv_addr, '0', sizeof(serv_addr)); 
				serv_addr.sin_family = AF_INET;
				serv_addr.sin_port = htons(pmi->uplink_port);

				if(inet_pton(AF_INET, pmi->uplink_addr, &serv_addr.sin_addr)<=0){
					swproxy_print("slave_thread inet_pton() failed!");
					//Render an error to the screen //TODO
					close(client_socket);
					free(pMem);
					free(pMem2);
					decrease_slave_thread_count();
					return -1;
				}

				//swproxy_print("slave_thread connecting to uplink");
				if( connect(uplink_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
					swproxy_print("slave_thread connect() failed");
					//Render an error to the screen //TODO
					close(client_socket);
					free(pMem);
					free(pMem2);
					decrease_slave_thread_count();
					return -1;
				} 
				//swproxy_print("slave_thread forwarding request");

			        tv.tv_sec = 1;
			        tv.tv_usec = 0;
			        if (setsockopt(uplink_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof tv)){
               			swproxy_print("slave_thread uplink_socket setsockopt(SO_RCVTIMEO) failed!");
			        }
			}
			send(uplink_socket, pMem, recv_len, 0);
			
			while(1){
				ssize_t uplink_recv_len = recv(uplink_socket, pMem2, 100000, 0);
				bytes_uplink += uplink_recv_len;
				if(uplink_recv_len > 0){
					//swproxy_print("uplink sent back %d bytes", uplink_recv_len);
					//hexdump_data(pMem2, uplink_recv_len);
					send(client_socket, pMem2, uplink_recv_len ,0);
				}
				else{
					break;
				}
			}
			//close(uplink_socket);
		}
		else{	//Intercept Request
			swproxy_print("slave_thread intercepting request %s", pMatchPtr->pMatchString);
			long total_sent = 0;
			char *cp = pMatchPtr->pContentReplaceBuffer;
			long left_to_send = pMatchPtr->pContentReplaceBufferLen;
			bytes_intercept += left_to_send;
			while(total_sent != pMatchPtr->pContentReplaceBufferLen){
				ssize_t bytes_sent = send(client_socket, cp, left_to_send, 0);
				if(bytes_sent <= 0)
					break;
				total_sent += bytes_sent;
				left_to_send -= bytes_sent;
				cp += bytes_sent;
			}
			swproxy_print("slave_thread intercepting complete!");
		}
	}

	if(uplink_socket != -1) close(uplink_socket);
	close(client_socket);
	free(pMem);
	free(pMem2);
	time(&t_end);
	double time_diff = difftime(t_end, t_start);
	swproxy_print("slave_thread exiting! (%.2f-run %d-sent %d-uplink %d-intercept)", time_diff, bytes_sent, bytes_uplink, bytes_intercept);
	decrease_slave_thread_count();
	return 0;
}

void* master_thread(void* ptr){
	PMASTER_TINFO pmi = ptr;
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr; 
	struct sockaddr_in client_addr;
	int clilen = sizeof(client_addr);
	int rc, on = 1;

	increase_master_thread_count();

	//pthread_t tid;
    //tid = pthread_self();

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	rc = setsockopt(listenfd, SOL_SOCKET,  SO_REUSEADDR,(char *)&on, sizeof(on));
	if (rc < 0)
	{
		swproxy_print("master_thread(%d) setsockopt() failed!", pmi->listen_port);
		close(listenfd);
		return -1;
	}

	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	inet_aton(pmi->listen_addr,&(serv_addr.sin_addr));
	serv_addr.sin_port = htons(pmi->listen_port);

	bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 
	swproxy_print("master_thread(%d) listening for connections!", pmi->listen_port);
	listen(listenfd, 50); 

	while(1){
		connfd = accept(listenfd, (struct sockaddr*)&client_addr, (socklen_t *)&clilen);
		if(connfd >= 0){
			char *pClientAddr = inet_ntoa(client_addr.sin_addr);
			swproxy_print("master_thread(%d) incoming connection from %s", pmi->listen_port, pClientAddr);
			pthread_t tid;
			PSLAVE_TINFO psi_tmp;

		    psi_tmp = (PSLAVE_TINFO)malloc(sizeof(struct _SLAVE_TINFO));
			psi_tmp->pmi = pmi;
			psi_tmp->client_socket = connfd;
		    pthread_create(&tid, NULL, slave_thread,(void*)psi_tmp);
		}
		else{
			swproxy_print("master_thread(%d) accept failed!", pmi->listen_port);
		}
	}
	free(pmi);	//This will never reach :(
	return 0;
}

void swproxy_print(char *fmt, ...){
	va_list list;
	time_t rawtime;
	struct tm *timeinfo;

	pthread_mutex_lock(&swproxy_log_mutex);

	char *pMem = malloc(10000);
	if (pMem == NULL)
	{
		printf("swproxy_print() malloc() Failed\n");
		pthread_mutex_unlock(&swproxy_log_mutex);
		return;
	}

	//Build time
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(pMem, 100,"[%H:%M] *", timeinfo);

	//Format message
	va_start(list, fmt);
		vsprintf((char *)&pMem[strlen(pMem)], fmt, list);
		strcat(pMem, "\n");
		printf("%s", pMem);
	va_end(list);

	free(pMem);	
	pthread_mutex_unlock(&swproxy_log_mutex);
}

void signal_handler(int sig){
	switch(sig){
		case SIGHUP:
			swproxy_print("Received SIGHUP signal.");
			break;
		case SIGINT:
		case SIGTERM:
			swproxy_print("Received SIGTERM signal.");
			time_to_quit = TRUE;
			break;
		default:
			swproxy_print("Unhandled signal (%d) %s", sig, (char*)strsignal(sig));
			break;
	}
}

int main(){
	//Infinite wait flag
	time_to_quit = FALSE;

	//Init mutex for logging
	pthread_mutex_init(&swproxy_log_mutex, NULL);
	pthread_mutex_init(&swproxy_status_mutex, NULL);

	//Setup signal handling before we start
	signal(SIGHUP, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);

	//Load files we want to be intercepting and replacing
	loadMatchingText();

	//Start threads
	pthread_t tid;
	PMASTER_TINFO pmi_tmp;

	pmi_tmp = (PMASTER_TINFO)malloc(sizeof(struct _MASTER_TINFO));
        pmi_tmp->uplink_port = 33200;
        pmi_tmp->listen_port = 33200;
        strncpy(pmi_tmp->uplink_addr, "10.1.2.3", 50);
        strncpy(pmi_tmp->listen_addr, "127.0.0.1", 50);
        pthread_create(&tid, NULL, master_thread,(void*)pmi_tmp);

	pmi_tmp = (PMASTER_TINFO)malloc(sizeof(struct _MASTER_TINFO));
        pmi_tmp->uplink_port = 33300;
        pmi_tmp->listen_port = 33300;
        strncpy(pmi_tmp->uplink_addr, "10.2.3.4", 50);
        strncpy(pmi_tmp->listen_addr, "127.0.0.1", 50);
        pthread_create(&tid, NULL, master_thread,(void*)pmi_tmp);

	time_t last_checked, time_now;
	time(&last_checked);
	//Wait to exit
	while(time_to_quit != TRUE){
		time(&time_now);
		if(difftime(time_now, last_checked) > 15.0){
			pthread_mutex_lock(&swproxy_status_mutex);
		    swproxy_print("iMThreadCount=%d, iSThreadCount=%d", iMThreadCount, iSThreadCount);
		    pthread_mutex_unlock(&swproxy_status_mutex);
			time(&last_checked);
		}
		sleep(1);
	}

	//Destroy threads
	//Release memory
	while(pMatchFile_head){
		PMATCH_FILE curr = pMatchFile_head;
		pMatchFile_head = pMatchFile_head->next;
		free(curr->pMatchString);
		free(curr->pReplaceFile);
		free(curr->pContentReplaceBuffer);
		free(curr);
	}
	pthread_mutex_destroy(&swproxy_log_mutex);

    return 0;
}
