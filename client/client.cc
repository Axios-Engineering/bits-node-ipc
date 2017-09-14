#include "messagecenter.h"

#include <chrono>

using namespace std;

/**
 * The bits-node-ipc module sends periodic
 * heartbeats; this can be used to check if the
 * UI is still alive.
 */
void onHeartbeat(const json &msg) {
    cout << "Received Heartbeat" << msg << endl;
}

/**
 * The bits-node-ipc module may choose to 
 * send ping messages to check if the client
 * is still alive and responding to messages.
 */
json handlePing(const json &ping) {
    cout << "Received PING" << ping << endl;
    json resp;
    resp["pong"] = std::time(0)*1000;

    return resp;
}

/**
 * Usage: ./client BITS-IPC-SOCKET-PATH
 */
int main(int argc, char *argv[]) {

  if (argc != 2) {
      fprintf(stderr, "incorrect number of arguments");
      exit(-1);
  }

  // construct the message center attached to the provided
  // Unix Domain Socket
  MessageCenter messageCenter(argv[1]);

  // start the message center
  if (!messageCenter.start()) {
      cerr << "failed to connect to BITS MessageCenter" << endl;
      exit(-1);
  };

  // handle the 'heartbeat' message
  messageCenter.addEventListener("bits-ipc#heartbeat", onHeartbeat);

  // handle 'ping' request
  messageCenter.addRequestListener("bits-ipc#ping", handlePing);

  // send a Client connected event
  messageCenter.sendEvent("bits-ipc#Client connected", {});

  // send a request for the bits system Id
  json response = messageCenter.sendRequest("base#System bitsId");
  cout << "BITS System Id " << response << endl;

  // start a loop to handle incoming events/requests
  while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // goodbye
  return 0;
}
