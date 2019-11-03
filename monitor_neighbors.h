#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>

#define MAX_NEIGHBOR  35
#define MAX_NUM_PATHS 2000
#define NOT_HEARD_FROM_SINCE 2

pthread_mutex_t updateLock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int cost; //cost to get there
    int costBeforeAddingMine;
    int hasUpdates; //To trigger an update if not alreadyknown
    int nextHop; //In order to know where to go next for this destination
    int pathSize;
    int path[MAX_NEIGHBOR]; //The [0]th element is the id of the first node in the path --Used to find loops
} path;

typedef struct { //So we can store many paths to a destination that we've heard
    int size;
    int hasUpdates;
    int alreadyProcessedNeighbor;
    int isMyNeighbor;
    int needsMyPaths;
    path pathsIKnow[MAX_NUM_PATHS];
} paths;

paths pathsIKnow[MAX_NEIGHBOR] =
{
  [0 ... MAX_NEIGHBOR - 1] =
   {
    -1,0,0,0,0, //size,hasUpdates,alreadyProcessedNeighbor,isMyNeighbor,needsMyPaths
    {
     [0 ... MAX_NUM_PATHS - 1] =
        {
           999,0,0,0,0,
           {
               [0 ... MAX_NEIGHBOR - 1] = 999
           }
        }
    }
   }
};

extern struct timeval globalLastHeartbeat[MAX_NEIGHBOR];
extern struct sockaddr_in globalNodeAddrs[MAX_NEIGHBOR];
extern char costs[MAX_NEIGHBOR];

extern bool debug,newPathDebug, NNWPATHdebug, debugDupPath, debugAddPath, debugEstablishNeigh, debugDisconnect, debugSendReceiveCount,debugReceiveProcessedCount;
extern int receivedFromCount[MAX_NEIGHBOR];
extern int sentToCount[MAX_NEIGHBOR];
extern int receivedAndProcessedFromCount[MAX_NEIGHBOR];
static const int SLEEPTIME = 300 * 1000 * 1000; //300 ms
static const int SLEEPTIME_DISCONNECT = 300 * 1000 * 1000 * 3; //900 ms
extern int globalMyID;
struct timeval getCurrentTime();
extern int globalSocketUDP;
extern FILE * myLogfile;

void doWithMessage(const char *fromAddr, const unsigned char *recvBuf, int i);


/**
 * Convience method to get current time
 * @return
 */
struct timeval getCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv;
}

struct timespec getHowLongToSleep();

int getNeigborCost(short heardFrom);

struct timespec getHowLongToSleep() {
    struct timespec sleepFor;
    sleepFor.tv_sec = 0;
    sleepFor.tv_nsec = SLEEPTIME;
    return sleepFor;
}

struct timespec getHowLongToSleepForDisconnect() {
    struct timespec sleepFor;
    sleepFor.tv_sec = 0;
    sleepFor.tv_nsec = SLEEPTIME_DISCONNECT;
    return sleepFor;
}

void establishNeighbor(short heardFrom);

void updateLastHeardTime(short from);

char *convertPath(path dPath);

char *convertPathBackwardsWithoutSelf(path dPath);

char* concat(int count, ...);

void hackyUpdateKnownPaths();

void processNewPath(const unsigned char *recvBuf,short heardFrom, bool flood);

bool amIInPath(const char *path);

void addNewPath(short heardFrom, int destination, int cost, const char *path,bool flood);

void extractNewPathData(const unsigned char *recvBuf, char **tofree, int *destination, int *cost, char **path);

bool notHeardFromSince(short from, short seconds);

int getNextHop(short destId);

void resetPath(short disconnectId, int i);

bool isNewPath(short heardFrom, int destination, int cost, const char *strPath, int currentKnownSize);

