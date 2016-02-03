/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2015
 *
 * This provides a class to configure, track the state of, and execute simple
 * program functions.
 ******************************************************************************/

#include <Arduino.h>
#include <HMTLProtocol.h>

#ifndef DEBUG_LEVEL
  #define DEBUG_LEVEL DEBUG_ERROR
#endif
#include "Debug.h"

#include "HMTLPrograms.h"
#include "ProgramManager.h"

#ifndef OBJECT_TYPE
  #define OBJECT_TYPE 0
#endif

/*******************************************************************************
 * Program tracking, configuration, etc
 */

ProgramManager::ProgramManager() {
};

ProgramManager::ProgramManager(output_hdr_t **_outputs,
                               program_tracker_t **_trackers,
                               void **_objects,
                               byte _num_outputs,

                               hmtl_program_t *_functions,
                               byte _num_programs) {
  outputs = _outputs;
  trackers = _trackers;
  objects = _objects;
  num_outputs = _num_outputs;

  functions = _functions;
  num_programs = _num_programs;

  for (byte i = 0; i < num_outputs; i++) {
    trackers[i] = NULL;
  }

  DEBUG3_VALUE("ProgramManager: outputs:", num_outputs);
  DEBUG3_VALUELN(" programs:", num_programs);
}

/*
 * Lookup a program in the manager based on its ID
 */
hmtl_program_t *ProgramManager::lookup_function(byte id) {
  /* Find the program to be executed */
  for (byte i = 0; i < num_programs; i++) {
    if (functions[i].type == id) {
      return &functions[i];
    }
  }
  return NULL;
}

/*
 * Process a program configuration message
 */
boolean ProgramManager::handle_msg(msg_program_t *msg) {
  DEBUG4_VALUE("handle_msg: program=", msg->type);
  DEBUG4_VALUELN(" output=", msg->hdr.output);

  /* Find the program to be executed */
  hmtl_program_t *program = lookup_function(msg->type);
  if (program == NULL) {
    DEBUG1_VALUELN("handle_msg: invalid type: ",
		  msg->type);
    return false;
  }

   /* Setup the tracker */
  if (msg->hdr.output > num_outputs) {
    DEBUG1_VALUELN("handle_msg: invalid output: ",
		  msg->hdr.output);
    return false;
  }
  if (outputs[msg->hdr.output] == NULL) {
    DEBUG1_VALUELN("handle_msg: NULL output: ",
		  msg->hdr.output);
    return false;
  }

  program_tracker_t *tracker = trackers[msg->hdr.output];

  if (program->type == HMTL_PROGRAM_NONE) {
    /* This is a message to clear the existing program so free the tracker */
    free_tracker(msg->hdr.output);
    return true;
  }

  if (tracker != NULL) {
    DEBUG5_PRINTLN("handle_msg: reusing old tracker");
    if (tracker->state) {
      DEBUG5_PRINTLN("handle_msg: deleting old state");
      free(tracker->state);
    }
  } else {
    tracker = (program_tracker_t *)malloc(sizeof (program_tracker_t));
    trackers[msg->hdr.output] = tracker;
  }

  tracker->program = program;
  tracker->flags = 0x0;
  tracker->program->setup(msg, tracker);

  return true;
}

/*
 * Free a single program tracker
 */
void ProgramManager::free_tracker(int index) {
  program_tracker_t *tracker = trackers[index];
  if (tracker != NULL) {
      DEBUG3_VALUELN("free_tracker: clearing program for ",
		    index);
      if (tracker->state) free(tracker->state);
      free(tracker);
    }
    trackers[index] = NULL;
}

/*
 * Execute all configured program functions
 */
boolean ProgramManager::run() {
  boolean updated = false;

  for (byte i = 0; i < num_outputs; i++) {
    program_tracker_t *tracker = trackers[i];
    if (tracker != NULL) {

      if (tracker->flags & PROGRAM_TRACKER_DONE) {
        /* If this program has been set as done then free its tracker */
        free_tracker(i);
        continue;
      }

      if (tracker->program->program(outputs[i], objects[i], tracker)) {
        updated = true;
      }
    }
  }

  return updated;
}

/*
 * Run a single program without an object or tracker.  This is used for
 * providing custom sensor handlers (PROGRAM_SENSOR_DATA) and similar
 * external functions.
 */
