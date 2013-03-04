/*
+----------------------------------------------------------------------+
| This implements the Isis class. |
| It contains the protocol implmentation. |
+----------------------------------------------------------------------+
*/
#include "Isis.h"

using namespace std;

// Constructor to initialize the member variables and start listening on a port for incoming connections.
Isis::Isis(ProcInfo *procInfo) throw(string) {
    this->myId = procInfo->myId;
    this->numProcs = procInfo->numProcs;
    this->numMsgs = procInfo->numMsgs;
    this->listenPort = procInfo->port;
    this->hostNames = procInfo->hostNames;
    
    this->canSend = true;
    this->latestSeqNumber = 0;
    this->msgCount = 0;
    this->ackCount = 0;
    this->listenSocketFD = -1;
    this->sendQueue = new bool[this->numProcs];

    startListening();
    if(this->listenSocketFD == -1) {
        throw string("\nDon't have a socket to listen. Hence can not receive messages.");
    }
}

// Destructor to deallocate memory and close the socket.
Isis::~Isis() {
    if(this->sendQueue) {
        delete[] sendQueue;
    }
    close(this->listenSocketFD);
}

// Opens a socket and starts listening on it. 
void Isis::startListening() throw(string) {
    int socketFD, status;
    struct addrinfo hints, *hostInfo, *curr;
    
    // Set the socket options.
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    // Get the address info given the port number and socket options.
    if((status = getaddrinfo(NULL, this->listenPort.c_str(), &hints, &hostInfo)) != 0) {
        cerr<<"getaddrinfo: "<<gai_strerror(status);
        throw string("\nCould not retrieve my address info.");
    }

    // Get the socket descriptor to use for listening.
    for(curr = hostInfo; curr != NULL; curr = curr->ai_next) {
        if((socketFD = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol)) == -1) {
            perror("Failed to create a socket for myself to listen on: socket() failed.");
            continue;
        }
        
        // Bind the socket to the port.
        if(bind(socketFD, curr->ai_addr, curr->ai_addrlen) == -1) {
            close(socketFD);
            perror("Failed to bind the socket for myself to listen on: bind() failed.");
            continue;
        }

        break;
    }
    
    if(curr == NULL) {
        throw string("\nFailed to create or bind any socket to listen on.");
    }

    freeaddrinfo(hostInfo);
    this->listenSocketFD = socketFD;
}

// Public method which starts the process. The main ISIS algorithm run in a never-ending loop.
void Isis::start() {
    struct timeval start;
    long int diff;
    bool waitingForAck = false;

    // Keeps listening like a daemon.
    while(1) {
        ssize_t numBytes;
        char buffer[BUFFER_LEN];
        struct sockaddr_in peerAddress;
        socklen_t addrLen = sizeof(peerAddress);

        // Checks if the the process can send the next message.
        if(this->msgCount < this->numMsgs && this->canSend) {
            this->msgCount++;      // Increment the message count which is also the message id.
            sendDataMessage();     // Send the data message.
            this->canSend = false; // Set the flag to false indicating a message has been sent.
            waitingForAck = true;

            gettimeofday(&start, NULL); // Record the start time.
        } else if(waitingForAck && diff > ACK_TIMEOUT) {
            retransmitMessage();        // Retransmit since all ACKS were not received in time.
            gettimeofday(&start, NULL); // Record the start time.
        }

        // Fetch data if something has arrived at the socket.
        if((numBytes = recvfrom(this->listenSocketFD, buffer, BUFFER_LEN, MSG_DONTWAIT, (struct sockaddr *) &peerAddress, &addrLen)) == -1) {
            if(errno != EWOULDBLOCK) {
                perror("Failed to receive a message: recvfrom() failed");
            }   
        } else {
            uint32_t *type = (uint32_t *) buffer; // The first field all types of messages is type.
            *type = ntohl(*type);
            if(numBytes == sizeof(DataMessage) && *type == TYPE_DATA) {
                DataMessage *msg = ntoh_data((DataMessage *) buffer);
                if(!dupDataMessage(msg)) {
                    this->latestSeqNumber++; // Increment the latest sequence number.
                    handleDataMessage(msg);
                }
            } else if(numBytes == sizeof(AckMessage) && *type == TYPE_ACK) {
                handleAckMessage(ntoh_ack((AckMessage *) buffer));
                
                // If all the ACKs have been received.
                if(this->ackCount == this->numProcs - 1) {
                    uint32_t finalSeqNumber = getFinalSeqNumber();
                    sendFinalSeqNumber(finalSeqNumber); // Multicast final sequence number to all processes.
                    deliver(finalSeqNumber);            // Deliver to app since all ACKs have been received.
                    this->canSend = true;               // Set the flag so that the next message can be sent.
                    this->ackMap.clear();               // Reset the map to maintain ACKs.
                    this->ackCount = 0;                 // Reset the ACK counter.
                    waitingForAck = false;
                }
            } else if(numBytes == sizeof(SeqMessage) && *type == TYPE_SEQ) {
                handleSeqMessage(ntoh_seq((SeqMessage *) buffer)); 
            } else {
                cerr<<"\nReceived an unknown type of message. Number of bytes received is "<<numBytes;
            }
        }

        // Record the current time and calculate the difference from the time message was sent.
        struct timeval end;
        gettimeofday(&end, NULL);
        diff = ((end.tv_sec * 1000000 + end.tv_usec) - ((start).tv_sec * 1000000 + (start).tv_usec));
    }
}