//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char *buf, int length) {
    int i;
    for (i = 0; i < MAX_NEIGHBOR; i++)
        if (i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
            sendto(globalSocketUDP, buf, length, 0,
                   (struct sockaddr *) &globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

//To keep track of disconnects
void *announceToNeighbors(void *unusedParam) {
    struct timespec sleepFor = getHowLongToSleep();
    while (1) {
        hackyBroadcast("KEEPALIVE", 9);
        nanosleep(&sleepFor, 0);
    }
}

//To send updates for new data
void *updateToNeighbors(void *unusedParam) {
    struct timespec sleepFor = getHowLongToSleep();
    while (1) {
        hackyUpdateKnownPaths();
        nanosleep(&sleepFor, 0);
    }
}

//Any disconnects communicate it to neighbors
void *processDisconnects(void *unusedParam) {
    struct timespec sleepFor = getHowLongToSleepForDisconnect();
    while (1) {
        for (int i= 0; i < MAX_NEIGHBOR; i++){
            if(notHeardFromSince(i,NOT_HEARD_FROM_SINCE) && i != globalMyID && pathsIKnow[i].isMyNeighbor == 1){
                short int no_destID = htons((short)i);
                int msgLen = 4+sizeof(short int);
                char* sendBuf = malloc(msgLen);
                strcpy(sendBuf, "dscn");
                memcpy(sendBuf+4, &no_destID, sizeof(short int));

                for (int j= 0; j < MAX_NEIGHBOR; j++) {

                    if( j != i && j != globalMyID){
                        if(debug && debugDisconnect)
                            fprintf(stdout, "[%d] Neighbor Disconnect [%d] Telling my Neighbors \n",globalMyID,i);
                        sendto(globalSocketUDP, sendBuf, msgLen, 0, (struct sockaddr*)&globalNodeAddrs[j], sizeof(globalNodeAddrs[j]));
                    }
                }
                free(sendBuf);
                pathsIKnow[i].isMyNeighbor = 0;
                pathsIKnow[i].alreadyProcessedNeighbor = 0;
                int numRemoved = 0;
                for (int k = 0 ; k<= pathsIKnow[i].size;k++) {
                    if (pathsIKnow[i].pathsIKnow[k].nextHop == i) {
                        resetPath(i, k);
                    }
                    numRemoved++;
                }
                pathsIKnow[i].size = pathsIKnow[i].size - numRemoved;
            }
        }
        nanosleep(&sleepFor, 0);
    }
}

//To send updates for new data
void *shareMyPathsToNeighbors(void *unusedParam) {
    struct timespec sleepFor = getHowLongToSleep();
    while (1) {
        for (int i= 0; i < MAX_NEIGHBOR; i++){
            if (i != globalMyID) {
                if(pathsIKnow[i].needsMyPaths==1){ //Because new neighbors need to know ALL my paths I know
                    for (int k = 0; k < MAX_NEIGHBOR; k++){ //go through all my destinations
                        if (k != globalMyID && k != i) { //Don't send them my path or their own path
                            if (pathsIKnow[k].size != -1) { //If I actually have paths to them
                                for (int p = 0; p <= pathsIKnow[k].size; p++) { //go through them and send
                                    if(pathsIKnow[k].pathsIKnow[p].nextHop != i){//Don't se it paths which we already know go to it
                                        path pathWithUpdate = pathsIKnow[k].pathsIKnow[p];
                                        char *pathToDestination = convertPath(pathWithUpdate);

                                        //|command|destinationId|data|
                                        char *destination[4]; //Because 256 is greatest value we get (3 size)
                                        sprintf(destination, "%d", k);

                                        char *nextHop[4]; //Because the next hop will always be me from my neighbor
                                        sprintf(nextHop, "%d", globalMyID);

                                        char *costOfPath[6]; //5?  hopefully enough to hold max cost
                                        sprintf(costOfPath, "%d", pathWithUpdate.cost);

                                        char *updateMessageToSend = concat(9, "NNWPATH", "|", destination, "|",
                                                                           pathToDestination, "|", costOfPath, "|",
                                                                           nextHop);
                                        if (debug && NNWPATHdebug) {
                                            fprintf(stdout,
                                                    "[%d] Sending New Neighbor [%d] All my Paths --> One is NEWPATH To [Id:%d][Path:%s][nextHop:%s][Cost:%s]\n",
                                                    globalMyID,i, k, pathToDestination,nextHop,costOfPath);
                                        }


                                        sendto(globalSocketUDP, updateMessageToSend, strlen(updateMessageToSend), 0,
                                               (struct sockaddr *) &globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));

                                        if(debugSendReceiveCount){
                                            sentToCount[i]++;
                                        }

                                        free(updateMessageToSend);
                                    }
                                }
                            }
                        }
                    }
                    pathsIKnow[i].needsMyPaths=0;
                }
            }
        }
        nanosleep(&sleepFor, 0);
    }
}

//To send updates for new data
void *dumpMyPathsToConsole() {
    for (int k = 0; k < MAX_NEIGHBOR; k++){ //go through all my destinations
        if (k != globalMyID) { //Don't send them my path or their own path
            if (pathsIKnow[k].size != -1) { //If I actually have paths to them
                for (int p = 0; p <= pathsIKnow[k].size; p++) { //go through them and send
                    path pathWithUpdate = pathsIKnow[k].pathsIKnow[p];
                    char *pathToDestination = convertPath(pathWithUpdate);

                    //|command|destinationId|data|
                    char *destination[4]; //Because 256 is greatest value we get (3 size)
                    sprintf(destination, "%d", k);

                    char *nextHop[4]; //Because the next hop will always be me from my neighbor
                    sprintf(nextHop, "%d", pathWithUpdate.nextHop);

                    char *costOfPath[6]; //5?  hopefully enough to hold max cost
                    sprintf(costOfPath, "%d", pathWithUpdate.cost);

                    char *updateMessageToSend = concat(9, "KNOWNPATH", "[To:", destination, "][Path:",
                                                       pathToDestination, "][Cost:", costOfPath, "][nextHop:",
                                                       nextHop, "]");
                    fprintf(stdout,"%s\n",updateMessageToSend);

                    free(updateMessageToSend);
                }
            }
        }
    }

}

//To send updates for new data
void *dumpMyPathStatsToConsole() {
    for (int k = 0; k < MAX_NEIGHBOR; k++){ //go through all my destinations
        if (k != globalMyID) { //Don't send them my path or their own path
            if (pathsIKnow[k].size != -1) { //If I actually have paths to them

                char *destination[4]; //Because 256 is greatest value we get (3 size)
                sprintf(destination, "%d", k);

                char *numPaths[8]; //Because 256 is greatest value we get (3 size)
                sprintf(numPaths, "%d", pathsIKnow[k].size );

                char *updateMessageToSend = concat(5,"[",destination,"][",numPaths,"]");
                fprintf(stdout,"%s\n",updateMessageToSend);

                free(updateMessageToSend);
            }
        }
    }

}



char *convertPath(path dPath) {
    char *fullPath = "";
//    for (int pathStep = dPath.pathSize; pathStep >= 0; pathStep--) {
    for (int pathStep = 0; pathStep <= dPath.pathSize; pathStep++) {
        int nextStep = dPath.path[pathStep];
        if (nextStep != 999) {
            if (fullPath != "") { //So we don't add a - in the begin of path with no number yet
                fullPath = concat(2,fullPath, "-");
            }
            char *stepChar[3]; //Because 256 is greatest value we get (3 size)
            sprintf(stepChar, "%d", nextStep);

            fullPath = concat(2,fullPath, stepChar);
        }
    }

    return fullPath;
}

char *convertPathBackwardsWithoutSelf(path dPath) {
    char *fullPath = "";
    for (int pathStep = 0; pathStep <= dPath.pathSize; pathStep++) {
        int nextStep = dPath.path[pathStep];
        if (nextStep != 999 && nextStep != globalMyID) { //So we don't add myself to it
            if (fullPath != "") { //So we don't add a - in the begin of path with no number yet
                fullPath = concat(2,fullPath, "-");
            }
            char *stepChar[3]; //Because 256 is greatest value we get (3 size)
            sprintf(stepChar, "%d", nextStep);

            fullPath = concat(2,fullPath, stepChar);
        }
    }

    return fullPath;
}

/**
 * From : https://stackoverflow.com/questions/8465006/how-do-i-concatenate-two-strings-in-c/8465083
 * @param count
 * @param ...
 * @return
 */
char* concat(int count, ...)
{
    va_list ap;
    int i;

    // Find required length to store merged string
    int len = 1; // room for NULL
    va_start(ap, count);
    for(i=0 ; i<count ; i++)
        len += strlen(va_arg(ap, char*));
    va_end(ap);

    // Allocate memory to concat strings
    char *merged = calloc(sizeof(char),len);
    int null_pos = 0;

    // Actually concatenate strings
    va_start(ap, count);
    for(i=0 ; i<count ; i++)
    {
        char *s = va_arg(ap, char*);
        strcpy(merged+null_pos, s);
        null_pos += strlen(s);
    }
    va_end(ap);

    return merged;
}


//So they know when I change state
void *updateNeighbors(void *unusedParam) {
    struct timespec sleepFor = getHowLongToSleep();
    while (1) {
        hackyUpdateKnownPaths();
        nanosleep(&sleepFor, 0);
    }
}


void listenForNeighbors() {
    char fromAddr[100];
    struct sockaddr_in theirAddr;
    socklen_t theirAddrLen;
    unsigned char recvBuf[1000];

    int bytesRecvd;
    while (1) {
        memset(recvBuf, 0, 1000);
        theirAddrLen = sizeof(theirAddr);
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000, 0,
                                   (struct sockaddr *) &theirAddr, &theirAddrLen)) == -1) {
            perror("connectivity listener: recvfrom failed");
            exit(1);
        }

        inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);

        doWithMessage(fromAddr, recvBuf, bytesRecvd);

    }
    //(should never reach here)
    close(globalSocketUDP);
}

