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
          try {
            if (msg.type === "event") {
              messageCenter.sendEvent(msg.event, ...msg.params);
            } else if (msg.type === "request") {
              messageCenter.sendRequest(msg.event, ...msg.params)
              .then((...data) => {
                console.log("response", ...data);
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
              });;
            }
          } catch (err) {
            logger.warn('Failed to send IPC message', err);
            return;
          }
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
        console.log("IPC client connected");
      });
    }

    load(messageCenter) {
      console.log('Loading Node IPC');
      return startIpcServer(messageCenter)
      .then(() => this._messenger.load(messageCenter));
//      .then(() => messageCenter.sendRequest('tutorials-message-center#Cat create', {scopes: ['public']}, {type: 'loaded'}))
//      .then(() => messageCenter.sendRequest('myRequest', {scopes: null}, 'Nic', 'Mike'))
//      .then(() => messageCenter.sendEvent('myEvent', {scopes: null}, 'Nic'));

    }

    unload() {
      console.log('Unloading Node IPC');
      return Promise.resolve()
      .then(() => this._messenger.unload());
    }
  }

  module.exports = new ModuleApp();

})();
