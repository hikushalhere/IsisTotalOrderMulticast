/*
+----------------------------------------------------------------------+
| This is the class defintion of Isis that implements the protocl. |
+----------------------------------------------------------------------+
*/

#ifndef ISIS_H
#define ISIS_H

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "message_format.h"

#define BUFFER_LEN sizeof(AckMessage) // AckMessage has the highest size
#define DATA 1                        // Dummy integer

#define NOT_SENT false 
#define SENT true

#define SEND_TIMEOUT 500000 // in microseconds
#define ACK_TIMEOUT 2000000 // in microseconds

#define TYPE_DATA 1
#define TYPE_ACK 2
#define TYPE_SEQ 3

#define UNDELIVERABLE 0
#define DELIVERABLE 1

// Data structure to pass information about a general (Commander or Leiutenant).
typedef struct {
    uint32_t myId;
    int numProcs;
    int numMsgs;
    std::string port;
    std::vector<std::string> hostNames;
    std::map<unsigned long, uint32_t> ipToId;
} ProcInfo;

typedef std::pair<uint32_t, uint32_t> msgPair; // (seqNumber, procId) or (msgId, marker) or (procId, msgId)

// Class definition.
class Isis {

    protected:
        uint32_t myId;            // My process id.
        uint32_t numProcs;        // Total number of processes in the system.
        uint32_t numMsgs;         // Total number of messages to send.
        uint32_t msgCount;        // Number of messages sent. Will use for message id as well.
        uint32_t ackCount;        // Number of ACKs collected.
        uint32_t latestSeqNumber; // Current sequence number. Starts from 1.
        int listenSocketFD;       // File descriptor of the socket on which the general is listening on.
        bool *sendQueue;          // Maintains the status of the sent messages.
        bool canSend;             // Flag to indicate if a message can be sent.

        std::string listenPort;                // Port to listen on.
        std::vector<std::string> hostNames;    // Vector of host names in the system.
        std::map<msgPair, msgPair> msgMap;     // Map for (seqNumber, procId) : (msgId, marker).
        std::map<uint32_t, uint32_t> ackMap;   // Map for procId : proposedSeqNumber.
        std::set<msgPair> msgHistory;          // Set for keeping a history of messages received till now.

        void startListening() throw(std::string);      // Opens a socket and starts listening on it. 
        void sendDataMessage();                        // Multicast data message.
        bool sendMessage(void*, uint32_t, std::string); // Sends a message to a host.
        bool allSent();                                // Checks if the message was sent to all hosts.
        bool dupDataMessage(DataMessage *);            // Finds out if the received DataMessage is a duplicate one.
        void handleDataMessage(DataMessage *);         // Handles a DataMessage that has been received.
        void handleAckMessage(AckMessage *);           // Handles an AckMessage that has been received.
        void handleSeqMessage(SeqMessage *);           // Handles a SeqMessage that has been received.
        uint32_t getInitSeqNumber(uint32_t, uint32_t); // Fetches the initial sequence number with which a message was stored in the data structure.
        void bufferMessage(DataMessage *);             // Marks the message and buffers it in the data structure. 
        void sendAck(DataMessage *);                   // Sends an ACK in response to the data message.
        void deliver(uint32_t);                        // Delivers to the app after the final ACK is received.
        void deliverToApp();                           // Delivers the data to the app. Here, we just display the result.
        uint32_t getFinalSeqNumber();                  // Determines the final sequence number.
        void sendFinalSeqNumber(uint32_t);             // Multicasts the final sequence number for the message.
        void retransmitMessage();                      // Retransmits messages to all hosts from which an ACK hasn't been received.
        DataMessage *getNewDataMessage();              // Constructs a new DataMessage and returns a pointer to it.
        AckMessage *getNewAckMessage(DataMessage *);   // Constructs a new AckMessage and returns a pointer to it.
        SeqMessage *getNewSeqMessage(uint32_t);        // Constructs a new SeqMessage and returns a pointer to it.
        DataMessage *hton_data(DataMessage *);         // Converts a DataMessage from host to network byte order.
        DataMessage *ntoh_data(DataMessage *);         // Converts a DataMessage from network to host byte order.
        AckMessage *hton_ack(AckMessage *);            // Converts an AckMessage from host to network byte order.
        AckMessage *ntoh_ack(AckMessage *);            // Converts an AckMessage from network to host byte order.
        SeqMessage *hton_seq(SeqMessage *);            // Converts a SeqMessage from host to network byte order.
        SeqMessage *ntoh_seq(SeqMessage *);            // Converts a SeqMessage from network to host byte order.

    public:
        Isis(ProcInfo *) throw(std::string); // Constructor to initialize the member variables and start listening on a port for incoming connections.
        ~Isis();                             // Destructor to deallocate memory and close the socket.
        void start();                        // Public method which starts the process. The main ISIS algorithm run in a never-ending loop.
};

#endif
