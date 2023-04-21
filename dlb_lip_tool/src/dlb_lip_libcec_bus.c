/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                Copyright (C) 2019 by Dolby International AB.
 *                            All rights reserved.
 ******************************************************************************/

/**
 *  @file       dlb_lip_cec_bus.c
 *  @brief      LIP cec bus
 *
 */

#include "dlb_lip_libcec_bus.h"
#include "dlb_lip.h"

#include <assert.h>
#include <ceccloader.h>
#include <inttypes.h>

struct dlb_cec_bus_handle_s
{
    printf_callback_t           pritnf_func;
    void *                      printf_arg;
    ICECCallbacks               libcec_callbacks;
    libcec_interface_t          libcec_interface;
    dlb_cec_bus_t               cec_bus;
    message_received_callback_t callback;
    void *                      callback_arg;
    bool                        sim_arc;
    bool                        arc_initiated;
};

static dlb_cec_bus_handle_t cec_bus_handle;

#define FATAL_ERROR(str)                                                         \
    do                                                                           \
    {                                                                            \
        printf("%s:%s:%d FATAL_ERROR: %s\n", __FILE__, __func__, __LINE__, str); \
        assert(!str);                                                            \
        abort();                                                                 \
    } while (0)

#define lip_libcec_log_message(bus_handle, ...) lip_libcec_log_message_internal(bus_handle, __VA_ARGS__)
static void lip_libcec_log_message_internal(dlb_cec_bus_handle_t *const bus_handle, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    if (bus_handle->pritnf_func)
    {
        bus_handle->pritnf_func(bus_handle->printf_arg, format, args);
    }
    else
    {
        vprintf(format, args);
    }

    va_end(args);
}

static void cb_cec_log_message(void *cb_param, const cec_log_message *message)
{
    dlb_cec_bus_handle_t *bus_handle = (dlb_cec_bus_handle_t *)cb_param;
    assert(cb_param);
    assert(message);

    if (NULL != message)
    {
        const char *strLevel;
        assert(message->message);
        switch (message->level)
        {
        case CEC_LOG_ERROR:
            strLevel = "ERROR:   ";
            break;
        case CEC_LOG_WARNING:
            strLevel = "WARNING: ";
            break;
        case CEC_LOG_NOTICE:
            strLevel = "NOTICE:  ";
            break;
        case CEC_LOG_TRAFFIC:
            strLevel = "TRAFFIC: ";
            break;
        case CEC_LOG_DEBUG:
            strLevel = "DEBUG:   ";
            break;
        default:
            strLevel = "";
            FATAL_ERROR("Unknown CEC_LOG_LEVEL");
            break;
        }

        if (NULL != message->message)
        {
            lip_libcec_log_message(bus_handle, "%s[%" PRId64 "]\t%s\n", strLevel, message->time, message->message);
        }
    }
}

static void insert_sad_into_command(unsigned int codec_id, cec_command *const command)
{
    switch (codec_id)
    {
    case 0x1: // PCM
    {
        command->parameters.data[command->parameters.size]     = 0x90;
        command->parameters.data[command->parameters.size + 1] = 0x7F;
        command->parameters.data[command->parameters.size + 2] = 0x07;
        command->parameters.size += 3;
        break;
    }
    case 0x2: // DD
    {
        command->parameters.data[command->parameters.size]     = 0x15;
        command->parameters.data[command->parameters.size + 1] = 0x07;
        command->parameters.data[command->parameters.size + 2] = 0x50;
        command->parameters.size += 3;
        break;
    }
    case 0xA: // DD+
    {
        command->parameters.data[command->parameters.size]     = 0x57;
        command->parameters.data[command->parameters.size + 1] = 0x04;
        command->parameters.data[command->parameters.size + 2] = 0x01;
        command->parameters.size += 3;
        break;
    }
    default:
        break;
    }
}

static void send_arc_initiate(dlb_cec_bus_handle_t *bus_handle)
{
    cec_command reply      = { 0 };
    reply.ack              = 1;
    reply.destination      = CECDEVICE_TV;
    reply.initiator        = (cec_logical_address)bus_handle->cec_bus.logical_address;
    reply.eom              = 0;
    reply.opcode_set       = 1;
    reply.transmit_timeout = 1000;
    reply.opcode           = CEC_OPCODE_START_ARC;
    reply.parameters.size  = 0;

    bus_handle->libcec_interface.transmit(bus_handle->libcec_interface.connection, &reply);
}

static void send_arc_terminate(dlb_cec_bus_handle_t *bus_handle)
{
    cec_command reply         = { 0 };
    reply.ack                 = 1;
    reply.destination         = CECDEVICE_TV;
    reply.initiator           = (cec_logical_address)bus_handle->cec_bus.logical_address;
    reply.eom                 = 0;
    reply.opcode_set          = 1;
    reply.transmit_timeout    = 1000;
    reply.opcode              = CEC_OPCODE_END_ARC;
    reply.parameters.size     = 0;
    bus_handle->arc_initiated = false;

    bus_handle->libcec_interface.transmit(bus_handle->libcec_interface.connection, &reply);
}

