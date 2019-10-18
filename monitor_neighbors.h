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

enum MAX_NEIGHBOR {MAX_NEIGHBOR = 4};
enum MAX_NUM_PATHS {MAX_NUM_PATHS = 1000};

typedef struct {
    int idDestination; //id of node I know how to get to
    int cost; //cost to get there
    int path[MAX_NEIGHBOR]; //The [0]th element is the id of the first node in the path --Used to find loops
    int hasUpdates; //To trigger an update if not alreadyknown
    int nextHop; //In order to know where to go next for this destination
} path;

typedef struct { //So we can store many paths to a destination that we've heard
    int size;
    int hasUpdates;
    int alreadyProcessedNeighbor;
    path pathsIKnow[MAX_NUM_PATHS];
} paths;

paths pathsIKnow[MAX_NEIGHBOR];

extern struct timeval globalLastHeartbeat[MAX_NEIGHBOR];
extern struct sockaddr_in globalNodeAddrs[MAX_NEIGHBOR];
extern char costs[MAX_NEIGHBOR];

extern bool debug;
static const int SLEEPTIME = 300 * 1000 * 1000; //300 ms
extern int globalMyID;
struct timeval getCurrentTime();
extern int globalSocketUDP;

void doWithMessage(const char *fromAddr, const unsigned char *recvBuf);


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

void establishNeighbor(short heardFrom);

void updateLastHeardTime(short from);

char *convertPath(path dPath);

char* concat(int count, ...);

void hackyUpdateKnownPaths();

void processNewPath(const unsigned char *recvBuf,short heardFrom);

bool amIInPath(const char *path);

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

char *convertPath(path dPath) {
//    path dPath = pathsIKnow[destinationOfKnownPath];
    char *fullPath = "";
    int pathStep = 0;
    for (pathStep = 0; pathStep < MAX_NEIGHBOR; pathStep++) {
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
        theirAddrLen = sizeof(theirAddr);
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000, 0,
                                   (struct sockaddr *) &theirAddr, &theirAddrLen)) == -1) {
            perror("connectivity listener: recvfrom failed");
            exit(1);
        }

        inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);

        doWithMessage(fromAddr, recvBuf);

    }
    //(should never reach here)
    close(globalSocketUDP);
}

void hackyUpdateKnownPaths() {

    int i;
    for (i = 0; i < MAX_NEIGHBOR; i++)
        if (i != globalMyID) {
            char *updateMessageToSend;
            if (pathsIKnow[i].hasUpdates==1) { //If any paths for this destination have updates

                for(int q=0;q<=MAX_NUM_PATHS;q++){
                    if(pathsIKnow[i].pathsIKnow[q].hasUpdates == 1){
                        path pathWithUpdate = pathsIKnow[i].pathsIKnow[q];
                        char *pathToDestination = convertPath(pathWithUpdate);

                        //|command|destinationId|data|
                        char *destination[3]; //Because 256 is greatest value we get (3 size)
                        sprintf(destination, "%d", i);

                        char *nextHop[5]; //Because the next hop will always be me from my neighbor
                        sprintf(nextHop, "%d", globalMyID);

                        char *costOfPath[5]; //5?  hopefully enough to hold max cost
                        sprintf(costOfPath,"%d",pathWithUpdate.cost);

                        updateMessageToSend = concat(9,"NEWPATH","|",destination,"|",pathToDestination,"|",costOfPath,"|",nextHop);

                        for (int possibleNeighbor = 0; possibleNeighbor < MAX_NEIGHBOR; possibleNeighbor++) {//Tell all about my new fancy path
                            if (debug) {
                                fprintf(stdout, "Update Neighbor [%d] With NEWPATH To [Id:%d][Path:%s]\n",possibleNeighbor, i, pathToDestination);
                                //fprintf(stdout, "Update Neighbors With: |Message:[%s]\n", updateMessageToSend);
                            }
                            sendto(globalSocketUDP, updateMessageToSend, strlen(updateMessageToSend), 0,
                                   (struct sockaddr *) &globalNodeAddrs[possibleNeighbor], sizeof(globalNodeAddrs[possibleNeighbor]));

                        }
                        free(updateMessageToSend);
                        pathsIKnow[i].pathsIKnow[q].hasUpdates = 0;
                    }
                }
                pathsIKnow[i].hasUpdates=0; //so we don't send updates again
            }

        }

}

