# bits-node-ipc

This module used the node-ipc module to provide a binding between the BITS
MessageCenter and external applications.  This allows external processes to
directly send and receive BITS events, requests, and responses.

# Installation


    git clone https://github.com/Axios-Engineering/bits-node-ipc.git
    cd bits-node-ipc
    npm install
    ln -s $PWD $BITS_HOME/data/base/modules/modules/bits-node-ipc

# C++ Client

The client folder contains an example C++11 application and the MesageCenter
header file.

First you include the MessageCenter header file and construct a MessageCenter
object.  The `bits-node-ipc` module creates a Unix Domain Socket at
/tmp/bits.systemid.
    
    #include "MessageCenter.h"

    MessageCenter messageCenter("/tmp/bits.systemid");

Requests are sent via the sendRequest() method:

    json response = messageCenter.sendRequest("base#System bitsId");
    std::string systemId = response[0];

Events are sent with the sendEvent() method:

    messageCenter.sendEvent("bits-ipc#Client connected");


Request and Event Listeners are also supported:

    void onHeartbeat(const json &msg) {
        cout << "Received Heartbeat" << msg << endl;
    }

    json handlePing(const json &ping) {
        cout << "Received PING" << ping << endl;
        json resp;
        resp["pong"] = std::time(0)*1000;

        return resp;
    }

    // handle the 'heartbeat' message
    messageCenter.addEventListener("bits-ipc#heartbeat", onHeartbeat);

    // handle 'ping' request
    messageCenter.addRequestListener("bits-ipc#ping", handlePing);