// Multicast data message.
void Isis::sendDataMessage() {
    DataMessage temp, *msg = getNewDataMessage();
    memcpy(&temp, msg, sizeof(DataMessage));

    if(msg != NULL) {
        struct timeval start;
        uint32_t diff = 0;
        
        this->latestSeqNumber++;                    // Increment the latest sequence number.
        bufferMessage(ntoh_data(&temp));            // Buffer the converted message. 
        memset(this->sendQueue, 0, this->numProcs); // Initialize the send status array.
        gettimeofday(&start, NULL);                 // Record the start time before multicast starts.

        do {
            for(uint32_t procId = 0; procId < this->numProcs; procId++) {
                // I don't want to send the message to myself. :)
                if(procId != this->myId && !this->sendQueue[procId]) {
                    this->sendQueue[procId] = sendMessage(msg, sizeof(DataMessage), hostNames[procId]);
                }
            }

            // Record the current time and calculate the difference from the time message was sent.
            struct timeval end;
            gettimeofday(&end, NULL);
            diff = ((end.tv_sec * 1000000 + end.tv_usec) - ((start).tv_sec * 1000000 + (start).tv_usec));

        } while(!allSent() && diff < SEND_TIMEOUT);
        delete msg;
    }
}

// Sends a message to a host.
bool Isis::sendMessage(void *msg, uint32_t msgSize, string hostname) {
    bool sendStatus;
    int socketFD, status, numbytes;
    struct addrinfo hint, *hostInfo, *curr;

    // Set the appropriate flags.
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_flags = AI_PASSIVE;

    // Get the address info of the host to send to.
    if((status = getaddrinfo(hostname.c_str(), this->listenPort.c_str(), &hint, &hostInfo)) != 0) {
        cerr<<"getaddrinfo: "<<gai_strerror(status);
        sendStatus = NOT_SENT;
    } else {
        // Get the socket descriptor to use for sending the message.
        for(curr = hostInfo; curr != NULL; curr = curr->ai_next) {
            if((socketFD = socket(curr->ai_family, curr->ai_socktype, curr->ai_protocol)) == -1) {
                perror("Failed to create a socket: socket() failed.");
                continue;
            }
            break;
        }

        // Socket creation failed for all options.
        if(curr == NULL) {
            cerr<<"\nFailed to create any socket for "<<hostname;
            sendStatus = NOT_SENT;
        } else {
            // Try sending the message to host.
            if((numbytes = sendto(socketFD, msg, msgSize, 0, curr->ai_addr, curr->ai_addrlen)) == -1) {
                cerr<<"Failed to send message to "<<hostname;
                perror("Failed to send message: sendto() failed");
                sendStatus = NOT_SENT;
            } else {
                // Update the status of the sending.
                sendStatus = SENT;
            }
            close(socketFD);
        }
        freeaddrinfo(hostInfo);
    }

    return sendStatus;
}