void doWithMessage(const char *fromAddr, const unsigned char *recvBuf) {
    short int heardFrom = -1;
    if (strstr(fromAddr, "10.1.1.")) {
        heardFrom = atoi(
                strchr(strchr(strchr(fromAddr, '.') + 1, '.') + 1, '.') + 1);

        if (!strncmp(recvBuf, "KEEPALIVE", 7)) {
            if (heardFrom != -1) {
                updateLastHeardTime(heardFrom);
                establishNeighbor(heardFrom);
            }
        }

        if (!strncmp(recvBuf, "NEWPATH", 7)) {
            processNewPath(recvBuf,heardFrom);

        }

        //TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.

        //record that we heard from heardFrom just now. ORIGINAL
//        gettimeofday(&globalLastHeartbeat[heardFrom], 0);
    }

    //Is it a packet from the manager? (see mp2 specification for more details)
    //send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
    if (!strncmp(recvBuf, "send", 4)) {

        //TODO send the requested message to the requested destination node
        // ...
    }
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
    else if (!strncmp(recvBuf, "cost", 4)) {
        //TODO record the cost change (remember, the link might currently be down! in that case,
        //this is the new cost you should treat it as having once it comes back up.)
        // ...
    }

    //TODO now check for the various types of packets you use in your own protocol
//else if(!strncmp(recvBuf, "your other message types", ))
// ...
}

void processNewPath(const unsigned char *recvBuf,short heardFrom) {
    if(debug)
        fprintf(stdout, "NEWPATH Message Received %s\n", recvBuf);

    char *token, *str, *tofree;
    tofree = str = strdup(recvBuf);
    int argCount = 0;
    int destination, cost, nextHop;
    char* path;

    //NEWPATH|destination|path|cost|nextHop
    while ((token = strsep(&str, "|"))) {
        if(argCount>=1){ //Ignore the command - we already know it
            if(argCount==1){//destination
                destination = atoi(token);
            }else if(argCount==2){//path
                path = token;
            }else if(argCount==3){//cost
                cost = atoi(token);
            }else if(argCount==4){//nextHope
                nextHop = atoi(token);
            }
        }
        argCount++;
    }

    if(amIInPath(path)==false){
        char *tokenPath, *strPath, *tofreePath;
        tofreePath = strPath = strdup(path);

        //    int currentSize = pathsIKnow[destination].size;
        //    pathsIKnow[destination].pathsIKnow[currentSize+1];
        while ((tokenPath = strsep(&strPath, "-"))) {
            int pathStep = atoi(tokenPath);

        }

        free(tofreePath);
    }

    free(tofree);

}

bool amIInPath(const char *path) {//Stored the details
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

    if (delta >= seconds) {
        return true;
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
 * Because a neighbor announcing itself means 1 known path with current cost value
 * @param heardFrom
 */
void establishNeighbor(short heardFrom) {
    paths myPath = pathsIKnow[heardFrom];

    if(myPath.alreadyProcessedNeighbor == 0){
        int currentKnownSize = myPath.size;
        path newPath = myPath.pathsIKnow[++currentKnownSize];
        myPath.size = currentKnownSize;

        newPath.cost = getNeigborCost(heardFrom);
        newPath.path[0] = globalMyID;
        newPath.path[1] = heardFrom;
        newPath.idDestination = heardFrom;
        newPath.hasUpdates = 1;
        newPath.nextHop = heardFrom;

        myPath.pathsIKnow[currentKnownSize+1] = newPath;

        myPath.hasUpdates = 1; //To trigger a flood to neighbors of new paths for this destination

        pathsIKnow[heardFrom] = myPath;

        if (debug) {
            fprintf(stdout, "New Neighbor |Id:%d|Cost:%d|\n", heardFrom, newPath.cost);
        }

        myPath.alreadyProcessedNeighbor = 1;

        pathsIKnow[heardFrom] = myPath;
    }

}

/**
 * So we can add it to the path
 * @param heardFrom
 * @return
 */
int getNeigborCost(short heardFrom) {
    int cost = 1;
    if (costs[heardFrom] != NULL) {
        cost = costs[heardFrom];
    }

    return cost;
}


