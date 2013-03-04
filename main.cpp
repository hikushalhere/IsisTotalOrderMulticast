/*
+----------------------------------------------------------------------+
| The Isis Total Order Algorithm. |
+----------------------------------------------------------------------+
| http://dl.acm.org/citation.cfm?id=7478 |
+----------------------------------------------------------------------+
| This is an implementation of the ABCAST protocol, |
| as described in the paper (link given above). |
+----------------------------------------------------------------------+
| This source file is the entry point of the implementation. |
+----------------------------------------------------------------------+
*/

#include <cstring>
#include <fstream>
#include "Isis.h"

#define NOP 0
#define PORT 1
#define HOSTFILE 2
#define COUNT 3

#define MIN_PORT_NUM 1024
#define MAX_PORT_NUM 65535

#define HOST_NAME_LEN 256

using namespace std;

Isis *bootstrap(string, char *, int, int *); // Bootstraps the application.
void printUsage();                           // Prints the usage.

// The show starts here!
int main(int argc, char **argv) {
    int nextArg = NOP, count, portNum;
    char *hostFilePath;
    string port;
    bool proceed = true;

    // Parses the command line arguments and reads the values passed.
    for(int i = 1; i < argc && proceed; i++) {
        if(argv[i][0] == '-') {
            if(strlen(argv[i]) != 2) {
                proceed = false;
                continue;
            }
            
            switch(argv[i][1]) {
                case 'p':
                    nextArg = PORT;
                    break;

                case 'h':
                    nextArg = HOSTFILE;
                    break;

                case 'c':
                    nextArg = COUNT;
                    break;

                default:
                    printUsage();
                    proceed = false;
            }
        } else {
            switch(nextArg) {
                case PORT:
                    port = string(argv[i]);
                    portNum = atoi(argv[i]);
                    if(portNum < MIN_PORT_NUM || portNum > MAX_PORT_NUM) {
                        cerr<<"The port number should lie between 1024 and 65535 including both.";
                        proceed = false;
                        continue;
                    }
                    break;

                case HOSTFILE:
                    hostFilePath = argv[i];
                    break;

                case COUNT:
                    count = atoi(argv[i]);
                    if(count < 0) {
                        proceed = false;
                    }
                    break;

                case NOP:
                    printUsage();
                    proceed = false;
                    break;
            }
        }
    }

    // All OK. The command line arguments were fine.
    if(proceed) {
        int myId = -1;
        Isis *isisNode = bootstrap(port, hostFilePath, count, &myId);
        if(isisNode && myId >= 0) {
            isisNode->start();
        }   
    }
}

// Prints the usage.
void printUsage() {
    cout<<"Incorrect usage.";
    cout<<"\nUsage: proj2 -p port -h hostfile -c count";
    cout.flush();
}

// Reads the host file and builds the required data structures and, instantiates the object.
Isis *bootstrap(string port, char *hostFilePath, int count, int *myId) {
    int status, numProcs = 0;
    uint32_t commanderId;
    char myHostName[HOST_NAME_LEN];
    vector<string> hostNames;
    ifstream hostfile(hostFilePath);
    Isis *isisNode = NULL;

    *myId = -1;

    // Read the hostfile and prepare the required data structures related to the hosts (generals).
    if(hostfile.is_open()) {
        if((status = gethostname(myHostName, HOST_NAME_LEN)) != 0) {
            perror("Error encountered in fetching my host name.");
        }
        while(hostfile.good()) {
            string hostName;
            getline(hostfile, hostName);
            if(!hostName.empty()) {
                hostNames.push_back(hostName);
                struct hostent *host = gethostbyname(hostName.c_str()); // Get the host address from host name.

                if(host != NULL) {
                    struct in_addr hostAddress;
                    memcpy(&hostAddress, host->h_addr_list[0], sizeof(struct in_addr)); // Extract the IP adress from hostent structure.
                }

                if(strcmp(myHostName, hostName.c_str()) == 0) {
                    *myId = numProcs;
                }
                numProcs++;
            }
        }
        hostfile.close();
    }
    
    if(*myId >= 0) {
        ProcInfo *procInfo = new ProcInfo;

        // Prepare the object with the required information to be passed to the constructors.
        if(procInfo) {
            procInfo->myId = (uint32_t) *myId;
            procInfo->port = port;
            procInfo->numProcs = numProcs;
            procInfo->numMsgs = count;
            procInfo->hostNames = hostNames;
            
            try {
                isisNode = new Isis(procInfo);
                delete procInfo;
            } catch(string msg) {
                cerr<<msg;
            }

        }
    } else {
        cerr<<"\nMy hostname was not found in the file: "<<hostFilePath;
    }

    return isisNode;
}
