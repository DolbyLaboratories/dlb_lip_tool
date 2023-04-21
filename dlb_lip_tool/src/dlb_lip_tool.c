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
 *  @file       dlb_lip_tool.c
 *  @brief      todo
 *
 *  Todo
 */

/* dlb_lip include */
#include "dlb_lip.h"
#include "dlb_lip_osa.h" // For Timer reuse only

/* General includes needed for binary */
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dlb_lip_libcec_bus.h"
#include "dlb_lip_tool.h"
#include "dlb_lip_xml_parser.h"

#if defined(_MSC_VER)
#include <Windows.h>
#include <fileapi.h>
#include <io.h>
static void usleep(__int64 usec)
{
    HANDLE        timer;
    LARGE_INTEGER ft;
    ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time
    timer       = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}
#else
#include <unistd.h>
#define MAX_PATH 1024
#endif

const char dolby_copyright[] = "\nUnpublished work.  Copyright 2019 Dolby Laboratories, Inc. and"
                               "\nDolby Laboratories Licensing Corporation.  All Rights Reserved.\n\n"
                               "USE OF THIS SOFTWARE IS SUBJECT TO A LEGAL AGREEMENT BETWEEN YOU AND DOLBY\n"
                               "LABORATORIES. DO NOT USE THIS SOFTWARE UNLESS YOU AGREE TO THE TERMS AND \n"
                               "CONDITIONS IN THE AGREEMENT.  BY USING THIS SOFTWARE, YOU ACKNOWLEDGE THAT \n"
                               "YOU HAVE READ THE AGREEMENT AND THAT YOU AGREE TO BE BOUND BY ITS TERMS. \n\n";

#define LIP_UUID_SIZE 2
#define COMMAND_BUFFER_SIZE 128
static long long     WAIT_TIME_MS = 1000; // Default wait time between commands
static FILE *        log_file     = NULL;
dlb_lip_xml_parser_t xml_parser;

// For on_update_uuid
static bool                   on_update_uuid_av_formats_valid = false;
static dlb_lip_video_format_t on_update_uuid_v_format         = { 0 };
static dlb_lip_audio_format_t on_update_uuid_a_format         = { 0 };
static bool                   uuid_valid                      = false;
static uint32_t               downstream_uuid;
static dlb_lip_osa_timer_t    on_update_uuid_timer = { 0 };

/**
 *  Lite command line parser struct type
 */
struct cmdline_options_t
{
    char commands_file_name[MAX_PATH];
    char config_file_name[MAX_PATH];
    char log_file_name[MAX_PATH];
    char port_name[MAX_PATH];
    char state_file_name[MAX_PATH];
    bool cache_enabled;
    bool sim_arc;
};

typedef struct cmdline_options_t cmdline_options; ///< typedef for structure cmdline_options_t type

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef enum dlb_lip_tool_status_e
{
    LIP_TOOL_INVALID,
    LIP_TOOL_INIT,
    LIP_TOOL_WAITING_FOR_DATA,
    LIP_TOOL_PROCESSSING,
    LIP_TOOL_QUIT
} dlb_lip_tool_status_t;

static const char *lip_tool_state_description(dlb_lip_tool_status_t state)
{
    const char *str = NULL;
    switch (state)
    {
    case LIP_TOOL_INVALID:
        str = "INVALID";
        break;
    case LIP_TOOL_INIT:
        str = "INIT";
        break;
    case LIP_TOOL_WAITING_FOR_DATA:
        str = "WAITING_FOR_DATA";
        break;
    case LIP_TOOL_PROCESSSING:
        str = "PROCESSING";
        break;
    case LIP_TOOL_QUIT:
        str = "QUIT";
        break;
    default:
        assert(!"Invalid lip_state");
        str = "Invalid lip_state";
        break;
    }
    return str;
}

static dlb_lip_tool_status_t lip_tool_state = LIP_TOOL_INVALID;

static void update_lip_tool_state(const char *filename, dlb_lip_tool_status_t new_state)
{
    if (lip_tool_state != new_state)
    {
        FILE *file = NULL;

        file = fopen(filename, lip_tool_state == LIP_TOOL_INVALID ? "wt+" : "at+");
        if (file)
        {
            fprintf(file, "%s\n", lip_tool_state_description(new_state));
            fclose(file);
        }
        else if (filename[0])
        {
            fprintf(stderr, "Failed to open state file(%s)\n", filename);
        }
        lip_tool_state = new_state;
    }
}

/**********************************************************************
 *
 *
 *  Function declarations
 *
 *
 **********************************************************************/

/**********************************************************************
 *
 *  Utils
 *
 **********************************************************************/

void usage(char **const argv);

void increase_count(int *count, const int argc, char **const argv);

void parse_cmdline(int argc, char **argv, cmdline_options *const opt);

char *concat_text(char *dst, char *end, const char *src);

char *t_itoa(char *dst, const int src);

/***********************************************************************
 *
 *  Main testing routine
 *
 ***********************************************************************/

void test_lip_tool(cmdline_options *const opt);

/***********************************************************************
 *
 *
 *  Function definitions
 *
 *
 ***********************************************************************/

/*!
Checks whether the counter of the command line arguments
has exceeded the number of arguments available. If true
it exits with failure, and if not increases the count by 1.

@param count    - pointer to current argument count
@param argc     - number of arguments
@param argv     - array of strings containing command line arguments

@return void
*/
void increase_count(int *count, const int argc, char **const argv)
{
    if (((*count) + 1) >= argc)
    {
        /* needs to have the option following */
        usage(argv);
        /* exit */
        exit(EXIT_FAILURE);
    }
    (*count)++;
}