void doWithMessage(const char *fromAddr, const unsigned char *recvBuf, int bytesRecvd) {
    short int heardFrom = -1;
//    if (strstr(fromAddr, "10.1.1.")) {
        heardFrom = atoi(
                strchr(strchr(strchr(fromAddr, '.') + 1, '.') + 1, '.') + 1);

        if (!strncmp(recvBuf, "KEEPALIVE", 7)) {
            if (heardFrom != -1) {
                updateLastHeardTime(heardFrom);
                establishNeighbor(heardFrom);
            }
        }else if (!strncmp(recvBuf, "NEWPATH", 7)) {
            if(debugSendReceiveCount){
                receivedFromCount[heardFrom]++;
            }
            processNewPath(recvBuf, heardFrom,true);
        }else if (!strncmp(recvBuf, "NNWPATH", 7)) {
            if(debugSendReceiveCount){
                receivedFromCount[heardFrom]++;
            }
            processNewPath(recvBuf, heardFrom,true);
        }else if(!strncmp(recvBuf,"dump",4)){

            fprintf(stdout, "Received DUMP %d\n",globalMyID);

//            dumpMyPathsToConsole();
//            dumpMyPathStatsToConsole();
            if(debugSendReceiveCount){
                for(int m =0; m <MAX_NEIGHBOR;m++){
                    if(pathsIKnow[m].isMyNeighbor==1)
                        fprintf(stdout, "[Received:%d][%d]\n",receivedFromCount[m],m);
                }
                for(int m =0; m <MAX_NEIGHBOR;m++){
                    if(pathsIKnow[m].isMyNeighbor==1)
                        fprintf(stdout, "[Sent:%d][%d]\n",sentToCount[m],m);
                }

            }

            if(debugReceiveProcessedCount){
                for(int m =0; m <MAX_NEIGHBOR;m++){
                    if(pathsIKnow[m].isMyNeighbor==1)
                        fprintf(stdout, "[Processed:%d][%d]\n",receivedAndProcessedFromCount[m],m);
                }
            }

        }else if (!strncmp(recvBuf, "send", 4)  || !strncmp(recvBuf, "frwd", 4)) {
            short int destId = ntohs(*((short int *)(recvBuf+4)));

//            if(debug) {
//                fprintf(stdout, "[Message:%s][Destination:%d][Size:%d]\n", &recvBuf[6], destId, bytesRecvd);
//            }
            char logLine[1000];
            char fullMessage[1000];
            memcpy(fullMessage, recvBuf + 6, 100);

            if(destId != globalMyID){//forward the packet to least cost nextHop
                int nextHop = getNextHop(destId);

                if(nextHop != -1){


                    if(!strncmp(recvBuf, "send", 4) ){
                        sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", destId, nextHop, fullMessage);
                        strncpy((char*)recvBuf,"frwd", 4); //forwarding it along
                        sendto(globalSocketUDP, recvBuf, bytesRecvd, 0,
                               (struct sockaddr*)&globalNodeAddrs[nextHop], sizeof(globalNodeAddrs[nextHop]));
                    }else{
                        sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", destId, nextHop,fullMessage);
                        sendto(globalSocketUDP, recvBuf, bytesRecvd, 0,
                               (struct sockaddr*)&globalNodeAddrs[nextHop], sizeof(globalNodeAddrs[nextHop]));
                    }

                }else{
                    sprintf(logLine, "unreachable dest %d\n", destId);
                }

            }else{
                sprintf(logLine, "receive packet message %s\n", fullMessage);
            }
            fwrite(logLine, 1, strlen(logLine), myLogfile);
            fflush(myLogfile);
        }else if (!strncmp(recvBuf, "cost", 4)) {
        //TODO record the cost change (remember, the link might currently be down! in that case,
        //this is the new cost you should treat it as having once it comes back up.)
        // ...
        }else if(!strncmp(recvBuf,"dscn",4)){
            short int disconnectId = ntohs(*((short int *)(recvBuf+4)));
            if(pathsIKnow[disconnectId].size >= 0){
                int numRemoved = 0;
                for (int i = 0 ; i<= pathsIKnow[disconnectId].size;i++){
                    if(pathsIKnow[disconnectId].pathsIKnow[i].nextHop == heardFrom){
                        if(debug && debugDisconnect)
                            fprintf(stdout, "Removing my [%d] path to [%d] with nextHop %d\n",i,disconnectId, heardFrom);
                        resetPath(disconnectId, i); //record it
                        numRemoved++;
                        for(int j=0 ;j < MAX_NEIGHBOR;j++){ //send it along!
                            if(j != heardFrom && j != globalMyID){
                                sendto(globalSocketUDP, recvBuf, bytesRecvd, 0,
                                       (struct sockaddr *) &globalNodeAddrs[j], sizeof(globalNodeAddrs[j]));
                            }
                        }
                    }
                }
                pathsIKnow[disconnectId].size = pathsIKnow[disconnectId].size - numRemoved;
            }
        }

    //TODO now check for the various types of packets you use in your own protocol
//else if(!strncmp(recvBuf, "your other message types", ))
// ...
}

