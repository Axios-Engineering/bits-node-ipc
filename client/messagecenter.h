#ifndef MESSAGE_CENTER_H
#define MESSAGE_CENTER_H

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <thread>
#include <utility>
#include <cstdlib>
#include <mutex>

#include "json.hpp"

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
    //////////////////////////////////////////////////////////////////////////
    // Typedefs
    public:
        typedef std::function<void(const json&)> EventCallback;
        typedef std::function<json(const json&)> RequestListener;
        typedef std::string EventIdentifier;
        typedef std::string RequestIdentifier;

    //////////////////////////////////////////////////////////////////////////
    // Public Methods
    public:

        /**
         * MessageCenter constructor
         */
        MessageCenter(const std::string &socket_path) : 
            _socket_path(socket_path),
            _fd(0),
            _stopEvent(false) 
        {
            std::srand(std::time(0));
            _requestId = std::abs(std::rand());
        }

        /**
         * MessageCenter destructor
         */
        ~MessageCenter() {
            _stopEvent = true;
            stop();
        }

        /**
         * Start the MessageCenter.
         *
         * @async - if async is false no background threads will be created,
         * you will be required to call dispatchMessages() periodically for
         * the MessageCenter to work correctly.
         */
        bool start(bool async=true) {
            // Create the socket and connect to the BITS server
            if ( (_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
                perror("socket error");
                exit(-1);
            }

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, _socket_path.c_str(), sizeof(addr.sun_path)-1);

            if (connect(_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                perror("connect error");
                return false;
            }

            // Set socket RCV timeout to one second
            struct timeval tv;
            tv.tv_sec = 1;
            setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));

            if (async) {
                // Start thread to read incoming messages
                _readThread = _spawn(); 
            }

            return true;
        }

        /**
         * Stop the MessageCenter background thread.
         */
        void stop() {
            _readThread.join();
        }

        /**
         * Read up to 'max' messages.
         *
         * TODO - add timeout, requires nonblock get() timeout support
         */
        void dispatchMessages(const size_t max=0) {
            size_t nReceived = 0;
            while (!_stopEvent) {  
                // Read the next data segment
                std::string data = _get();
                if (data.size() > 0) {
                    // Attempt to parse it
                    try {
                      json msg = json::parse(data);
                      ++nReceived;
                      if (msg["data"]["type"] == "event") {
                          this->_handleEvent(msg["data"]);
                      } else if (msg["data"]["type"] == "response") {
                          this->_handleResponse(msg["data"]);
                      } else if (msg["data"]["type"] == "request") {
                          this->_handleRequest(msg["data"]);
                      }
                    } catch(...) {
                        continue;
                    }

                    if (max > 0 && nReceived >= max) {
                        break;
                    }
                }
            }
        }
       
        /**
         * Send a request BITS using the default scope
         */ 
        template<typename... Args>
        json sendRequest(const std::string &request, Args... args) {
            return sendRequest(request, {}, args...);
        }

        /**
         * Send a request to BITS
         */
        template<typename... Args>
        json sendRequest(
            const std::string &request,
            const std::vector<std::string> scopes,
            Args... args
        ) {
            std::string requestId = _getRequestId();
            json msg;

            msg["type"] = "bits-ipc";
            msg["data"] = {};

            msg["data"]["type"] = "request";
            msg["data"]["event"] = request;
            msg["data"]["requestId"] = requestId;
            msg["data"]["params"] = { };

            if (scopes.size() == 0) {
                msg["data"]["params"].push_back( { { "scope", nullptr } } );
            } else if (scopes.size() == 1) {
                msg["data"]["params"].push_back( { { "scope", scopes[0] } } );
            } else {
                msg["data"]["params"].push_back( { { "scopes", scopes } } );
            }

            concatArgs(msg["data"]["params"], args...);

            this->_send(msg.dump());

            std::mutex _resp_mutex;
            json resp;

            this->_addReponseListener(requestId, scopes, [&](const json &msg) {
                std::lock_guard<std::mutex> lock(_resp_mutex); 
                resp = msg;
            });

            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                {
                    std::lock_guard<std::mutex> lock(_resp_mutex); 
                    if (!resp.is_null()) {
                        break;
                    }
                }
            }

            return resp;
        }
       

        /**
         * Send an event to BITS using the default scope.
         */
        template<typename... Args>
        bool sendEvent(const std::string &event, Args... args) {
            sendEvent(event, {}, args...);
        }

        /**
         * Send an event to BITS
         */ 
        template<typename... Args>
        bool sendEvent(
            const std::string &event,
            const std::vector<std::string> scopes,
            Args... args
        ) {
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

            return this->_send(msg.dump());
        }
       
        /**
         * Register with BITS to receive events on the default scope.
         */ 
        void addEventListener(const std::string &event, const EventCallback &cb) {
            addEventListener(event, {}, cb);
        }

        /**
         * Register with BITS to receive events.
         */
        void addEventListener(
            const std::string &event,
            const std::vector<std::string> &scopes,
            const EventCallback &cb
        ) {
            _eventListeners[event].push_back(cb);

            json msg;
            msg["type"] = "bits-ipc";
            msg["data"] = {};

            msg["data"]["type"] = "addEventListener";
            msg["data"]["event"] = event;
            msg["data"]["params"] = { };

            if (scopes.size() == 0) {
                msg["data"]["params"].push_back( { { "scopes", nullptr } } );
            } else if (scopes.size() == 1) {
                msg["data"]["params"].push_back( { { "scopes", scopes[0] } } );
            } else {
                msg["data"]["params"].push_back( { { "scopes", scopes } } );
            }
            this->_send(msg.dump());
        }

        /**
         * Register with BITS to handle requests on the default scope
         */ 
        void addRequestListener(
            const std::string &event,
            const RequestListener &cb
        ) {
            addRequestListener(event, {}, cb);
        }

        /**
         * Register with BITS to handle requests on the default scope
         */
        void addRequestListener(
            const std::string &event,
            const std::vector<std::string> &scopes,
            const RequestListener &cb
        ) {
            _requestListeners[event] = cb;

            json msg;
            msg["type"] = "bits-ipc";
            msg["data"] = {};

            msg["data"]["type"] = "addRequestListener";
            msg["data"]["event"] = event;
            msg["data"]["params"] = { };

            if (scopes.size() == 0) {
                msg["data"]["params"].push_back( { { "scopes", nullptr } } );
            } else if (scopes.size() == 1) {
                msg["data"]["params"].push_back( { { "scopes", scopes[0] } } );
            } else {
                msg["data"]["params"].push_back( { { "scopes", scopes } } );
            }
            this->_send(msg.dump());
        }


    //////////////////////////////////////////////////////////////////////////
    // Internal Methods & Variables
    private:
        // the delimiter used by node-ipc
        const char* DELIMITER = "\f";

        // private member variables
        std::string _socket_path;
        int _fd;
        unsigned int _requestId;
        std::thread _readThread;
        bool _stopEvent;

        // Thread-lock mutex for protected members
        std::mutex _fd_wr_mutex;
        std::mutex _fd_rd_mutex;
        std::mutex _requestId_mutex;

        // Listeners and handlers
        std::unordered_map< EventIdentifier, std::vector<EventCallback> > _eventListeners;
        std::unordered_map< RequestIdentifier, EventCallback > _responseListeners;
        std::unordered_map< EventIdentifier, RequestListener > _requestListeners;

        /**
         * Send a message on the socket, conforming to
         * node-ipc by adding a \f delimiter
         */
        bool _send(const std::string &msg) {
            std::lock_guard<std::mutex> lock(_fd_wr_mutex); 

            if (_fd == 0) {
                return false;
            }

            if (write(_fd, msg.c_str(), msg.size()) != msg.size()) {
                return false;
            }

            if (write(_fd, DELIMITER, strlen(DELIMITER)) != 1) {
                return false;
            }

            return true;
        }

        /**
         * Get the next message from the socket
         */
        std::string _get() {
            std::lock_guard<std::mutex> lock(_fd_rd_mutex);

            static std::string _buffer;
            char buf[1024];
            ssize_t rc;
            size_t idx;

            while ( (rc = read(_fd, buf, sizeof(buf)-1)) > 0) {
                if (_stopEvent) {
                    return "";
                }
                // Null terminate the string
                buf[rc] = '\0';
                // Append to the buffer
                _buffer.append(buf);
                // Find the delimiter
                if ((idx = _buffer.find_first_of("\f")) != std::string::npos) {
                    // Extract the message
                    std::string msg = _buffer.substr(0, idx);
                    // Delete the message from the buffer
                    _buffer.erase(0, idx+1);
                    
                    return msg;
                } 
            }

            return "";
        }

        /**
         * Spawns the dispatchMessage in a loop.
         */
        std::thread _spawn() {
            return std::thread( [this] { this->dispatchMessages(); } );
        }

        /**
         * Get the next requestId
         */
        std::string _getRequestId() {
            std::lock_guard<std::mutex> lock(_requestId_mutex);
            return std::to_string(_requestId++);
        }

        /**
         * Add a response listener for the defined requestId
         */
        void _addReponseListener(
            const std::string &requestId,
            const std::vector<std::string> &scopes,
            const EventCallback &cb
        ) {
            // TODO behaviour if requestId already is in the list
            _responseListeners[requestId] = cb;
        }

        /**
         * Handle an incoming event, passing it to the eventListeners
         */
        void _handleEvent(const json &msg) {
            std::string event = msg["event"];
            auto callbacks = _eventListeners.find(event);
            if (callbacks != _eventListeners.end()) {
                for (auto &&cb : callbacks->second) {
                    cb(msg["params"]);
                }     
            }
        }

        /**
         * Handle an incoming response, passing it to the responseListener
         */
        void _handleResponse(const json &msg) {
            std::string responseId = msg["responseId"];
            auto callback = _responseListeners.find(responseId);
            if (callback != _responseListeners.end()) {
                callback->second(msg["result"]);
            }
        }

        /**
         * Handle an incoming request, passing it to the requestListener
         */
        void _handleRequest(const json &msg) {
            std::string event = msg["event"];
            auto requestId = msg["requestId"];
            auto callback = _requestListeners.find(event);
            if (callback != _requestListeners.end()) {
                json result = callback->second(msg["params"]);

                json resp;
                resp["type"] = "bits-ipc";
                resp["data"] = {};

                resp["data"]["type"] = "response";
                resp["data"]["event"] = event;
                resp["data"]["responseId"] = requestId;
                resp["data"]["params"] = { };
                resp["data"]["params"].push_back(result);

                this->_send(resp.dump());
            }
        }

        /**
         * Prevent copy
         */
        MessageCenter(const MessageCenter& other) {
        }

        /**
         * Prevent assignment
         */
        MessageCenter& operator=(MessageCenter) {
        }

};

#endif
