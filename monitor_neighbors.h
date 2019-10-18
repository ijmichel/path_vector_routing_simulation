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
#include <stdbool.h>

enum MAX_NEIGHBOR {MAX_NEIGHBOR = 3};
typedef struct {
    int idDestination; //id of node I know how to get to
    int cost; //cost to get there
    int path[MAX_NEIGHBOR]; //The [0]th element is the id of the first node in the path --Used to find loops
    int alreadyKnow; //To trigger an update if not alreadyknown
    int nextHop; //In order to know where to go next for this destination
} path;

extern struct timeval globalLastHeartbeat[MAX_NEIGHBOR];
extern struct sockaddr_in globalNodeAddrs[MAX_NEIGHBOR];
extern bool idsWithUpdates[MAX_NEIGHBOR];
extern char costs[MAX_NEIGHBOR];

extern bool debug;
static const int SLEEPTIME = 300 * 1000 * 1000; //300 ms
extern path pathsIKnow[1000];
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

char *convertKnownPathToBuffer(int destinationOfKnownPath);

char *concatIt(const char *s1, const char *s2);

void hackyUpdateKnownPaths();

char *addNumberToString(char *stringToAddto, int valueToAdd, bool prePendValue, char *delimiter);

char *addStringToString(char *stringToAddto, char *valueToAdd, bool prePendValue, char *delimiter);

bool hasNewPathInfoFor(int i);

char *buildMessage(char *command, int destinationId, char *data);



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

void hackyUpdateKnownPaths() {

    int i;
    for (i = 0; i < MAX_NEIGHBOR; i++)
        if (i != globalMyID) {
            char *updateMessageToSend;
            if (hasNewPathInfoFor(i)) {
                char *currentPath = convertKnownPathToBuffer(i);

                updateMessageToSend = buildMessage("NEWPATH",i,currentPath);

                for (int possibleNeighbor = 0; possibleNeighbor < MAX_NEIGHBOR; possibleNeighbor++) {//Tell all about my new fancy path
                    if (debug) {
                        fprintf(stdout, "Update Neighbor [%d] With NEWPATH To [Id:%d][Path:%s]\n",possibleNeighbor, i, currentPath);
//                        fprintf(stdout, "Update Neighbors With: |Message:[%s]\n", updateMessageToSend);
                    }
                    sendto(globalSocketUDP, updateMessageToSend, sizeof(updateMessageToSend), 0,
                           (struct sockaddr *) &globalNodeAddrs[possibleNeighbor], sizeof(globalNodeAddrs[possibleNeighbor]));

                }
                idsWithUpdates[i] = false;

            }

        }

}

char *buildMessage(char *command, int destinationId, char *data) {
    //|command|destinationId|data|
    char *builtMessage;
    builtMessage = addStringToString("|", command, false, "|");
    builtMessage = addNumberToString(builtMessage, destinationId, false, "|");
    builtMessage = addStringToString(builtMessage, data, false, "|");

    return builtMessage;
}


bool hasNewPathInfoFor(int i) { return idsWithUpdates[i]; }


char *convertKnownPathToBuffer(int destinationOfKnownPath) {
    path dPath = pathsIKnow[destinationOfKnownPath];
    char *fullPath = "";
    int pathStep = 0;
    for (pathStep = 0; pathStep < MAX_NEIGHBOR; pathStep++) {
        int nextStep = dPath.path[pathStep];
        if (nextStep != 999) {
            if (fullPath != "") { //So we don't add a - in the begin of path with no number yet
                fullPath = concatIt(fullPath, "-");
            }
            fullPath = addNumberToString(fullPath, nextStep, false, NULL);
        }
    }

    return fullPath;
}

char *addNumberToString(char *stringToAddto, int valueToAdd, bool prePendValue, char *delimiter) {
    char *stepChar[3]; //Because 256 is greatest value we get (3 size)
    sprintf(stepChar, "%d", valueToAdd);

    return addStringToString(stringToAddto, stepChar, prePendValue, delimiter);
}

char *addStringToString(char *stringToAddto, char *valueToAdd, bool prePendValue, char *delimiter) {
    if (prePendValue) { //So I can reuse this for post or prependin g:)
        stringToAddto = concatIt(valueToAdd, stringToAddto);

        if (delimiter != NULL)
            stringToAddto = concatIt(stringToAddto, delimiter);
    } else {
        stringToAddto = concatIt(stringToAddto, valueToAdd);
        if (delimiter != NULL)
            stringToAddto = concatIt(stringToAddto, delimiter);
    }

    return stringToAddto;
}

char *concatIt(const char *s1, const char *s2) {
    char *result = malloc(strlen(s1) + strlen(s2) + 1);
    strcpy(result, s1);
    strcat(result, s2);

    return result;
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
        }else{
            fprintf(stdout, "NEWPATH RECEIVED %s\n", recvBuf);
        }

        if (!strncmp(recvBuf, "NEWPATH", 7)) {
            if(debug)
                fprintf(stdout, "NEWPATH RECEIVED %s\n", recvBuf);

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
    path myPath = pathsIKnow[heardFrom];

    if (myPath.alreadyKnow ==
        0) { //Because if we already know the neighbors path then no work needs to occur to update neighbors other than it of its presence
        myPath.cost = getNeigborCost(heardFrom);
        myPath.path[0] = globalMyID;
        myPath.path[1] = heardFrom;
        myPath.idDestination = heardFrom;
        myPath.alreadyKnow = 1;

        pathsIKnow[heardFrom] = myPath;

        //Used by update thread to know what paths to send updates for
        idsWithUpdates[heardFrom] = true;

        if (debug) {
            fprintf(stdout, "New Neighbor |Id:%d|Cost:%d|\n", heardFrom, myPath.cost);
        }
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