void resetPath(short disconnectId, int i) {
    path pathToupdate = pathsIKnow[disconnectId].pathsIKnow[i];
    pathToupdate.cost = 9999;
    pathToupdate.hasUpdates = 0;
    pathToupdate.cost = 999;
    pathToupdate.costBeforeAddingMine = 999;
    pathToupdate.nextHop = 999;
    pathToupdate.pathSize = 0;
    for(int j=0 ;j < MAX_NEIGHBOR;j++){
        int cPathValue = pathToupdate.path[j];
        cPathValue = 999;
        pathToupdate.path[j] = cPathValue;
    }
    pathsIKnow[disconnectId].pathsIKnow[i] = pathToupdate;
}

/**
 * Is it really this simple?&& k==33
 * @return
 */
int getNextHop(short destId) {
    int nextHop = -1;
    if(pathsIKnow[destId].size != -1){
        int leastCost = 99999;
        for(int i=0;i<=pathsIKnow[destId].size;i++){
            int currCost = pathsIKnow[destId].pathsIKnow[i].cost;
            char *pathToDestination = convertPath(pathsIKnow[destId].pathsIKnow[i]);
//            fprintf(stdout,"I'm %d Looking at [Path:%d][PathIs:%s][TotalPaths:%d][To:%d][Cost:%d][Leastcost:%d][nextHop:%d]\n",globalMyID,i,pathToDestination,
//                    pathsIKnow[destId].size,destId,currCost,leastCost,pathsIKnow[destId].pathsIKnow[i].nextHop);
            if(currCost<leastCost){//take it because its least cost
                leastCost = currCost;
                nextHop = pathsIKnow[destId].pathsIKnow[i].nextHop;
            }else if(currCost == leastCost){//tie breaker : take it only if nextHop is less than current nextHop
                if(pathsIKnow[destId].pathsIKnow[i].nextHop < nextHop){
                    leastCost = currCost;
                    nextHop = pathsIKnow[destId].pathsIKnow[i].nextHop;
                }
            }
        }
    }
    return nextHop;
}