static void cb_cec_cmd_received(void *cb_param, const cec_command *const command)
{
    dlb_cec_bus_handle_t *bus_handle      = (dlb_cec_bus_handle_t *)cb_param;
    dlb_cec_message_t     dlb_message     = { 0 };
    bool                  message_handled = false;

    dlb_message.msg_length  = command->parameters.size;
    dlb_message.destination = (dlb_cec_logical_address_t)command->destination;
    dlb_message.initiator   = (dlb_cec_logical_address_t)command->initiator;
    dlb_message.opcode      = (dlb_cec_opcode_t)command->opcode;
    memcpy(dlb_message.data, command->parameters.data, command->parameters.size);

    if (bus_handle->sim_arc)
    {
        bool        transmit   = false;
        cec_command reply      = { 0 };
        reply.ack              = 1;
        reply.destination      = command->initiator;
        reply.initiator        = (cec_logical_address)bus_handle->cec_bus.logical_address;
        reply.eom              = 0;
        reply.opcode_set       = 1;
        reply.transmit_timeout = 1000;

        switch (command->opcode)
        {
        // REQUEST ARC INITIATION
        case CEC_OPCODE_REQUEST_ARC_START:
        {
            printf("Got CEC_OPCODE_REQUEST_ARC_START\n");
            message_handled       = true;
            transmit              = true;
            reply.opcode          = CEC_OPCODE_START_ARC;
            reply.parameters.size = 0;
            break;
        }
        case CEC_OPCODE_REQUEST_ARC_END:
        {
            printf("Got CEC_OPCODE_REQUEST_ARC_END\n");
            message_handled       = true;
            transmit              = true;
            reply.opcode          = CEC_OPCODE_END_ARC;
            reply.parameters.size = 0;
            break;
        }
        case CEC_OPCODE_REPORT_ARC_STARTED:
        {
            printf("Got CEC_OPCODE_REPORT_ARC_STARTED\n");
            message_handled = true;
            // Do nothing
            bus_handle->arc_initiated = true;

            break;
        }
        case CEC_OPCODE_REPORT_ARC_ENDED:
        {
            printf("Got CEC_OPCODE_REPORT_ARC_ENDED\n");
            message_handled           = true;
            bus_handle->arc_initiated = false;
            // Do nothing
            break;
        }
        case CEC_OPCODE_REQUEST_SAD:
        {
            printf("Got CEC_OPCODE_REQUEST_SAD\n");
            message_handled       = true;
            transmit              = true;
            reply.opcode          = CEC_OPCODE_REPORT_SAD;
            reply.parameters.size = 0;
            for (unsigned int i = 0; i < command->parameters.size; i += 1)
            {
                insert_sad_into_command(command->parameters.data[i], &reply);
            }
        }
        default:
            break;
        }
        if (transmit)
        {
            bus_handle->libcec_interface.transmit(bus_handle->libcec_interface.connection, &reply);
        }
    }

    if (!message_handled && bus_handle->callback)
    {
        bus_handle->callback(bus_handle->callback_arg, &dlb_message);
    }
}

static void dlb_libcec_bus_register_callback(dlb_cec_bus_handle_t *const bus_handle, message_received_callback_t func, void *arg)
{
    bus_handle->callback     = func;
    bus_handle->callback_arg = arg;
}

static int dlb_libcec_bus_transmit(dlb_cec_bus_handle_t *bus_handle, const dlb_cec_message_t *const dlb_message)
{
    cec_command command;

    command.parameters.size  = dlb_message->msg_length;
    command.destination      = (cec_logical_address)dlb_message->destination;
    command.initiator        = (cec_logical_address)dlb_message->initiator;
    command.opcode           = (cec_opcode)dlb_message->opcode;
    command.opcode_set       = dlb_message->opcode != DLB_CEC_OPCODE_NONE;
    command.eom              = 0;
    command.ack              = 1;
    command.transmit_timeout = CEC_DEFAULT_TRANSMIT_TIMEOUT;

    memcpy(command.parameters.data, dlb_message->data, dlb_message->msg_length);

    lip_libcec_log_message(
        bus_handle,
        "transmitting from: %d to %d, size: %d, opcode: 0x%x\n",
        command.initiator,
        command.destination,
        command.parameters.size,
        command.opcode);
    return bus_handle->libcec_interface.transmit(bus_handle->libcec_interface.connection, &command) == 1 ? 0 : 1;
}