/*!
Lite command line parser. Parses the command line of the binary call.

@param  argc    - count of command line arguments
@param  argv    - array of C-strings command line arguments
@param  opt     - pointer to the command line parser structure type to store parsed content

@return void    On error prints out the help menu and an error message and exits.
*/
void parse_cmdline(int argc, char **argv, cmdline_options *const opt)
{
    int  count = 1, len, flag = 0;
    char ch;

    memset(opt->log_file_name, '\0', sizeof(opt->log_file_name));
    memset(opt->commands_file_name, '\0', sizeof(opt->commands_file_name));
    memset(opt->port_name, '\0', sizeof(opt->port_name));
    memset(opt->state_file_name, '\0', sizeof(opt->state_file_name));
    opt->cache_enabled = true;
    opt->sim_arc       = false;

    if (argc == 1)
    {
        /* print usage if no options are given */
        usage(argv);
        /* and exit */
        exit(EXIT_FAILURE);
    }

    /* parse command line arguments */
    while (count < argc)
    {
        len = strlen(argv[count]);
        if (len != 2)
        {
            /* if length is not 2, then there is an error as we do not
            have a flag for sure */
            usage(argv);
            /* and exit */
            exit(EXIT_FAILURE);
        }
        ch = argv[count][0];
        /* check if command line parameter exists */
        if (ch != '-')
        {
            /* if no switch is present just print the usage */
            usage(argv);
            /* and exit */
            exit(EXIT_FAILURE);
        }
        /* get the flag */
        ch = argv[count][1];
        switch (ch)
        {
        case 'a':
        {
            opt->sim_arc = true;
            break;
        }
        case 'c':
        {
            increase_count(&count, argc, argv);

            if (strlen(argv[count]) >= MAX_PATH)
            {
                fprintf(stderr, "ERROR: Path to the commands file is too long.\n");
                assert(strlen(argv[count]) < MAX_PATH);
                exit(EXIT_FAILURE);
            }

            snprintf(opt->commands_file_name, sizeof(opt->commands_file_name), "%s", argv[count]);
            break;
        }
        case 'f':
        {
            increase_count(&count, argc, argv);

            if (strlen(argv[count]) >= MAX_PATH)
            {
                fprintf(stderr, "ERROR: Path to the log file is too long.\n");
                assert(strlen(argv[count]) < MAX_PATH);
                exit(EXIT_FAILURE);
            }

            snprintf(opt->log_file_name, sizeof(opt->log_file_name), "%s", argv[count]);
            break;
        }
        case 'n':
        {
            opt->cache_enabled = false;
            break;
        }
        case 'p':
        {
            increase_count(&count, argc, argv);

            if (strlen(argv[count]) >= MAX_PATH)
            {
                fprintf(stderr, "ERROR: Port name is too long.\n");
                assert(strlen(argv[count]) < MAX_PATH);
                exit(EXIT_FAILURE);
            }

            snprintf(opt->port_name, sizeof(opt->port_name), "%s", argv[count]);
            break;
        }
        case 's':
        {
            increase_count(&count, argc, argv);

            if (strlen(argv[count]) >= MAX_PATH)
            {
                fprintf(stderr, "ERROR: Path to the state file is too long.\n");
                assert(strlen(argv[count]) < MAX_PATH);
                exit(EXIT_FAILURE);
            }

            snprintf(opt->state_file_name, sizeof(opt->state_file_name), "%s", argv[count]);
            break;
        }
        case 'x':
        {
            increase_count(&count, argc, argv);

            if (strlen(argv[count]) >= MAX_PATH)
            {
                fprintf(stderr, "ERROR: Path to the config XML file is too long.\n");
                assert(strlen(argv[count]) < MAX_PATH);
                exit(EXIT_FAILURE);
            }

            snprintf(opt->config_file_name, sizeof(opt->config_file_name), "%s", argv[count]);

            flag = 1;
            break;
        }
        case 'v':
        {
            fprintf(stdout, "Binary name: %s\nBuild date: %s\nBuild time: %s\n\n", argv[0], __DATE__, __TIME__);
            break;
        }
        default:
        {
            /* if none of the cases was encountered, then print usage */
            usage(argv);
            /* and exit */
            exit(EXIT_FAILURE);
            break;
        }
        }
        count++;
    }
    if (flag == 0)
    {
        fprintf(stderr, "ERROR: No xml file given.\n");
        exit(EXIT_FAILURE);
    }
}

/*!
Concatenates a C-string to another C-string. If the destination C-string
is not large enough the source C-string is truncated. The ending character
'\0' of the source C-string is stripped away at concatenation.

@param dst  - pointer to the location of the destination C-string where source
C-string is to be appended
@param end  - pointer to the end of the destination C-string
@param src  - pointer to the C-string to be concatenated

@return pointer to the current position in the destination C-string reached after
concatenation
*/
char *concat_text(char *dst, char *end, const char *src)
{
    while ((*src != '\0') && (dst < end))
    {
        *dst++ = *src++;
    }
    return dst;
}

/*!
Converts an integer number to ASCII characters. Works for both positive
and negative integers.

@param dst  - pointer to the destination string where characters are written to
@param src  - integer number to convert to ASCII

@return pointer to the beginning of the destination written C-string
*/
char *t_itoa(char *dst, const int src)
{
    char *       ptr = dst;
    unsigned int i   = 0, k;
    unsigned int n   = (src < 0) ? -src : src;

    if (n == 0)
    {
        ptr[i++] = '0';
        ptr[i]   = '\0';
        return dst;
    }

    while (n != 0)
    {
        ptr[i++] = '0' + (n % 10);
        n /= 10;
    }

    if (src < 0)
    {
        ptr[i++] = '-';
    }

    ptr[i] = '\0';

    for (k = 0; k < i / 2; k++)
    {
        ptr[k] ^= ptr[i - k - 1];
        ptr[i - k - 1] ^= ptr[k];
        ptr[k] ^= ptr[i - k - 1];
    }
    return dst;
}

void test_lip_tool(cmdline_options *const opt)
{
    (void)opt;
}

static int log_messages(void *arg, const char *format, va_list va)
{
    (void)arg;
    if (log_file)
    {
        va_list va_cpy;
        va_copy(va_cpy, va);
        vfprintf(log_file, format, va_cpy);
        fflush(log_file);
#if defined(_MSC_VER)
        _commit(_fileno(log_file));
#else
        fsync(fileno(log_file));
#endif
        va_end(va_cpy);
    }
    vprintf(format, va);

    return 0;
}
static void print_and_log_message(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_messages(NULL, format, args);
    va_end(args);
}

