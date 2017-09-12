#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include "json.hpp"

using namespace std;
using json = nlohmann::json;

/*
 * Variadic template for pushing arguments onto a JSON array
 */
void concatArgs(json &holder) {
}

template<typename T>
void concatArgs(json &holder, T t) {
    holder.push_back(t);
}

template<typename T, typename... Args>
void concatArgs(json &holder, T t, Args... rest)
{
    holder.push_back(t);
    concatArgs(holder, rest...);
}


/*
 * C++ adapter to bits-ipc message center
 */
class MessageCenter {

  private:
    std::string socket_path;
    std::string buffer;
    int fd;

    bool send(const std::string &msg) const {
      if (fd == 0) {
        return false;
      }

      cout << msg << endl;
      if (write(fd, msg.c_str(), msg.size()) != msg.size()) {
        return false;
      }

      if (write(fd, "\f", 1) != 1) {
	return false;
      }

      return true;
    }

    std::string get() {
       char buf[1024];
       size_t rc;
       size_t idx;

       while ( (rc = read(fd, buf, sizeof(buf))) > 0) {
	 buffer.append(&buf[0]);
	 if ((idx = buffer.find_first_of("\f")) != string::npos) {
	   std::string msg = buffer.substr(0, idx);
	   buffer.erase(0, idx+1);
	   return msg;
	 } 
       }
    }

  public:

    MessageCenter(const std::string &socket_path) : socket_path(socket_path), fd(0) {
    }

    bool start() {
      if ( (this->fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket error");
        exit(-1);
      }

      struct sockaddr_un addr;
      memset(&addr, 0, sizeof(addr));
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path)-1);

      if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect error");
        return false;
      }

      sendEvent("bits-ipc#Client connected", {});
      return true;
    }
    
    json sendRequest(const string &request) {
	return sendRequest(request, {});
    }

    bool sendEvent(const string &event) {
	sendEvent(event, {});
    }

    template<typename... Args>
    json sendRequest(const string &request, const std::vector<string> scopes, Args... args)
    {
        json msg;

	msg["type"] = "bits-ipc";
	msg["data"] = {};

        msg["data"]["type"] = "request";
        msg["data"]["event"] = request;
        msg["data"]["requestId"] = "test"; // TODO
        msg["data"]["params"] = { };

        if (scopes.size() == 0) {
          msg["data"]["params"].push_back( { { "scope", nullptr } } );
        } else if (scopes.size() == 1) {
          msg["data"]["params"].push_back( { { "scope", scopes[0] } } );
        } else {
          msg["data"]["params"].push_back( { { "scopes", scopes } } );
        }

        concatArgs(msg["data"]["params"], args...);

        this->send(msg.dump());

	std::string data = get();

	json resp = json::parse(data);

	// TODO if resp["err"] throw exception
	// TODO what do we do if responseId does not match requestId?
	// TODO what do we do with events we get?

	return resp["data"]["result"][0];
    }
    
    template<typename... Args>
    bool sendEvent(const string &event, const std::vector<string> scopes, Args... args) const
    {
        json msg;

	msg["type"] = "bits-ipc";
	msg["data"] = {};

        msg["data"]["type"] = "event";
        msg["data"]["event"] = event;
        msg["data"]["params"] = { };

        if (scopes.size() == 0) {
          msg["data"]["params"].push_back( { { "scope", nullptr } } );
        } else if (scopes.size() == 1) {
          msg["data"]["params"].push_back( { { "scope", scopes[0] } } );
        } else {
          msg["data"]["params"].push_back( { { "scopes", scopes } } );
        }

        concatArgs(msg["data"]["params"], args...);

        return this->send(msg.dump());
    }


};

int main(int argc, char *argv[]) {

  if (argc != 2) {
    fprintf(stderr, "incorrect number of arguments");
    exit(-1);
  }

  cout << "construct" << endl;
  MessageCenter messageCenter(argv[1]);
  cout << "start" << endl;
  if (!messageCenter.start()) {
    cerr << "failed to connect to BITS MessageCenter" << endl;
    exit(-1);
  };

  cout << "sendRequest" << endl;
  json response = messageCenter.sendRequest("base#System bitsId");
  cout << "response " << response << endl;

  return 0;
}