// Checks if the message was sent to all hosts.
bool Isis::allSent() {
    for(uint32_t procId = 0; procId < this->numProcs; procId++) {
        if(procId != this->myId && !this->sendQueue[procId]) {
            return false;
        }
    }
    return true;
}

// Finds out if the received DataMessage is a duplicate one.
bool Isis::dupDataMessage(DataMessage *msg) {
    msgPair msgObj(msg->sender, msg->msg_id);
    bool isDuplicate = true;
    
    if(this->msgHistory.count(msgObj) == 0) {
        this->msgHistory.insert(msgObj);
        isDuplicate = false;
    }
    return isDuplicate;
}

// Handles a DataMessage that has been received.
void Isis::handleDataMessage(DataMessage *msg) {
    bufferMessage(msg); // Buffer the message. 
    sendAck(msg);       // Send an ACK to the sender.
}

// Handles an AckMessage that has been received.
void Isis::handleAckMessage(AckMessage *ack) {
    // If an ACK has already been received from this host.
    if(this->ackMap.count(ack->receiver) != 0) {
        return;
    }
    this->ackMap[ack->receiver] = ack->proposed_seq; // Store the proposed sequence number.
    this->ackCount++;                                // Increase the ACK count.
}

// Handles a SeqMessage that has been received.
void Isis::handleSeqMessage(SeqMessage *msg) {
    msgPair oldKey(getInitSeqNumber(msg->sender, msg->msg_id), msg->sender);
    msgPair newKey(msg->final_seq, msg->sender);
    msgPair value(msg->msg_id, DELIVERABLE);

    this->msgMap.erase(oldKey);     // Remove the old message entry.
    this->msgMap[newKey] = value;   // Insert the new messageenrty.
    deliverToApp();                 // Deliver all DELIVERABLE messages.
   
    if(msg->final_seq > this->latestSeqNumber) {
        this->latestSeqNumber = msg->final_seq;
    }
}

// Fetches the initial sequence number with which a message was stored in the data structure.
uint32_t Isis::getInitSeqNumber(uint32_t sender, uint32_t msgId) {
    for(map<msgPair, msgPair>::iterator iter = this->msgMap.begin(); iter != msgMap.end(); iter++) {
        if(iter->first.second == sender &&  iter->second.first == msgId) {
            return iter->first.first;
        }
    }
    return 0;
}

// Marks the message and buffers it in the data structure.
void Isis::bufferMessage(DataMessage *msg) {
    msgPair key(this->latestSeqNumber, msg->sender);
    msgPair value(msg->msg_id, UNDELIVERABLE);
    
    this->msgMap[key] = value; // Buffer the message into data structure.
}

// Sends an ACK in response to the data message.
void Isis::sendAck(DataMessage *dataMsg) {
    AckMessage *msg = getNewAckMessage(dataMsg);
    if(msg != NULL) {
        bool status = sendMessage(msg, sizeof(AckMessage), hostNames[dataMsg->sender]);
        delete msg;
    }
}

// Delivers to the app after the final ACK is received.
void Isis::deliver(uint32_t finalSequenceNumber) {
    SeqMessage msg;
    msg.type = TYPE_SEQ;
    msg.sender = this->myId;
    msg.msg_id = this->msgCount;
    msg.final_seq = finalSequenceNumber;
    handleSeqMessage(&msg);
}

// Delivers the data to the app. Here we just display the result.
void Isis::deliverToApp() {
    map<msgPair, msgPair >::iterator iter = this->msgMap.begin();
    while(iter->second.second == DELIVERABLE) {
        cout<<"\n"<<this->myId + 1<<": Processed message "<<iter->second.first<<" from "<<iter->first.second + 1<<" with seq "<<iter->first.first;
        cout.flush();
        this->msgMap.erase(iter);    // Remove the entry from msgMap.
        iter = this->msgMap.begin(); // Fetch the first element again.
    }
}

// Determines the final sequence number.
uint32_t Isis::getFinalSeqNumber() {
    uint32_t maxSeqNumber = 0;
    for(map<uint32_t, uint32_t>::iterator iter = this->ackMap.begin(); iter != this->ackMap.end(); iter++) {
        if(iter->second > maxSeqNumber) {
            maxSeqNumber = iter->second;
        }
    }
    return maxSeqNumber;
}