void processNewPath(const unsigned char *recvBuf,short heardFrom,bool flood){

    char *tofree;
    int destination;
    int cost;
    char *path;

    extractNewPathData(recvBuf, &tofree, &destination, &cost, &path);

    if(amIInPath(path)==false){//To prevent loops
        if(destination != 999) {
            addNewPath(heardFrom, destination, cost, path, flood);
        }

    }

    free(tofree);

}


void extractNewPathData(const unsigned char *recvBuf, char **tofree, int *destination, int *cost, char **path) {
    char *token, *str;
    (*tofree) = str = strdup(recvBuf);
    int argCount = 0;
    int   nextHop;//NEWPATH|destination|path|cost|nextHop
    while ((token = strsep(&str, "|"))) {
        if(argCount>=1){ //Ignore the command - we already know it
            if(argCount==1){//destination
                (*destination) = atoi(token);
            }else if(argCount==2){//path
                (*path) = token;
            }else if(argCount==3){//cost
                (*cost) = atoi(token);
            }else if(argCount==4){//nextHope
                nextHop = atoi(token);
            }
        }
        argCount++;
    }
}

void hackyUpdateKnownPaths() {

    int i;
    for (i = 0; i < MAX_NEIGHBOR; i++)
        if (i != globalMyID) {
            char *updateMessageToSend;
            if (pathsIKnow[i].hasUpdates==1) { //If any paths for this destination have updates
                for(int q=0;q<=pathsIKnow[i].size;q++){
                    if(pathsIKnow[i].pathsIKnow[q].hasUpdates == 1){
                        path pathWithUpdate = pathsIKnow[i].pathsIKnow[q];
                        char *pathToDestination = convertPath(pathWithUpdate);

                        //|command|destinationId|data|
                        char *destination[3]; //Because 256 is greatest value we get (3 size)
                        sprintf(destination, "%d", i);

                        char *nextHop[3]; //Because the next hop will always be me from my neighbor
                        sprintf(nextHop, "%d", globalMyID);

                        char *costOfPath[5]; //5?  hopefully enough to hold max cost
                        sprintf(costOfPath,"%d",pathWithUpdate.cost);

                        if(i != 999){
//                            if(costOfPath == 0){
//                                fprintf(stdout, "Found cost of 0\n",costOfPath);
//                            }
                            updateMessageToSend = concat(9,"NEWPATH","|",destination,"|",pathToDestination,"|",costOfPath,"|",nextHop);

                            for (int possibleNeighbor = 0; possibleNeighbor < MAX_NEIGHBOR; possibleNeighbor++) {//Tell all about my new fancy path

                                if(pathsIKnow[possibleNeighbor].isMyNeighbor == 1 && pathsIKnow[i].pathsIKnow[q].nextHop != possibleNeighbor){
                                    sendto(globalSocketUDP, updateMessageToSend, strlen(updateMessageToSend), 0,
                                           (struct sockaddr *) &globalNodeAddrs[possibleNeighbor], sizeof(globalNodeAddrs[possibleNeighbor]));

                                    if (debug && newPathDebug) {
                                        fprintf(stdout, "[%d] Update Neighbor [%d] With NEWPATH To [Id:%d][Path:%s]\n",globalMyID,possibleNeighbor, i, pathToDestination);
                                    }
                                    if(debugSendReceiveCount){
                                        sentToCount[possibleNeighbor]++;
                                    }
                                }

                            }
                            free(updateMessageToSend);
                            pathsIKnow[i].pathsIKnow[q].hasUpdates = 0;
                        }
                    }
                }
                pathsIKnow[i].hasUpdates=0; //so we don't send updates again
            }

        }

}

