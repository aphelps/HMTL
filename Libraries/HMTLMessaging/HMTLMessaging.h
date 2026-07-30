/*******************************************************************************
 * Author: Adam Phelps
 * License: MIT
 * Copyright: 2014
 *
 * This library handles communications to and between HMTL Modules
 ******************************************************************************/

#ifndef HMTLMESSAGING_H
#define HMTLMESSAGING_H

#include "Socket.h"
#include "RS485Utils.h"
#include "HMTLTypes.h"

/* msg_hdr_t, the MSG_TYPE_* / MSG_FLAG_* codes, the msg_* payload structs and
 * the HMTL_MSG_* length macros. Moved out of this header so that consumers
 * which only need the wire format do not have to pull in RS485Utils.h (and
 * through it Arduino.h) via this file. */
#include "HMTLWireFormat.h"

// Uncomment this line to enable CRC checking of messages
//#define HMTL_USE_CRC

/*******************************************************************************
 * Utility functions
 */
uint16_t hmtl_msg_size(output_hdr_t *output);

/* Process a HMTL formatted message */
int hmtl_handle_output_msg(msg_hdr_t *msg_hdr,
                           byte num_objects,
                           output_hdr_t *outputs[],
                           void *objects[] = NULL);

/* Receive a message over the serial interface */
boolean hmtl_serial_getmsg(byte *msg, byte msg_len, byte *offset_ptr);

/* Receive a message over the socket interface */
msg_hdr_t *hmtl_socket_getmsg(Socket *socket, unsigned int *msglen,
                             socket_addr_t address = SOCKET_ADDR_INVALID);


/*******************************************************************************
 * Formatting for individual messages
 */
void hmtl_msg_fmt(msg_hdr_t *msg_hdr, socket_addr_t address, uint8_t length,
                  uint8_t type, uint8_t flags = 0);

uint16_t hmtl_value_fmt(byte *buffer, uint16_t buffsize,
			socket_addr_t address, uint8_t output, int value);
uint16_t hmtl_rgb_fmt(byte *buffer, uint16_t buffsize,
                      socket_addr_t address, uint8_t output,
                      uint8_t r, uint8_t g, uint8_t b);
uint16_t hmtl_poll_fmt(byte *buffer, uint16_t buffsize, socket_addr_t address,
                       byte flags, uint16_t object_type,
                       config_hdr_t *config, output_hdr_t *outputs[],
                       uint16_t recv_buffer_size);
uint16_t hmtl_set_addr_fmt(byte *buffer, uint16_t buffsize,
                           socket_addr_t address,
                           uint16_t device_id, socket_addr_t new_address);
uint16_t hmtl_dumpconfig_fmt(byte *buffer, uint16_t buffsize, uint16_t address,
                             byte flags,
                             byte datalen);
uint16_t hmtl_sensor_fmt(byte *buffer, uint16_t buffsize, socket_addr_t address,
                         uint8_t datalen, uint8_t **data_ptr);

/*******************************************************************************
 * Wrapper functions for sending HMTL Messages 
 */
void hmtl_send_value(Socket *socket, byte *buff, byte buff_len,
                     socket_addr_t address, uint8_t output, int value);

void hmtl_send_rgb(Socket *socket, byte *buff, byte buff_len,
                   socket_addr_t address, uint8_t output,
                   uint8_t r, uint8_t g, uint8_t b);

void hmtl_send_poll_request(Socket *socket, byte *buff, byte buff_len,
                            socket_addr_t address);

void hmtl_send_sensor_request(Socket *socket, byte *buff, byte buff_len,
                              socket_addr_t address);

/*******************************************************************************
 * Data processing helper functions
 */
msg_sensor_data_t* hmtl_next_sensor(msg_hdr_t *msg, msg_sensor_data_t *current);

#endif
