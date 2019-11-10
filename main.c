#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "monitor_neighbors.h"

void listenForNeighbors();
void* announceToNeighbors(void* unusedParam);
void readCostsFile(char *const *argv);

int globalMyID = 0;
struct timeval globalLastHeartbeat[MAX_NEIGHBOR];
int globalSocketUDP;
struct sockaddr_in globalNodeAddrs[MAX_NEIGHBOR];
char costs[MAX_NEIGHBOR];
paths pathsIKnow[MAX_NEIGHBOR];
bool debug, newPathDebug, NNWPATHdebug, debugDupPath, debugAddPath, debugEstablishNeigh, debugDisconnect, debugSendReceiveCount, debugReceiveProcessedCount;
int receivedFromCount[MAX_NEIGHBOR];
int sentToCount[MAX_NEIGHBOR];
int receivedAndProcessedFromCount[MAX_NEIGHBOR];
int processedNeighbor[MAX_NEIGHBOR];
FILE * myLogfile;

int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
		exit(1);
	}

	//initialization: get this process's node ID, record what time it is, 
	//and set up our sockaddr_in's for sending to the other nodes.
	globalMyID = atoi(argv[1]);


	debug = true;
    newPathDebug = false;
    NNWPATHdebug = false;
    debugDupPath = false;
    debugAddPath = false;
    debugEstablishNeigh = false;
    debugSendReceiveCount = false;
    debugReceiveProcessedCount = false;
    debugDisconnect = true;

	int i;
	for(i=0;i<MAX_NEIGHBOR;i++)
	{
		gettimeofday(&globalLastHeartbeat[i], 0);
		
		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}
	
	//TODO: read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
    readCostsFile(argv);

    //socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
	if((globalSocketUDP=socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	char myAddr[100];
	struct sockaddr_in bindAddr;
	sprintf(myAddr, "10.1.1.%d", globalMyID);	
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(7777);
	inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
	if(bind(globalSocketUDP, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(globalSocketUDP);
		exit(1);
	}

	char* logFileName = argv[3];
    myLogfile = fopen(logFileName, "w+");
    if(myLogfile == NULL)
    {
        fprintf(stdout,"Unable to Create Log File! --> %s \n", logFileName);
        exit(EXIT_FAILURE);
    }else{
        fprintf(stdout,"Created Log File! --> %s \n", logFileName);
    }

	pthread_t announcerThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void*)0);

    pthread_t updateThread;
    pthread_create(&updateThread, 0, updateToNeighbors, (void*)0);

//    pthread_t disconnectThread;
//    pthread_create(&disconnectThread, 0, processDisconnects, (void*)0);

	//good luck, have fun!
	listenForNeighbors();

}

/**
 * Because the system sends us the initial costs of nodes which we can use
 * to associate to our neighbors
 * @param argv
 */
void readCostsFile(char *const *argv) {
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    const size_t path_size = strlen(argv[2]) + 1;
    char* path = malloc(path_size);
    strcat( path, argv[2] );

    fp = fopen(path, "r");
//    if (fp == NULL)
//        exit(EXIT_FAILURE);

    while ((read = getline(&line, &len, fp)) != -1) {
        int id,cost;
        sscanf(line, "%d %d\n", &id,&cost);
        costs[id] = cost;
    }

//    fprintf(stdout,"%d",costs[5][1]);
//    fprintf(stdout,"%d",costs[2][1]);

    fclose(fp);
    if (line)
        free(line);
}