void addNewPath(short heardFrom, int destination, int cost, const char *path, bool flood) {
    if(debug && debugDupPath)
        fprintf(stdout, "\nNEW [heardFrom:%d][destination:%d][cost:%d][path:%s]\n",heardFrom,destination,cost,path);

    char *tokenPath, *strPath, *tofreePath;
    tofreePath = strPath = strdup(path);

    int currentKnownSize = pathsIKnow[destination].size;

    bool isNew = isNewPath(heardFrom, destination, cost, strPath, currentKnownSize);

    if(isNew){

        currentKnownSize++;

        if(debugAddPath)
            fprintf(stdout, "\nNEW [%d] [heardFrom:%d][destination:%d][cost:%d][path:%s]\n",globalMyID,heardFrom,destination,cost,path);

        if(currentKnownSize >= MAX_NUM_PATHS)
            fprintf(stdout,"!!!! [Size:%d]\n",currentKnownSize);

        int i = 0;
        pathsIKnow[destination].pathsIKnow[currentKnownSize].path[i] = globalMyID; //make me first in path
        while ((tokenPath = strsep(&strPath, "-"))) {
            i++;
            int pathStep = atoi(tokenPath);
            pathsIKnow[destination].pathsIKnow[currentKnownSize].path[i] = pathStep;
        }

        if(flood==true)
            pathsIKnow[destination].pathsIKnow[currentKnownSize].hasUpdates = 1;
        else
            pathsIKnow[destination].pathsIKnow[currentKnownSize].hasUpdates = 0;

        pathsIKnow[destination].pathsIKnow[currentKnownSize].nextHop = heardFrom;
        pathsIKnow[destination].pathsIKnow[currentKnownSize].costBeforeAddingMine = cost;
        pathsIKnow[destination].pathsIKnow[currentKnownSize].cost = cost + getNeigborCost(heardFrom);
//        fprintf(stdout, "Neihbor Cost [%d] Incoming Cost [%d][Result Cost:%d]\n",getNeigborCost(heardFrom), cost, pathsIKnow[destination].pathsIKnow[currentKnownSize].cost);
        pathsIKnow[destination].pathsIKnow[currentKnownSize].pathSize = i;

        if(flood==true)
            pathsIKnow[destination].hasUpdates = 1;
        else
            pathsIKnow[destination].hasUpdates = 0;

//        fprintf(stdout,"Increment Size Before [Size:%d]---",pathsIKnow[destination].size);
        pathsIKnow[destination].size = currentKnownSize;
//        fprintf(stdout,"After [Size:%d]\n",pathsIKnow[destination].size);

        free(tofreePath);

        if(debugReceiveProcessedCount){
            receivedAndProcessedFromCount[heardFrom]++;
        }

    }


}