dlb_cec_bus_t *dlb_cec_bus_init(
    const uint16_t        physical_address,
    const char *          port_name,
    dlb_lip_device_type_t device_type,
    printf_callback_t     func,
    void *                arg,
    bool                  sim_arc)
{
    libcec_configuration libcec_config;
    char                 buffer[100];
    char                 strPort[50];

    cec_bus_handle.pritnf_func   = func;
    cec_bus_handle.printf_arg    = arg;
    cec_bus_handle.sim_arc       = sim_arc;
    cec_bus_handle.arc_initiated = false;

    // loader API call #1
    libcecc_reset_configuration(&libcec_config);

    /* configure callbacks */
    libcec_config.callbacks                  = &cec_bus_handle.libcec_callbacks;
    libcec_config.callbacks->logMessage      = &cb_cec_log_message;
    libcec_config.callbacks->commandReceived = &cb_cec_cmd_received;
    libcec_config.callbackParam              = &cec_bus_handle;

    /* don't make this device as an active source on the startup */
    libcec_config.bActivateSource = 0;

    libcec_config.clientVersion = LIBCEC_VERSION_CURRENT;

    /* set the device OSD name */
    snprintf(libcec_config.strDeviceName, sizeof(libcec_config.strDeviceName), "LIP");

    /* set the logical type of the device */
    switch (device_type)
    {
    case LIP_DEVICE_STB:
        libcec_config.deviceTypes.types[0] = CEC_DEVICE_TYPE_PLAYBACK_DEVICE;
        break;

    case LIP_DEVICE_AVR:
        libcec_config.deviceTypes.types[0] = CEC_DEVICE_TYPE_AUDIO_SYSTEM;
        break;

    case LIP_DEVICE_TV:
        libcec_config.deviceTypes.types[0] = CEC_DEVICE_TYPE_TV;
        libcec_config.tvVendor             = CEC_VENDOR_DOLBY;
        break;
    default:
        FATAL_ERROR("Invalid device type!");
        break;
    }

    if (device_type != LIP_DEVICE_AVR && sim_arc)
    {
        FATAL_ERROR("ONLY AVR can act as ARC receiver");
    }

    /* set the physical address of the device */
    libcec_config.iPhysicalAddress = physical_address;

    // loader API call #1
    if (libcecc_initialise(&libcec_config, &cec_bus_handle.libcec_interface, NULL) != 1)
    {
        lip_libcec_log_message(&cec_bus_handle, "can't initialise libCEC\n");
        return NULL;
    }

    // lib API call #2
    cec_bus_handle.libcec_interface.version_to_string(libcec_config.serverVersion, buffer, sizeof(buffer));
    lip_libcec_log_message(&cec_bus_handle, "CEC Parser created - libCEC version %s\n", buffer);

    if (port_name == NULL || port_name[0] == '\0')
    {
        /* discover devices on the serial COM ports #fixme - add commandline parameter to specify port wanted by the user */
        // lib API call #3
        cec_adapter devices[4];
        int8_t      iDevicesFound = cec_bus_handle.libcec_interface.find_adapters(
            cec_bus_handle.libcec_interface.connection, devices, sizeof(devices) / sizeof(devices[0]), NULL);
        if (iDevicesFound <= 0)
        {
            lip_libcec_log_message(&cec_bus_handle, "FAILED to find the adapters\n");
            libcecc_destroy(&cec_bus_handle.libcec_interface);
            return NULL;
        }
        else
        {
            lip_libcec_log_message(&cec_bus_handle, "\n path:     %s\n com port: %s\n\n", devices[0].path, devices[0].comm);
            strcpy(strPort, devices[0].comm);
        }
    }
    else
    {
        lip_libcec_log_message(&cec_bus_handle, "\n com port: %s\n\n", port_name);
        strcpy(strPort, port_name);
    }
    lip_libcec_log_message(&cec_bus_handle, "opening a connection to the CEC adapter...\n");

    // lib API call #4
    if (!cec_bus_handle.libcec_interface.open(cec_bus_handle.libcec_interface.connection, strPort, 5000))
    {
        lip_libcec_log_message(&cec_bus_handle, "unable to open the device on port %s\n", strPort);
        libcecc_destroy(&cec_bus_handle.libcec_interface);
        return NULL;
    }

    cec_bus_handle.cec_bus.handle            = &cec_bus_handle;
    cec_bus_handle.cec_bus.transmit_callback = dlb_libcec_bus_transmit;
    cec_bus_handle.cec_bus.register_callback = dlb_libcec_bus_register_callback;
    // lib API call #5
    cec_bus_handle.cec_bus.logical_address = (dlb_cec_logical_address_t)cec_bus_handle.libcec_interface
                                                 .get_logical_addresses(cec_bus_handle.libcec_interface.connection)
                                                 .primary;

    if (cec_bus_handle.sim_arc)
    {
        if (cec_bus_handle.libcec_interface.poll_device(cec_bus_handle.libcec_interface.connection, CECDEVICE_TV))
        {
            send_arc_initiate(&cec_bus_handle);
        }
    }
    return &cec_bus_handle.cec_bus;
}

void dlb_cec_bus_destroy(void)
{
    if (cec_bus_handle.arc_initiated)
    {
        send_arc_terminate(&cec_bus_handle);
    }
    libcecc_destroy(&cec_bus_handle.libcec_interface);
}

int dlb_cec_poll_device(cec_logical_address downstream_device)
{
    return cec_bus_handle.libcec_interface.poll_device(cec_bus_handle.libcec_interface.connection, downstream_device);
}
