/**
Copyright 2017 LGS Innovations

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**/

(() => {
  'use strict';
    
  const EventEmitter = require('events');
  const fs = require('fs');
  const ipc = require('node-ipc');
  const logger = global.helper.LoggerFactory.getLogger();

  const Messenger = global.helper.Messenger;

  // see MessageCenter._messageHandler
  const validMessageTypes = [
    'event',
    'request',
    'response',
    'addEventListener',
    'removeEventListener',
    'addRequestListener',
    'removeRequestListener',
    'addResponseListener',
    'removeResponseListener',
    'addEventSubscriberListener',
    'removeEventSubscriberListener'
  ];

  let pendingRequests = {};
  let responseEmitter = new EventEmitter();

  function handleIpcMessage(messageCenter, socket, msg) {
    try {
      if (msg.type === "event") {
        // Events are easy, just forward them to the BITS message center
        messageCenter.sendEvent(msg.event, ...msg.params);
      } else if (msg.type === "request") {
        // For requests we pass the request to the BITS message center
        // tied to a callback with this socket.  When the BITS
        // response comes back we forward it to IPC
        messageCenter.sendRequest(msg.event, ...msg.params)
        .then((...data) => {
          ipc.server.emit(
            socket,
            'bits-ipc',
            {
              type: 'response',
              event: msg.event,
              responseId: msg.requestId,
              err: null,
              result: data
            }
          );
        })
        .catch((err) => {
          logger.error('error on request', err);
        });
      } else if (msg.type === "response") {
        // For requests we pass the request to the BITS message center
        // tied to a callback with this socket.  When the BITS
        // response comes back we forward it to IPC
        responseEmitter.emit(msg.event, msg.responseId, msg.err, msg.params);
      } else if (msg.type === "addEventListener") {
        let scope = msg.params[0];
        let listener = (...data) => { 
          if (!socket.destroyed) {
            // if the socket is still active, sent the event
            ipc.server.emit(
              socket,
              'bits-ipc',
              {
                type: 'event',
                event: msg.event,
                params: data
              }
            );
          } else {
            // otherwise remove this listener
            messageCenter.removeEventListener(msg.event, listener);
          }
        }
        messageCenter.addEventListener(msg.event, scope, listener);
      } else if (msg.type === "addRequestListener") {
        let scope = msg.params[0];
        let listener = (metadata, ...data) => { 
          if (!socket.destroyed) {
            // if the socket is still active, sent the event
            ipc.server.emit(
              socket,
              'bits-ipc',
              {
                type: 'request',
                requestId: metadata.requestId,
                event: msg.event,
                params: data
              }
            );


            let responsePromise = new Promise((resolve, reject) => {
              const handleIpcResponse = (responseId, err, result) => {
                if (responseId === metadata.requestId) {
                  responseEmitter.removeListener(msg.event, handleIpcResponse);
                  if (err) {
                    reject(err);
                  } else {
                    resolve(result);
                  }
                }
              };
              responseEmitter.on(msg.event, handleIpcResponse);

            });

            return responsePromise;
          } else {
            // otherwise remove this listener
            messageCenter.removeRequestListener(msg.event, listener);
            return Promise.resolve();
          }
        }
        messageCenter.addRequestListener(msg.event, scope, listener);
      }
    } catch (err) {
      logger.warn('Failed to send IPC message', err);
      return;
    }
  }

  function startIpcServer(messageCenter) {
    return messageCenter.sendRequest('base#System bitsId')
    .then((systemId) => {
      // create the socket path
      const socketPath = ipc.config.socketRoot + "bits." + systemId;

      // setup the server
      ipc.serve(socketPath);
      ipc.server.on('start', () => {
        logger.info(`IPC server started at ${socketPath}`);
      });
      
      // Handle incoming messages from clients
      ipc.server.on('bits-ipc', (msg, socket) => {
        if (!msg) {
          logger.warn('Received empty IPC message');
          return;
        }
        
        logger.debug('Received IPC message', msg);
 
        if (msg && validMessageTypes.includes(msg.type)) {
          handleIpcMessage(messageCenter, socket, msg);
        } else {
          logger.warn('Ignoring invalid message');
        }
      });

      // start the server
      ipc.server.start();
    })
    .catch((err) => {
      console.error("Failed to get the BITS system id:", err);
    });
  }

  class ModuleApp {

    constructor() {
      this._messenger = new global.helper.Messenger();
      this._messenger.addEventListener('bits-ipc#Client connected', {scopes: null}, (name) => {
        logger.info("IPC client connected");
      });
    }

    load(messageCenter) {
      console.log('Loading Node IPC');
      return startIpcServer(messageCenter)
      .then(() => this._messenger.load(messageCenter))
      .then(() => this.startHeartbeat(messageCenter));
    }

    startHeartbeat(messageCenter) {
      setInterval(() => {
        messageCenter.sendEvent('bits-ipc#heartbeat', {scopes: null}, Date.now())
        messageCenter.sendRequest('bits-ipc#ping', {scopes: null}, Date.now())
        .then((result) => {
          // TODO update a watchdog?
        })
        .catch((err) => { console.log(err) });
      }, 1000);
    }

    unload() {
      console.log('Unloading Node IPC');
      return Promise.resolve()
      .then(() => this._messenger.unload());
    }
  }

  module.exports = new ModuleApp();

})();
