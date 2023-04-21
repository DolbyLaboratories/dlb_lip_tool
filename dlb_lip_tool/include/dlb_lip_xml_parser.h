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
 *  @file       dlb_lip_xml_parser.h
 *  @brief      todo
 *
 *  Todo
 */

#ifndef DLB_LIP_XML_PARSER_H
#define DLB_LIP_XML_PARSER_H

#include <stdbool.h>
#include "dlb_lip.h"            // for dlb_lip_config_params_t
#include "dlb_lip_libcec_bus.h" // for dlb_lip_device_type_t

#define MAX_XML_LINE 4096

typedef struct dlb_lip_xml_cache_s
{
    bool                   vid_latency_opened;
    bool                   aud_latency_opened;
    dlb_lip_video_format_t video_format;
    dlb_lip_audio_format_t audio_format;
} dlb_lip_xml_cache_t;

typedef struct dlb_lip_xml_parser_s
{
    FILE *config_file;
    char  xml_line[MAX_XML_LINE];

    dlb_lip_xml_cache_t     xml_cache;
    dlb_lip_config_params_t config_params;
    uint16_t                physical_address;
    dlb_lip_device_type_t   device_type;
} dlb_lip_xml_parser_t;

/**
 * @brief Parses input XML file with LIP device config parameters.
 * @return 0 if parsed correctly, 1 if error occured.
 */

int parse_xml_config_file(dlb_lip_xml_parser_t *p_parser, const char *p_config_file_name);

/**
 * @brief Translates codec name to dlb_lip_audio_codec_t
 * @param codec_str Null terminated codec name string.
 * @return one of dlb_lip_audio_codec_t value.
 */
dlb_lip_audio_codec_t get_codec_type_from_str(const char *const codec_str);

#endif