static bool wait_for_downstream_device(dlb_lip_t *p_dlb_lip, const unsigned int max_retry_cnt)
{
    bool         ret       = false;
    unsigned int retry_cnt = 0;
    do
    {
        dlb_lip_status_t status = dlb_lip_get_status(p_dlb_lip, true);
        if ((status.status & LIP_DOWNSTREAM_CONNECTED) == LIP_DOWNSTREAM_CONNECTED)
        {
            ret = true;
            break;
        }
        dlb_lip_set_config(p_dlb_lip, NULL, true, DLB_LOGICAL_ADDR_UNKNOWN);
        retry_cnt += 1;
    } while (retry_cnt < max_retry_cnt);

    return ret;
}

static int transmit_data(dlb_cec_bus_t *cec_bus, const unsigned char data[CEC_BUS_MAX_MSG_LENGTH], const unsigned char size)
{
    dlb_cec_message_t command = { 0 };

    command.initiator   = (dlb_cec_logical_address_t)(data[0] & 0xF0) >> 4;
    command.destination = (dlb_cec_logical_address_t)data[0] & 0x0F;

    if (size == 1)
    {
        command.opcode     = DLB_CEC_OPCODE_NONE;
        command.msg_length = 0;
    }
    else
    {
        command.opcode     = (dlb_cec_opcode_t)data[1];
        command.msg_length = size - 2;

        for (int i = 2; i < size; i++)
        {
            command.data[i - 2] = data[i];
        }
    }

    return cec_bus->transmit_callback(cec_bus->handle, &command);
}

static int process_command_tx(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    const char delim[2]         = ":";
    const char delim_command[2] = " ";
    char *     token;

    unsigned char parsed_data[64] = { 0 };
    unsigned char parsed_size     = 0;

    char data_tmp[COMMAND_BUFFER_SIZE] = { 0 };
    (void)p_dlb_lip;

    memcpy(data_tmp, data, COMMAND_BUFFER_SIZE);

    strtok(data_tmp, delim_command);

    /* drop the 'tx' part */
    token = strtok(NULL, delim_command);

    /* get first byte */
    token = strtok(token, delim);

    if (NULL == token)
    {
        print_and_log_message(">>>> specify the command\n");
        return 1;
    }

    while (NULL != token)
    {
        const char *value          = token;
        parsed_data[parsed_size++] = (unsigned char)strtoul(value, NULL, 16);
        token                      = strtok(NULL, delim);
    }

    return transmit_data(cec_bus, parsed_data, parsed_size);
}

static int process_command_quit(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    (void)p_dlb_lip;
    (void)data;
    (void)cec_bus;

    return 1;
}