bool isNewPath(short heardFrom, int destination, int cost, const char *strPath, int currentKnownSize) {
    bool shouldProcess = true;
    if (currentKnownSize >= 0) {
        for (int q = 0; q <= currentKnownSize; q++) {
            if (debug && debugDupPath)
                fprintf(stdout, "EXIST [heardFrom:%d][destination:%d][cost:%d][path:%s] --> ",
                        pathsIKnow[destination].pathsIKnow[q].nextHop,
                        destination,
                        pathsIKnow[destination].pathsIKnow[q].costBeforeAddingMine,
                        convertPathBackwardsWithoutSelf(pathsIKnow[destination].pathsIKnow[q]));

            //Because the stored path would have a cost attribute with my costs, so using the one before
            if (pathsIKnow[destination].pathsIKnow[q].costBeforeAddingMine == cost &&
                pathsIKnow[destination].pathsIKnow[q].nextHop == heardFrom) {

                //Because the path coming is backwards to what we send on NEWPATH
                if (strcmp(strPath, convertPathBackwardsWithoutSelf(pathsIKnow[destination].pathsIKnow[q])) == 0) {
                    shouldProcess = false;
                    if (debug & debugDupPath)
                        fprintf(stdout, "[%s] \n", "same");
                } else {
                    if (debug && debugDupPath)
                        fprintf(stdout, "[%s] \n", "notsame");
                }
            } else {
                if (debug && debugDupPath)
                    fprintf(stdout, "[%s] \n", "notsame");
            }
        }
    }
    return shouldProcess;
}