// Multicasts the final sequence number for the message.
void Isis::sendFinalSeqNumber(uint32_t finalSeqNumber) {
    SeqMessage *msg = getNewSeqMessage(finalSeqNumber);
    if(msg != NULL) {
        for(uint32_t procId = 0; procId < this->numProcs; procId++) {
            // I don't want to send a message to myself. :)
            if(procId != this->myId) {
                bool status = sendMessage(msg, sizeof(SeqMessage), hostNames[procId]);
            }
        }
        delete msg;
    }
}

// Retransmits messages to all hosts from which an ACK hasn't been received.
void Isis::retransmitMessage() {
    DataMessage *msg = getNewDataMessage();
    if(msg != NULL) {
        for(uint32_t procId = 0; procId < this->numProcs; procId++) {
            // I don't want to send a message to myself. :)
            if(procId != this->myId) {
                if(this->ackMap.count(procId) == 0) {
                    bool status = sendMessage(msg, sizeof(DataMessage), hostNames[procId]);
                }
            }
        }
        delete msg;
    }
}

// Constructs a DataMessage and returns the pointer to it. 
DataMessage* Isis::getNewDataMessage() {
    DataMessage *msg = new DataMessage;
    msg->type = TYPE_DATA;
    msg->sender = this->myId;
    msg->msg_id = this->msgCount;
    msg->data = DATA;
    return hton_data(msg); 
}

// Constructs an AckMessage and returns the pointer to it. 
AckMessage* Isis::getNewAckMessage(DataMessage *dataMsg) {
    AckMessage *msg = new AckMessage;
    msg->type = TYPE_ACK;
    msg->sender = dataMsg->sender;
    msg->msg_id = dataMsg->msg_id;
    msg->proposed_seq = this->latestSeqNumber;
    msg->receiver = this->myId;
    return hton_ack(msg); 
}

// Constructs a SeqMessage and returns the pointer to it. 
SeqMessage* Isis::getNewSeqMessage(uint32_t finalSeqNumber) {
    SeqMessage *msg = new SeqMessage;
    msg->type = TYPE_SEQ;
    msg->sender = this->myId;
    msg->msg_id = this->msgCount;
    msg->final_seq = finalSeqNumber;
    return hton_seq(msg); 
}

// Converts a DataMessage from host to network byte order.
DataMessage* Isis::hton_data(DataMessage *msg) {
    msg->type = htonl(msg->type);
    msg->sender = htonl(msg->sender);
    msg->msg_id = htonl(msg->msg_id);
    msg->data = htonl(msg->data);
    return msg;
}

// Converts a DataMessage from network to host byte order.
DataMessage* Isis::ntoh_data(DataMessage *msg) {
    msg->sender = ntohl(msg->sender);
    msg->msg_id = ntohl(msg->msg_id);
    msg->data = ntohl(msg->data);
    return msg;
}

// Converts an AckMessage from host to network byte order.
AckMessage* Isis::hton_ack(AckMessage *msg) {
    msg->type = htonl(msg->type);
    msg->sender = htonl(msg->sender);
    msg->msg_id = htonl(msg->msg_id);
    msg->proposed_seq = htonl(msg->proposed_seq);
    msg->receiver = htonl(msg->receiver);
    return msg;
}

// Converts an AckMessage from network to host byte order.
AckMessage* Isis::ntoh_ack(AckMessage *msg) {
    msg->sender = ntohl(msg->sender);
    msg->msg_id = ntohl(msg->msg_id);
    msg->proposed_seq = ntohl(msg->proposed_seq);
    msg->receiver = ntohl(msg->receiver);
    return msg;
}

// Converts a SeqMessage from host to network byte order.
SeqMessage* Isis::hton_seq(SeqMessage *msg) {
    msg->type = htonl(msg->type);
    msg->sender = htonl(msg->sender);
    msg->msg_id = htonl(msg->msg_id);
    msg->final_seq = htonl(msg->final_seq);
    return msg;
}

// Converts a SeqMessage from network to host byte order.
SeqMessage* Isis::ntoh_seq(SeqMessage *msg) {
    msg->sender = ntohl(msg->sender);
    msg->msg_id = ntohl(msg->msg_id);
    msg->final_seq = ntohl(msg->final_seq);
    return msg;
}