boolean ProgramManager::run_program(byte type, void *arg) {
  hmtl_program_t *program = lookup_function(type);

  program->program(NULL, arg, NULL);

  return false;
}


/*******************************************************************************
 * XXX - This shouuld probably be moved into is own set of files
 */

MessageHandler::MessageHandler() {
  address = SOCKET_ADDR_INVALID;
}

MessageHandler::MessageHandler(socket_addr_t _address, ProgramManager *_manager,
                               Socket *_sockets[], uint8_t _num_sockets) {
  address = _address;
  manager = _manager;
  sockets = _sockets;
  num_sockets = _num_sockets;

  serial_msg_offset = 0;
  last_serial_ms = 0;
  last_ready_ms = 0;
}

/*
 * Check is a serial-ready messages should be sent over the serial port
 */
void MessageHandler::serial_ready() {
  unsigned long now = time.ms();

  if ((now - last_serial_ms > READY_THRESHOLD) &&
      (now - last_ready_ms > READY_RESEND_PERIOD)) {
    /*
     * If the module has never received a message (last_serial_ms == 0) or it has
     * been a long time since the last message, intermittently resend the 'ready'
     * message.  This allows for connection methods that may miss the first
     * 'ready' to catch this one (such as Bluetooth or boards that don't reset
     * when a serial connection is opened).
     */
    Serial.println(F(HMTL_READY));
    last_ready_ms = now;
  }
}

/* Process a message if it is for this module */
boolean MessageHandler::process_msg(msg_hdr_t *msg_hdr, Socket *src,
                                    Socket *serial_socket,
                                    config_hdr_t *config) {
  if (msg_hdr->version != HMTL_MSG_VERSION) {
    DEBUG_ERR("Invalid message version");
    return false;
  }

  /* Test if the message is for this device */
  if ((msg_hdr->address == address) ||
      (msg_hdr->address == SOCKET_ADDR_ANY)) {

    if ((msg_hdr->flags & MSG_FLAG_ACK) &&
        (msg_hdr->address != SOCKET_ADDR_ANY)) {
      /*
       * This is an ack message that is not for us, resend it over serial in
       * case that was the original source.
       * TODO: Maybe this should check address as well, and serial needs to be
       * assigned an address?
       */
      DEBUG4_PRINTLN("Forwarding ack to serial");
      Serial.write((byte *)msg_hdr, msg_hdr->length);

      if (msg_hdr->type != MSG_TYPE_SENSOR) { // Sensor broadcasts are for everyone
        return false;
      }
    }

    switch (msg_hdr->type) {

      case MSG_TYPE_OUTPUT: {
        output_hdr_t *out_hdr = (output_hdr_t *)(msg_hdr + 1);
        if (out_hdr->type == HMTL_OUTPUT_PROGRAM) {
          manager->handle_msg((msg_program_t *)out_hdr);
        } else {
          hmtl_handle_output_msg(msg_hdr, manager->num_outputs,
                                 manager->outputs, manager->objects);
        }

        return true;
      }

      case MSG_TYPE_POLL: {
        // Generate a response to a poll message
        uint16_t source_address = 0;
        Socket *sock;

        if (src != NULL) {
          // The response will be going over a socket, get the source address
          source_address = src->sourceFromData(msg_hdr);
          sock = src;
        } else {
          // The data will be sent back to the indicated Serial device.  A
          // socket still needs to be specified in order to have a buffer to
          // fill.
          sock = serial_socket;
        }

        DEBUG3_VALUELN("Poll req src:", source_address);

        // Format the poll response
        uint16_t len = hmtl_poll_fmt(sock->send_buffer,
                                     sock->send_data_size,
                                     source_address,
                                     msg_hdr->flags, OBJECT_TYPE,
                                     config,
                                     manager->outputs, 
                                     sock->recvLimit);

        // Respond to the appropriate source
        if (src != NULL) {
          if (msg_hdr->address == SOCKET_ADDR_ANY) {
            // If this was a broadcast address then do not respond immediately,
            // delay for time based on our address.
            int delayMs = address * 2;
            DEBUG3_VALUELN("Delay resp: ", delayMs)
              delay(delayMs);
          }

          src->sendMsgTo(source_address, sock->send_buffer, len);
        } else {
          Serial.write(sock->send_buffer, len);
        }

        break;
      }

      case MSG_TYPE_SET_ADDR: {
        /* Handle an address change message */
        msg_set_addr_t *set_addr = (msg_set_addr_t *)(msg_hdr + 1);
        if ((set_addr->device_id == 0) ||
            (set_addr->device_id == config->device_id)) {
          address = set_addr->address;
          src->sourceAddress = address;
          DEBUG2_VALUELN("Address changed to ", address);
        }
        break;
      }

      case MSG_TYPE_SENSOR: {
        if (msg_hdr->flags & MSG_FLAG_ACK) {
          /*
           * This is a sensor response, record relevant values for usage
           * elsewhere.
           */
          msg_sensor_data_t *sensor = NULL;
          while ((sensor = hmtl_next_sensor(msg_hdr, sensor))) {
            // Call the ProgramManager's handler for the sensor function
            manager->run_program(PROGRAM_SENSOR_DATA, sensor);
          }
          DEBUG_PRINT_END();
        }
        break;
      }
    }
  }

  return false;
}