/**
 * Because a neighbor announcing itself means 1 known path with current cost value
 * @param heardFrom
 */
void establishNeighbor(short heardFrom) {
    if(pathsIKnow[heardFrom].alreadyProcessedNeighbor == 0){
        pathsIKnow[heardFrom].size++;

        pathsIKnow[heardFrom].pathsIKnow[pathsIKnow[heardFrom].size].cost = getNeigborCost(heardFrom);
        pathsIKnow[heardFrom].pathsIKnow[pathsIKnow[heardFrom].size].path[0] = globalMyID;
        pathsIKnow[heardFrom].pathsIKnow[pathsIKnow[heardFrom].size].path[1] = heardFrom;
        pathsIKnow[heardFrom].pathsIKnow[pathsIKnow[heardFrom].size].pathSize = 2;
        pathsIKnow[heardFrom].pathsIKnow[pathsIKnow[heardFrom].size].hasUpdates = 1;
        pathsIKnow[heardFrom].pathsIKnow[pathsIKnow[heardFrom].size].nextHop = heardFrom;
        pathsIKnow[heardFrom].hasUpdates = 1; //To trigger a flood to neighbors of new paths for this destination
        pathsIKnow[heardFrom].alreadyProcessedNeighbor = 800;
        pathsIKnow[heardFrom].isMyNeighbor = 1;
        pathsIKnow[heardFrom].needsMyPaths = 1;

        if (debug  && debugEstablishNeigh) {
           fprintf(stdout, "[%d] New Neighbor |Id:%d|Cost:%d|\n", globalMyID,heardFrom, pathsIKnow[heardFrom].pathsIKnow[pathsIKnow[heardFrom].size].cost);
        }
    }

}

bool amIInPath(const char *path) {
    char *tokenPath, *strPath, *tofreePath;
    tofreePath = strPath = strdup(path);
    bool amIInPath = false;
    while ((tokenPath = strsep(&strPath, "-"))) {
        int pathStep = atoi(tokenPath);
        if(pathStep==globalMyID){
            amIInPath=true;
            break;
        }
    }

    free(tofreePath);
    return amIInPath;
}


/**
 * Because we want to know if destinations can't be reached anymore
 * in seconds
 * @param from
 * @param seconds
 * @return
 */
bool notHeardFromSince(short from, short seconds) {
    struct timeval tv;
    time_t curtime;
    gettimeofday(&tv, 0);
    curtime = tv.tv_sec;

    time_t lastHeardTime;
    time_t delta = curtime - globalLastHeartbeat[from].tv_sec;

    if (debug) {
//        if(pathsIKnow[from].isMyNeighbor == 1)
//            fprintf(stdout, "Last Heard From |Id:%d|Seconds Ago:%d|\n", from, delta);
    }

    if (delta >= seconds  && pathsIKnow[from].isMyNeighbor == 1) {
        return true;
    }else{
        return false;
    }
}

/**
 * Because we want to know if destinations can't be reached anymore
 * in seconds
 * @param from
 * @param seconds
 * @return
 */
void updateLastHeardTime(short from) {
    struct timeval tv = getCurrentTime();
    globalLastHeartbeat[from] = tv;

//    if(debug)
//        fprintf(stdout, "Last Heard from %d at this time --> %d\n", from, globalLastHeartbeat[from]);
}



/**
 * So we can add it to the path
 * @param heardFrom
 * @return
 */
int  getNeigborCost(short heardFrom) {
    int cost = 1;
    if (costs[heardFrom] != NULL) {
        cost = costs[heardFrom];
    }

    return cost;
}