static int process_command_wait_time(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    unsigned int sleep_time_ms = 0;
    int          ret           = 0;

    (void)p_dlb_lip;
    (void)cec_bus;

    if (sscanf(data, "%*s %u\n", &sleep_time_ms) == 1)
    {
        WAIT_TIME_MS = sleep_time_ms;
        ret          = 0;
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ]\n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_wait(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char               wait_arg[32]  = { 0 };
    long long          sleep_time_ms = 0;
    int                ret           = 0;
    const unsigned int MAX_WAIT_MS   = 32000;

    (void)p_dlb_lip;
    (void)cec_bus;

    if (sscanf(data, "%*s %31s\n", wait_arg) == 1)
    {
        if (strcmp(wait_arg, "downstream") == 0)
        {
            const unsigned int MAX_RETRY_CNT = 10;

            if (wait_for_downstream_device(p_dlb_lip, MAX_RETRY_CNT) == false)
            {
                print_and_log_message("Waiting for downstream device failed\n");
            }
        }
        else if (strcmp(wait_arg, "upstream") == 0)
        {
            unsigned int waited = 0;
            ret                 = 1;

            while (waited < MAX_WAIT_MS)
            {
                dlb_lip_status_t status = dlb_lip_get_status(p_dlb_lip, true);
                if ((status.status & LIP_UPSTREAM_CONNECTED) == LIP_UPSTREAM_CONNECTED)
                {
                    ret = 0;
                    break;
                }
                print_and_log_message("Waiting for upstream device \n");
                usleep(1000 * 1000LL);
                waited += 1000;
            }
        }
        else
        {
            ret           = 0;
            sleep_time_ms = strtoull(wait_arg, NULL, 10);
            usleep(sleep_time_ms * 1000LL);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ]\n", data);
        ret = 1;
    }

    return ret;
}

static int
get_audio_format_from_string(const char *codec, const char *subtype_str, const char *ext_str, dlb_lip_audio_format_t *audio_format)
{
    int           ret         = 0;
    unsigned long subtype_val = 0;
    unsigned long ext_val     = 0;

    audio_format->codec = get_codec_type_from_str(codec);
    if (audio_format->codec == IEC61937_AUDIO_CODECS)
    {
        ret = 1;
    }

    subtype_val = strtoul(subtype_str, NULL, 10);
    if (subtype_val < IEC61937_SUBTYPES)
    {
        audio_format->subtype = (dlb_lip_audio_formats_subtypes_t)(subtype_val);
    }
    else
    {
        ret = 1;
    }

    ext_val = strtoul(ext_str, NULL, 10);
    if (ext_val < MAX_AUDIO_FORMAT_EXTENSIONS)
    {
        audio_format->ext = (uint8_t)(ext_val);
    }
    else
    {
        ret = 1;
    }

    return ret;
}

static int process_command_req_audio_latency(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char                   codec_str[16]   = { 0 };
    char                   subtype_str[16] = { 0 };
    char                   ext_str[16]     = { 0 };
    int                    ret             = 0;
    const dlb_lip_status_t status          = dlb_lip_get_status(p_dlb_lip, true);
    (void)cec_bus;

    if (status.status == 0)
    {
        print_and_log_message("LIP not supported ignoring cmd!\n");
    }
    else if (sscanf(data, "%*s %*s %s %s %s\n", codec_str, subtype_str, ext_str) == 3)
    {
        unsigned char          audio_latency = 0;
        dlb_lip_audio_format_t format        = { 0 };

        ret = get_audio_format_from_string(codec_str, subtype_str, ext_str, &format);

        if (ret == 0)
        {
            ret = dlb_lip_get_audio_latency(p_dlb_lip, format, &audio_latency);
            print_and_log_message("Audio_latency=%u\n", audio_latency);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ]\n", data);
        ret = 1;
    }

    return ret;
}

static int get_video_mode_from_string(const char *color_format, const char *hdr_mode, dlb_lip_video_format_t *video_format)
{
    int ret = 0;

    if (strcmp(color_format, "HDR_STATIC") == 0)
    {
        video_format->color_format        = LIP_COLOR_FORMAT_HDR_STATIC;
        video_format->hdr_mode.hdr_static = LIP_HDR_STATIC_SDR;

        if (hdr_mode && (strcmp(hdr_mode, "SDR") == 0))
        {
            video_format->hdr_mode.hdr_static = LIP_HDR_STATIC_SDR;
        }
        else if (hdr_mode && (strcmp(hdr_mode, "HDR") == 0))
        {
            video_format->hdr_mode.hdr_static = LIP_HDR_STATIC_HDR;
        }
        else if (hdr_mode && (strcmp(hdr_mode, "SMPTE") == 0))
        {
            video_format->hdr_mode.hdr_static = LIP_HDR_STATIC_SMPTE_ST_2084;
        }
        else if (hdr_mode && (strcmp(hdr_mode, "HLG") == 0))
        {
            video_format->hdr_mode.hdr_static = LIP_HDR_STATIC_HLG;
        }
        else
        {
            print_and_log_message("Invalid hdr_mode[%s] for HDR_STATIC\n", hdr_mode ? hdr_mode : "NULL");
            ret = 1;
        }
    }
    else if (strcmp(color_format, "HDR_DYNAMIC") == 0)
    {
        video_format->color_format         = LIP_COLOR_FORMAT_HDR_DYNAMIC;
        video_format->hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_SMPTE_ST_2094_10;

        if (hdr_mode && (strcmp(hdr_mode, "SMPTE_ST_2094_10") == 0))
        {
            video_format->hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_SMPTE_ST_2094_10;
        }
        else if (hdr_mode && (strcmp(hdr_mode, "ETSI") == 0))
        {
            video_format->hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_ETSI_TS_103_433;
        }
        else if (hdr_mode && (strcmp(hdr_mode, "ITU") == 0))
        {
            video_format->hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_ITU_T_H265;
        }
        else if (hdr_mode && (strcmp(hdr_mode, "SMPTE_ST_2094_40") == 0))
        {
            video_format->hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_SMPTE_ST_2094_40;
        }
        else
        {
            print_and_log_message("Invalid hdr_mode[%s] for HDR_DYNAMIC\n", hdr_mode ? hdr_mode : "NULL");
            ret = 1;
        }
    }
    else if (strcmp(color_format, "DV") == 0)
    {
        video_format->color_format          = LIP_COLOR_FORMAT_DOLBY_VISION;
        video_format->hdr_mode.dolby_vision = LIP_HDR_DOLBY_VISION_SINK_LED;

        if (hdr_mode && (strcmp(hdr_mode, "SINK") == 0))
        {
            video_format->hdr_mode.dolby_vision = LIP_HDR_DOLBY_VISION_SINK_LED;
        }
        else if (hdr_mode && (strcmp(hdr_mode, "SOURCE") == 0))
        {
            video_format->hdr_mode.dolby_vision = LIP_HDR_DOLBY_VISION_SOURCE_LED;
        }
        else
        {
            print_and_log_message("Invalid hdr_mode[%s] for Dolby Vision\n", hdr_mode ? hdr_mode : "NULL");
            ret = 1;
        }
    }
    else
    {
        ret = 1;
    }

    return ret;
}

static int process_command_req_video_latency(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char                   color_format_str[16] = { 0 };
    char                   hdr_mode_str[32]     = { 0 };
    unsigned char          vic                  = 0; // VIC code
    int                    ret                  = 0;
    const dlb_lip_status_t status               = dlb_lip_get_status(p_dlb_lip, true);
    (void)cec_bus;

    if (status.status == 0)
    {
        print_and_log_message("LIP not supported ignoring cmd!\n");
    }
    else if (sscanf(data, "%*s %*s VIC%hhu %s %s\n", &vic, color_format_str, hdr_mode_str) >= 2)
    {
        dlb_lip_video_format_t video_format  = { 0 };
        unsigned char          video_latency = 0;
        video_format.vic                     = vic;

        ret |= get_video_mode_from_string(color_format_str, hdr_mode_str, &video_format);

        if (ret == 0)
        {
            ret = dlb_lip_get_video_latency(p_dlb_lip, video_format, &video_latency);
            print_and_log_message("Video_latency=%u\n", video_latency);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_req_av_latency(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char                   codec_str[16]        = { 0 };
    char                   subtype_str[16]      = { 0 };
    char                   ext_str[16]          = { 0 };
    char                   color_format_str[16] = { 0 };
    char                   hdr_mode_str[32]     = { 0 };
    unsigned char          vic                  = 0; // VIC code
    int                    ret                  = 0;
    const dlb_lip_status_t status               = dlb_lip_get_status(p_dlb_lip, true);
    (void)cec_bus;

    if (status.status == 0)
    {
        print_and_log_message("LIP not supported ignoring cmd!\n");
    }
    else if (
        sscanf(data, "%*s %*s %s %s %s VIC%hhu %s %s\n", codec_str, subtype_str, ext_str, &vic, color_format_str, hdr_mode_str)
        >= 5)
    {
        unsigned char          audio_latency = 0;
        unsigned char          video_latency = 0;
        dlb_lip_video_format_t video_format  = { 0 };
        dlb_lip_audio_format_t a_format      = { 0 };
        video_format.vic                     = vic;

        ret = get_audio_format_from_string(codec_str, subtype_str, ext_str, &a_format);
        ret |= get_video_mode_from_string(color_format_str, hdr_mode_str, &video_format);

        if (ret == 0)
        {
            ret = dlb_lip_get_av_latency(p_dlb_lip, video_format, a_format, &video_latency, &audio_latency);
            print_and_log_message("Video_latency=%u Audio_latency=%u\n", video_latency, audio_latency);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_update_audio_latency(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char                   codec_str[16]   = { 0 };
    char                   subtype_str[16] = { 0 };
    char                   ext_str[16]     = { 0 };
    unsigned char          audio_latency   = 0;
    int                    ret             = 0;
    const dlb_lip_status_t status          = dlb_lip_get_status(p_dlb_lip, true);
    (void)cec_bus;

    if (status.status == 0)
    {
        print_and_log_message("LIP not supported ignoring cmd!\n");
    }
    else if (sscanf(data, "%*s %*s %s %s %s %hhu\n", codec_str, subtype_str, ext_str, &audio_latency) == 4)
    {
        dlb_lip_audio_format_t a_format = { 0 };

        ret = get_audio_format_from_string(codec_str, subtype_str, ext_str, &a_format);
        if (ret == 0)
        {
            if (xml_parser.config_params.downstream_device_addr != DLB_LOGICAL_ADDR_UNKNOWN)
            {
                // HUB
                const uint8_t audio_rendering_mode = (((xml_parser.config_params.uuid >> 4) & 0xF) + 1U) % 0xF;
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFFFF0F) | (audio_rendering_mode << 4U);
            }
            else
            {
                // SINK
                const uint8_t audio_rendering_mode = ((xml_parser.config_params.uuid & 0xF) + 1U) % 0xF;
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFFFFF0) | audio_rendering_mode;
            }
            xml_parser.config_params.audio_latencies[a_format.codec][a_format.subtype][a_format.ext] = audio_latency;
            ret = dlb_lip_set_config(p_dlb_lip, &xml_parser.config_params, false, DLB_LOGICAL_ADDR_UNKNOWN);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_update_video_latency(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char                   color_format_str[16] = { 0 };
    char                   hdr_mode_str[32]     = { 0 };
    unsigned char          vic                  = 0; // VIC code
    unsigned char          video_latency        = 0;
    int                    ret                  = 0;
    const dlb_lip_status_t status               = dlb_lip_get_status(p_dlb_lip, true);
    (void)cec_bus;

    if (status.status == 0)
    {
        print_and_log_message("LIP not supported ignoring cmd!\n");
    }
    else if (sscanf(data, "%*s %*s VIC%hhu %s %s %hhu\n", &vic, color_format_str, hdr_mode_str, &video_latency) == 4)
    {
        dlb_lip_video_format_t v_format = { 0 };
        v_format.vic                    = vic;

        ret = get_video_mode_from_string(color_format_str, hdr_mode_str, &v_format);
        if (ret == 0)
        {
            if (xml_parser.config_params.downstream_device_addr != DLB_LOGICAL_ADDR_UNKNOWN)
            {
                // HUB
                const uint8_t video_rendering_mode = (((xml_parser.config_params.uuid >> 12U) & 0xF) + 1U) % 0xF;
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFF0FFF) | (video_rendering_mode << 12U);
            }
            else
            {
                // SINK
                const uint8_t video_rendering_mode = (((xml_parser.config_params.uuid >> 8U) & 0xF) + 1U) % 0xF;
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFFF0FF) | (video_rendering_mode << 8U);
            }

            xml_parser.config_params
                .video_latencies[v_format.vic][v_format.color_format][dlb_lip_get_hdr_mode_from_video_format(v_format)]
                = video_latency;
            ret = dlb_lip_set_config(p_dlb_lip, &xml_parser.config_params, false, DLB_LOGICAL_ADDR_UNKNOWN);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_update_av_latency(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char                   codec_str[16]        = { 0 };
    char                   subtype_str[16]      = { 0 };
    char                   ext_str[16]          = { 0 };
    char                   color_format_str[16] = { 0 };
    char                   hdr_mode_str[32]     = { 0 };
    unsigned char          vic                  = 0; // VIC code
    unsigned char          audio_latency        = 0;
    unsigned char          video_latency        = 0;
    int                    ret                  = 0;
    const dlb_lip_status_t status               = dlb_lip_get_status(p_dlb_lip, true);
    (void)cec_bus;

    if (status.status == 0)
    {
        print_and_log_message("LIP not supported ignoring cmd!\n");
    }
    else if (
        sscanf(
            data,
            "%*s %*s %s %s %s VIC%hhu %s %s %hhu %hhu\n",
            codec_str,
            subtype_str,
            ext_str,
            &vic,
            color_format_str,
            hdr_mode_str,
            &audio_latency,
            &video_latency)
        == 8)
    {
        dlb_lip_video_format_t v_format = { 0 };
        dlb_lip_audio_format_t a_format = { 0 };
        v_format.vic                    = vic;

        ret = get_audio_format_from_string(codec_str, subtype_str, ext_str, &a_format);
        ret |= get_video_mode_from_string(color_format_str, hdr_mode_str, &v_format);
        if (ret == 0)
        {
            if (xml_parser.config_params.downstream_device_addr != DLB_LOGICAL_ADDR_UNKNOWN)
            {
                // HUB
                const uint8_t video_rendering_mode = (((xml_parser.config_params.uuid >> 12U) & 0xF) + 1U) % 0xF;
                const uint8_t audio_rendering_mode = (((xml_parser.config_params.uuid >> 4U) & 0xF) + 1U) % 0xF;
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFF0FFF) | (video_rendering_mode << 12U);
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFFFF0F) | (audio_rendering_mode << 4U);
            }
            else
            {
                // SINK
                const uint8_t video_rendering_mode = (((xml_parser.config_params.uuid >> 8U) & 0xF) + 1U) % 0xF;
                const uint8_t audio_rendering_mode = ((xml_parser.config_params.uuid & 0xF) + 1U) % 0xF;
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFFF0FF) | (video_rendering_mode << 8U);
                xml_parser.config_params.uuid      = (xml_parser.config_params.uuid & 0xFFFFFFF0) | audio_rendering_mode;
            }

            xml_parser.config_params
                .video_latencies[v_format.vic][v_format.color_format][dlb_lip_get_hdr_mode_from_video_format(v_format)]
                = video_latency;
            xml_parser.config_params.audio_latencies[a_format.codec][a_format.subtype][a_format.ext] = audio_latency;

            ret = dlb_lip_set_config(p_dlb_lip, &xml_parser.config_params, false, DLB_LOGICAL_ADDR_UNKNOWN);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_update_uuid(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    uint32_t               uuid   = 0;
    int                    ret    = 0;
    const dlb_lip_status_t status = dlb_lip_get_status(p_dlb_lip, true);
    (void)cec_bus;

    if (status.status == 0)
    {
        print_and_log_message("LIP not supported ignoring cmd!\n");
    }
    else if (sscanf(data, "%*s %*s %u\n", &uuid) == 1)
    {
        xml_parser.config_params.uuid = uuid;
        ret                           = dlb_lip_set_config(p_dlb_lip, &xml_parser.config_params, false, DLB_LOGICAL_ADDR_UNKNOWN);
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_on_update_uuid(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    char          codec_str[16]        = { 0 };
    char          subtype_str[16]      = { 0 };
    char          ext_str[16]          = { 0 };
    char          color_format_str[16] = { 0 };
    char          hdr_mode_str[32]     = { 0 };
    unsigned char vic                  = 0; // VIC code
    int           ret                  = 0;
    (void)cec_bus;
    (void)p_dlb_lip;

    if (sscanf(data, "%*s %*s %*s %s %s %s VIC%hhu %s %s\n", codec_str, subtype_str, ext_str, &vic, color_format_str, hdr_mode_str)
        >= 5)
    {
        dlb_lip_video_format_t video_format = { 0 };
        dlb_lip_audio_format_t a_format     = { 0 };
        video_format.vic                    = vic;

        ret = get_audio_format_from_string(codec_str, subtype_str, ext_str, &a_format);
        ret |= get_video_mode_from_string(color_format_str, hdr_mode_str, &video_format);

        if (ret != 0)
        {
            print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        }
        on_update_uuid_av_formats_valid = ret ? false : true;
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

static int process_command_random(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE])
{
    int      ret       = 0;
    unsigned cmd_count = 0;
    (void)p_dlb_lip;

    if (sscanf(data, "%*s %u\n", &cmd_count) == 1)
    {
        srand((unsigned int)time(NULL));

        for (unsigned int i = 0; i < cmd_count; ++i)
        {
            unsigned char parsed_data[64] = { 0 };
            unsigned char cmd_size        = (rand() % 64) + 1;

            for (unsigned int byte_no = 0; byte_no < cmd_size; ++byte_no)
            {
                parsed_data[byte_no] = rand() % 255;
            }

            if (i % 2)
            {
                const dlb_lip_status_t    status = dlb_lip_get_status(p_dlb_lip, true);
                dlb_cec_logical_address_t addresses[MAX_UPSTREAM_DEVICES_COUNT + 1];
                unsigned int              valid_addresses = 0;
                if (status.downstream_device_addr != DLB_LOGICAL_ADDR_UNKNOWN)
                {
                    addresses[valid_addresses++] = status.downstream_device_addr;
                }
                for (unsigned int j = 0; j < MAX_UPSTREAM_DEVICES_COUNT; j += 1)
                {
                    if (status.upstream_devices_addresses[j] != DLB_LOGICAL_ADDR_UNKNOWN)
                    {
                        addresses[valid_addresses++] = status.upstream_devices_addresses[j];
                    }
                }
                if (valid_addresses)
                {
                    parsed_data[0]
                        = (unsigned char)((p_dlb_lip->cec_bus.logical_address << 4) | addresses[rand() % valid_addresses]);
                    parsed_data[1] = 0xa0;
                    parsed_data[2] = 0x00;
                    parsed_data[3] = 0xd0;
                    parsed_data[4] = 0x46;
                    parsed_data[5] = (rand() % (LIP_OPCODES - LIP_OPCODE_REQUEST_LIP_SUPPORT) + LIP_OPCODE_REQUEST_LIP_SUPPORT);
                    cmd_size       = cmd_size > 6 ? cmd_size : 6;
                }
            }
            transmit_data(cec_bus, parsed_data, cmd_size);
        }
    }
    else
    {
        print_and_log_message("ERROR parsing cmd [ %s ] \n", data);
        ret = 1;
    }

    return ret;
}

struct commands_handlers
{
    char *command;
    int (*func)(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char data[COMMAND_BUFFER_SIZE]);
} commands_list[] = {
    { "tx", process_command_tx },
    { "q", process_command_quit },
    { "wait_time", process_command_wait_time },
    { "wait", process_command_wait },
    { "req audio_latency", process_command_req_audio_latency },
    { "req video_latency", process_command_req_video_latency },
    { "req av_latency", process_command_req_av_latency },
    { "update audio_latency", process_command_update_audio_latency },
    { "update video_latency", process_command_update_video_latency },
    { "update av_latency", process_command_update_av_latency },
    { "update uuid", process_command_update_uuid },
    { "on update uuid", process_command_on_update_uuid },
    { "random", process_command_random },
};

static int process_console_command(dlb_lip_t *p_dlb_lip, dlb_cec_bus_t *cec_bus, const char buffer[COMMAND_BUFFER_SIZE])
{
    int ret = 1;

    for (unsigned int i = 0; i < ARRAY_SIZE(commands_list); ++i)
    {
        if (strncmp(buffer, commands_list[i].command, strlen(commands_list[i].command)) == 0)
        {
            ret = !commands_list[i].func(p_dlb_lip, cec_bus, buffer);
            if (process_command_quit != commands_list[i].func && !ret)
            {
                print_and_log_message("\n\nERROR: CMD(%s) PROCESSING FAILED\n\n\n", buffer);
            }
            break;
        }
    }

    return ret;
}

static void store_cache_callback(void *arg, uint32_t uuid, const void *const cache_data, unsigned int size)
{
    FILE *file          = NULL;
    char  filename[128] = { 0 };
    snprintf(filename, sizeof(filename), "cache_%x.dat", uuid);
    (void)arg;

    file = fopen(filename, "wb");
    if (file)
    {
        fwrite(cache_data, 1, size, file);
        fclose(file);
    }
}

static unsigned int read_cache_callback(void *arg, uint32_t uuid, void *const cache_data, unsigned int size)
{
    FILE *       file          = NULL;
    char         filename[128] = { 0 };
    unsigned int data_read     = 0;
    (void)arg;
    snprintf(filename, sizeof(filename), "cache_%x.dat", uuid);

    file = fopen(filename, "rb");
    if (file)
    {
        data_read = fread(cache_data, 1, size, file);
        fclose(file);
    }

    return data_read;
}

static uint32_t merge_uuid_callback(void *arg, uint32_t own_uuid, uint32_t ds_uuid)
{
    const uint8_t uuid_a[LIP_UUID_SIZE]          = { (own_uuid >> 24) & 0xFF, (own_uuid >> 16) & 0xFF };
    const uint8_t uuid_b[LIP_UUID_SIZE]          = { (ds_uuid >> 24) & 0xFF, (ds_uuid >> 16) & 0xFF };
    uint32_t      merged_uuid                    = 0;
    uint8_t       merged_uuid_arr[LIP_UUID_SIZE] = { 0 };
    // We are HUB - copy correct a/v rendering mode bits
    uint16_t rendering_mode = (own_uuid & 0xF0F0) | (ds_uuid & 0x0F0F);
    (void)arg;

    for (uint32_t i = 0; i < LIP_UUID_SIZE; ++i)
    {
        merged_uuid_arr[i] = uuid_a[i] ^ uuid_b[(i + 1) % LIP_UUID_SIZE];
    }

    merged_uuid = (uint32_t)merged_uuid_arr[0] << 24 | merged_uuid_arr[1] << 16 | rendering_mode;

    return merged_uuid;
}

static void status_change(void *arg, dlb_lip_status_t status)
{
    (void)arg;
    if (status.status & LIP_DOWNSTREAM_CONNECTED)
    {
        if (uuid_valid && downstream_uuid != status.downstream_device_uuid)
        {
            print_and_log_message(
                "Downstream device[%x] uuid change %x -> %x \n",
                status.downstream_device_addr,
                downstream_uuid,
                status.downstream_device_uuid);
            dlb_lip_osa_cancel_timer(&on_update_uuid_timer);
            dlb_lip_osa_set_timer(&on_update_uuid_timer, 1U);
        }
        print_and_log_message("Downstream device with addr 0x%x connected\n", status.downstream_device_addr);
        uuid_valid      = true;
        downstream_uuid = status.downstream_device_uuid;
    }
    else
    {
        uuid_valid = false;
    }
    if (status.status & LIP_UPSTREAM_CONNECTED)
    {
        print_and_log_message("Upstream device connected\n");
    }
}

static int uuid_timer_callback(void *arg, uint32_t callback_id)
{
    dlb_lip_t *p_dlb_lip = (dlb_lip_t *)arg;
    (void)callback_id;

    if (on_update_uuid_av_formats_valid)
    {
        uint8_t video_latency = 0;
        uint8_t audio_latency = 0;

        print_and_log_message("Calling dlb_lip_get_av_latency triggered by UUID update\n");
        dlb_lip_get_av_latency(p_dlb_lip, on_update_uuid_v_format, on_update_uuid_a_format, &video_latency, &audio_latency);
    }
    return 0;
}

int main(int argc, char **argv)
{
    cmdline_options     opt;
    dlb_lip_t *         p_dlb_lip         = NULL;
    dlb_cec_bus_t *     cec_bus           = NULL;
    dlb_lip_callbacks_t dlb_lip_callbacks = { 0 };

    unsigned char *p_mem;
    size_t         mem_size;

    FILE *commands_file = NULL;

    char buffer[COMMAND_BUFFER_SIZE];
    int  bExit = 0;

#if !defined(_MSC_VER)
    setvbuf(stdout, (char *)NULL, _IOLBF, 0);
    setvbuf(stdin, (char *)NULL, _IOLBF, 0);
#endif

    /* display copyright info */
    fprintf(stdout, "\n************** Dolby LIP Tool Version %u.%u.%u *****************\n", DLB_LIP_TOOL_V_API, DLB_LIP_TOOL_V_FCT, DLB_LIP_TOOL_V_MTNC);
    fprintf(stdout, "************** Dolby LIP library Version %u.%u **************\n", DLB_LIP_LIB_V_API, DLB_LIP_LIB_V_FCT);
    fprintf(stdout, "************** Build date: %s **************\n", __DATE__);
    fprintf(stdout, "************** Build time: %s **************\n", __TIME__);
    fprintf(stdout, "%s", dolby_copyright);

    /* parse command line */
    memset((void *)&opt, 0, sizeof(cmdline_options));
    parse_cmdline(argc, argv, &opt);

    update_lip_tool_state(opt.state_file_name, LIP_TOOL_INIT);

    memset((void *)&xml_parser, 0, sizeof(dlb_lip_xml_parser_t));

    xml_parser.config_params.downstream_device_addr = DLB_LOGICAL_ADDR_UNKNOWN;
    xml_parser.config_params.audio_transcoding      = false;
    memset(xml_parser.config_params.audio_latencies, LIP_INVALID_LATENCY, sizeof(xml_parser.config_params.audio_latencies));
    memset(xml_parser.config_params.video_latencies, LIP_INVALID_LATENCY, sizeof(xml_parser.config_params.video_latencies));
    if (opt.config_file_name[0] != '\0')
    {
        int status = 0;
        status     = parse_xml_config_file(&(xml_parser), opt.config_file_name);

        if (0 != status)
        {
            print_and_log_message("XML parsing ERROR!\n");
            exit(EXIT_FAILURE);
        }
    }

    if (opt.commands_file_name[0] != '\0')
    {
        commands_file = fopen(opt.commands_file_name, "r");
        if (commands_file == NULL)
        {
            print_and_log_message("Couldn't open commands file[%s]!\n", opt.commands_file_name);
            return -1;
        }
    }

    if (opt.log_file_name[0] != '\0')
    {
        log_file = fopen(opt.log_file_name, "w");
        if (log_file == NULL)
        {
            print_and_log_message("can't open log file: %s \n", opt.log_file_name);
            return -1;
        }
    }

    mem_size = dlb_lip_query_memory();
    p_mem    = (unsigned char *)malloc(mem_size);
    cec_bus = dlb_cec_bus_init(xml_parser.physical_address, opt.port_name, xml_parser.device_type, log_messages, NULL, opt.sim_arc);
    if (cec_bus == NULL)
    {
        free(p_mem);
        return -1;
    }

    dlb_lip_callbacks.arg                    = NULL;
    dlb_lip_callbacks.printf_callback        = log_messages;
    dlb_lip_callbacks.store_cache_callback   = opt.cache_enabled ? store_cache_callback : NULL;
    dlb_lip_callbacks.read_cache_callback    = opt.cache_enabled ? read_cache_callback : NULL;
    dlb_lip_callbacks.status_change_callback = status_change;
    dlb_lip_callbacks.merge_uuid_callback    = merge_uuid_callback;

    p_dlb_lip = dlb_lip_open(p_mem, &xml_parser.config_params, dlb_lip_callbacks, cec_bus);

    if (NULL == p_dlb_lip)
    {
        free(p_mem);
        return -1;
    }

    dlb_lip_osa_init_timer(&on_update_uuid_timer, uuid_timer_callback, p_dlb_lip);

    // Wait for downstream device
    if (xml_parser.config_params.downstream_device_addr != DLB_LOGICAL_ADDR_UNKNOWN
        && xml_parser.config_params.downstream_device_addr != DLB_LOGICAL_ADDR_UNREGISTERED)
    {
        const unsigned int MAX_RETRY_CNT = 10;

        if (wait_for_downstream_device(p_dlb_lip, MAX_RETRY_CNT) == false)
        {
            print_and_log_message("Waiting for downstream device failed\n");
        }
    }
#if 1
    print_and_log_message("waiting for input\n");

    while (!bExit)
    {
        update_lip_tool_state(opt.state_file_name, commands_file ? LIP_TOOL_PROCESSSING : LIP_TOOL_WAITING_FOR_DATA);
        memset(buffer, 0, sizeof(buffer));
        if (fgets(buffer, sizeof(buffer), commands_file ? commands_file : stdin))
        {
            if (commands_file)
            {
                print_and_log_message("%s", buffer);
            }

            // Strip new line characte
            buffer[strcspn(buffer, "\r\n")] = 0;

            if (buffer[0] == 0 || buffer[0] == '\n' || buffer[0] == '\r')
            {
                // Skip empty lines
                continue;
            }
            update_lip_tool_state(opt.state_file_name, LIP_TOOL_PROCESSSING);

            if (process_console_command(p_dlb_lip, cec_bus, buffer))
            {
                if (buffer[0] != 0 && buffer[0] != '\n' && buffer[0] != '\r')
                {
                    print_and_log_message("waiting for input\n");
                }
            }
            else
            {
                print_and_log_message("Exiting ... cmd: %s\n", buffer);
                bExit = 1;
            }

            if (!bExit)
            {
                usleep(WAIT_TIME_MS * 1000LL);
            }
        }
        else
        {
            if (commands_file)
            {
                if (feof(commands_file))
                {
                    fclose(commands_file);
                    commands_file = NULL;
                }
            }
        }
    }
#endif
    dlb_lip_osa_delete_timer(&on_update_uuid_timer);
    dlb_lip_close(p_dlb_lip);
    dlb_cec_bus_destroy();
    free(p_mem);

    if (log_file)
    {
        fclose(log_file);
        log_file = NULL;
    }

    update_lip_tool_state(opt.state_file_name, LIP_TOOL_QUIT);

    return 0;
}

/*!
Displays the help menu of the tool at command line

@return void
*/
void usage(char **const argv)
{
    fprintf(stdout, "Build date: " __DATE__ "\n\n");
    fprintf(stdout, "Usage:");
    fprintf(stdout, "\t%s -x <file> [-x <file>] [-v]]\n\n", argv[0]);
    fprintf(stdout, "MANDATORY attributes:\n");
    fprintf(stdout, "\t-x:     [file] Reads LIP parameters of the device from XML file.\n");
    fprintf(stdout, "OPTIONAL attributes:\n");
    fprintf(stdout, "\t-a:     Act as ARC receiver - anwser to CEC ARC communication\n");
    fprintf(stdout, "\t-c:     [file] Reads real-time commands from file.\n");
    fprintf(stdout, "\t-f:     [file] Writes all LIP and libCEC log message with timestamps to a file.\n");
    fprintf(stdout, "\t-n:     No cache - disable caching\n");
    fprintf(stdout, "\t-p:     [port] Pulse8 cec adapter port name eg. COM4\n");
    fprintf(stdout, "\t-s:     [file] Writes current LIP tool state to a file.\n");
    fprintf(stdout, "\t-v:    verbosity flag\n");
    fprintf(stdout, "Supported real-time commands:\n");
    for (unsigned int i = 0; i < ARRAY_SIZE(commands_list); ++i)
    {
        fprintf(stdout, "\t%s\n", commands_list[i].command);
    }
    /* print a new line for pretty & clear help output */
    fprintf(stdout, "\n");
}