/*
 * Check for messages over the Serial port.  If a message is received,
 * forward it over other sockets if it isn't for this device or is a broacast
 * message, and then process the message if it is for this device.
 *
 * Returns true if processing the message resulted in some change that may
 * require the device's outputs to be updated.
 */
boolean MessageHandler::check_serial(config_hdr_t *config) {
  boolean update = false;

  /* Check for messages on the serial interface */
  msg_hdr_t *msg_hdr = (msg_hdr_t *)serial_msg;
  if (hmtl_serial_getmsg(serial_msg, MSG_MAX_SZ, &serial_msg_offset)) {
    /* Received a complete message */
    DEBUG5_VALUE("Received msg len=", serial_offset);
    DEBUG5_PRINT(" ");
    DEBUG5_COMMAND(
            print_hex_string(msg, serial_offset)
    );
    DEBUG_PRINT_END();
    Serial.println(F(HMTL_ACK));

    /* Check if the message should be forwarded to any sockets */
    for (uint8_t i = 0; i < num_sockets; i++) {
      if (sockets[i] != NULL) {
         check_and_forward(msg_hdr, sockets[i]);
      }
    }

    if (process_msg(msg_hdr, NULL, sockets[0], config)) {
      update = true;
    }

    serial_msg_offset = 0;
    last_serial_ms = time.ms();
  }

  return update;
}

/*
 * Check for messages over the indicated socket and handle any messages
 * received.
 *
 * Returns true if processing the message resulted in some change that may
 * require the device's outputs to be updated.
 */
boolean MessageHandler::check_socket(Socket *socket, Socket *serial_socket,
                                     config_hdr_t *config) {
  unsigned int msglen;
  msg_hdr_t *msg_hdr = hmtl_socket_getmsg(socket, &msglen);
  if (msg_hdr != NULL) {
    DEBUG5_VALUE("Received rs485 msg len=", msglen);
    DEBUG5_PRINT(" ");
    DEBUG5_COMMAND(
            print_hex_string((byte *)msg_hdr, msglen)
    );
    DEBUG_PRINT_END();

    if (process_msg(msg_hdr, socket, serial_socket, config)) {
      return true;
    }
  }

  return false;
}

/*
 * Check the serial device and all sockets for messsages
 */
boolean MessageHandler::check(config_hdr_t *config) {
  boolean update = false;

  if (check_serial(config)) {
    update = true;
  }

  for (uint8_t socket = 0; socket < num_sockets; socket++) {
    if ((sockets[socket] != NULL) &&
            (check_socket(sockets[socket], sockets[socket], config))) {
      update = true;
    }
  }

  return update;
}

/*
 * Check if a message should be forwarded and transmit it over
 * the indicated socket if so.
 */
boolean MessageHandler::check_and_forward(msg_hdr_t *msg_hdr, Socket *socket) {
  if ((msg_hdr->address != address) ||
      (msg_hdr->address == SOCKET_ADDR_ANY)) {
    /*
     * Messages that are not to this module's address or are on the broadcast
     * address should be forwarded.
     */
    if (msg_hdr->length > socket->send_data_size) {
      DEBUG1_VALUELN("Message larger than send buffer:", msg_hdr->length);
    } else {
      DEBUG4_HEXVALLN("Forwarding serial msg to ", msg_hdr->address);
      memcpy(socket->send_buffer, msg_hdr, msg_hdr->length);
      socket->sendMsgTo(msg_hdr->address, socket->send_buffer, msg_hdr->length);
      return true;
    }
  }

  // The message was not forwarded over the socket
  return false;
}


