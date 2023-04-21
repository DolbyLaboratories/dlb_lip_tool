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
 *  @file       dlb_lip_xml_parser.c
 *  @brief      todo
 *
 *  Todo
 */

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dlb_lip_types.h"
#include "dlb_lip_xml_parser.h"
#include "dlb_xml.h"

/**********************************************************************
 *
 *
 *  Function declarations
 *
 *
 **********************************************************************/

/**********************************************************************
 *
 *
 *  Static variables
 *
 *
 **********************************************************************/

static const struct
{
    const dlb_lip_audio_codec_t codec;
    const char *const           name;
} codec_names[IEC61937_AUDIO_CODECS] = {
    { PCM, "PCM" },
    { IEC61937_AC3, "DD" },
    { IEC61937_SMPTE_338M, "SMPTE_338M" },
    { IEC61937_PAUSE_BURST, "PAUSE_BURST" },
    { IEC61937_MPEG1_L1, "MPEG1_L1" },
    { IEC61937_MEPG1_L2_L3, "MEPG1_L2_L3" },
    { IEC61937_MPEG2, "MPEG2" },
    { IEC61937_MPEG2_AAC, "AAC" },
    { IEC61937_MPEG2_L1, "MPEG2_L1" },
    { IEC61937_MPEG2_L2, "MPEG2_L2" },
    { IEC61937_MPEG2_L3, "MPEG2_L3" },
    { IEC61937_DTS_TYPE_I, "DTS_TYPE_I" },
    { IEC61937_DTS_TYPE_II, "DTS_TYPE_II" },
    { IEC61937_DTS_TYPE_III, "DTS_TYPE_IIII" },
    { IEC61937_ATRAC, "ATRAC" },
    { IEC61937_ATRAC_2_3, "ATRAC_2_3" },
    { IEC61937_ATRAC_X, "ATRAC_X" },
    { IEC61937_DTS_TYPE_IV, "DTS_TYPE_IV" },
    { IEC61937_WMA_PRO, "WMA_PRO" },
    { IEC61937_MPEG2_AAC_LSF, "MPEG2_AAC_LSF" },
    { IEC61937_MPEG4_AAC, "MPEG4_AAC" },
    { IEC61937_EAC3, "DDP" },
    { IEC61937_MAT, "MAT" },
    { IEC61937_MPEG4, "MPEG4" },
};

/**********************************************************************
 *
 *  dlb_xml callbacks
 *
 **********************************************************************/

static char *line_callback(void *p_context);

static int element_callback(void *p_context, char *tag, char *text);

static int attribute_callback(void *p_context, char *tag, char *attribute, char *value);

static void error_callback(void *p_context, char *msg);

/**********************************************************************
 *
 *  Utils
 *
 **********************************************************************/

static void clear_xml_cache(dlb_lip_xml_cache_t *p_xml_cache);

static int save_video_latency(dlb_lip_xml_parser_t *p_ctx, char *text);

static int save_audio_latency(dlb_lip_xml_parser_t *p_ctx, char *text);

static int cache_video_latency_params(dlb_lip_xml_parser_t *p_ctx, char *attribute, char *value);

static int cache_audio_latency_params(dlb_lip_xml_parser_t *p_ctx, char *attribute, char *value);

/***********************************************************************
 *
 *
 *  Function definitions
 *
 *
 ***********************************************************************/

int parse_xml_config_file(dlb_lip_xml_parser_t *p_parser, const char *p_config_file_name)
{
    int status            = 0;
    p_parser->config_file = fopen(p_config_file_name, "r");
    if (p_parser->config_file == NULL)
    {
        printf("Couldn't open XML config file[%s]!\n", p_config_file_name);
        return -1;
    }

    status = dlb_xml_parse2(p_parser, &line_callback, &element_callback, &attribute_callback, &error_callback);
    fclose(p_parser->config_file);

    return status;
}

static void clear_xml_cache(dlb_lip_xml_cache_t *p_xml_cache)
{
    p_xml_cache->aud_latency_opened        = false;
    p_xml_cache->vid_latency_opened        = false;
    p_xml_cache->video_format.vic          = 0;
    p_xml_cache->video_format.color_format = LIP_COLOR_FORMAT_COUNT;
    p_xml_cache->audio_format.codec        = IEC61937_AUDIO_CODECS;
    p_xml_cache->audio_format.subtype      = IEC61937_SUBTYPES;
    p_xml_cache->audio_format.ext          = MAX_AUDIO_FORMAT_EXTENSIONS;
}

static int save_video_latency(dlb_lip_xml_parser_t *p_ctx, char *text)
{
    if (p_ctx->xml_cache.vid_latency_opened)
    {
        uint8_t                     VIC          = p_ctx->xml_cache.video_format.vic;
        dlb_lip_color_format_type_t color_format = p_ctx->xml_cache.video_format.color_format;
        uint8_t                     hdr_mode     = color_format != LIP_COLOR_FORMAT_COUNT
                               ? dlb_lip_get_hdr_mode_from_video_format(p_ctx->xml_cache.video_format)
                               : HDR_MODES_COUNT;

        uint8_t vid_latency     = (uint8_t)strtoul(text, NULL, 10);
        uint8_t old_vid_latency = 0;

        if (vid_latency > 0 && vid_latency < LIP_INVALID_LATENCY)
        {
            // all parameters provided, save given latency
            if ((VIC != 0) && (color_format != LIP_COLOR_FORMAT_COUNT) && (hdr_mode != HDR_MODES_COUNT))
            {
                old_vid_latency = p_ctx->config_params.video_latencies[VIC][color_format][hdr_mode];
                if (LIP_INVALID_LATENCY != old_vid_latency)
                {
                    fprintf(stderr, "WARNING: Overwriting video latency value: %d with: %d!\n", old_vid_latency, vid_latency);
                }

                p_ctx->config_params.video_latencies[VIC][color_format][hdr_mode] = vid_latency;
            }
            // only VIC provided, save given latency for all color_formats and hdr_modes
            else if ((VIC != 0) && (color_format == LIP_COLOR_FORMAT_COUNT) && (hdr_mode == HDR_MODES_COUNT))
            {
                for (uint8_t proc_idx = 0; proc_idx < LIP_COLOR_FORMAT_COUNT; proc_idx++)
                {
                    for (uint8_t hdr_idx = 0; hdr_idx < HDR_MODES_COUNT; hdr_idx++)
                    {
                        old_vid_latency = p_ctx->config_params.video_latencies[VIC][proc_idx][hdr_idx];
                        if (LIP_INVALID_LATENCY != old_vid_latency)
                        {
                            fprintf(
                                stderr, "WARNING: Overwriting video latency value: %d with: %d!\n", old_vid_latency, vid_latency);
                        }
                        p_ctx->config_params.video_latencies[VIC][proc_idx][hdr_idx] = vid_latency;
                    }
                }
            }
            // VIC and color_format mode provided, save given latency for all hdr_modes
            else if ((VIC != 0) && (color_format != LIP_COLOR_FORMAT_COUNT) && (hdr_mode == HDR_MODES_COUNT))
            {
                for (uint8_t hdr_idx = 0; hdr_idx < HDR_MODES_COUNT; hdr_idx++)
                {
                    old_vid_latency = p_ctx->config_params.video_latencies[VIC][color_format][hdr_idx];
                    if (LIP_INVALID_LATENCY != old_vid_latency)
                    {
                        fprintf(stderr, "WARNING: Overwriting video latency value: %d with: %d!\n", old_vid_latency, vid_latency);
                    }

                    p_ctx->config_params.video_latencies[VIC][color_format][hdr_idx] = vid_latency;
                }
            }
            // no VIC provided (or couple other permutations), save given latency for all VICs, processing and latency modes
            else
            {
                for (uint8_t vic_idx = 0; vic_idx < MAX_VICS; vic_idx++)
                {
                    for (uint8_t color_idx = 0; color_idx < LIP_COLOR_FORMAT_COUNT; color_idx++)
                    {
                        old_vid_latency = p_ctx->config_params.video_latencies[vic_idx][color_idx][hdr_mode];
                        if (LIP_INVALID_LATENCY != old_vid_latency)
                        {
                            fprintf(
                                stderr, "WARNING: Overwriting video latency value: %d with: %d!\n", old_vid_latency, vid_latency);
                        }

                        p_ctx->config_params.video_latencies[vic_idx][color_idx][hdr_mode] = vid_latency;
                    }
                }
            }
        }
        else
        {
            fprintf(stderr, "ERROR: Invalid video latency value provided in the input XML file!\n");
            return 1;
        }
    }
    else
    {
        fprintf(stderr, "ERROR: Video latency without parameters in the input XML file!\n");
        return 1;
    }

    p_ctx->xml_cache.vid_latency_opened = false;
    return 0;
}

static int save_audio_latency(dlb_lip_xml_parser_t *p_ctx, char *text)
{
    // TODO handle extension
    if (p_ctx->xml_cache.aud_latency_opened)
    {
        dlb_lip_audio_format_t audio_format = p_ctx->xml_cache.audio_format;

        uint8_t aud_latency     = (uint8_t)strtoul(text, NULL, 10);
        uint8_t old_aud_latency = 0;

        if (aud_latency > 0 && aud_latency < LIP_INVALID_LATENCY)
        {
            // all parameters provided, save given latency
            if ((audio_format.codec != IEC61937_AUDIO_CODECS) && (audio_format.subtype != IEC61937_SUBTYPES)
                && (audio_format.ext != MAX_AUDIO_FORMAT_EXTENSIONS))
            {
                old_aud_latency = p_ctx->config_params.audio_latencies[audio_format.codec][audio_format.subtype][audio_format.ext];
                if (LIP_INVALID_LATENCY != old_aud_latency)
                {
                    fprintf(stderr, "WARNING: Overwriting audio latency value: %d with: %d!\n", old_aud_latency, aud_latency);
                }

                p_ctx->config_params.audio_latencies[audio_format.codec][audio_format.subtype][audio_format.ext] = aud_latency;
            }
            // only audio format provided, save given latency for all latency modes
            else if (
                (audio_format.codec != IEC61937_AUDIO_CODECS)
                && (audio_format.subtype == IEC61937_SUBTYPES && audio_format.ext == MAX_AUDIO_FORMAT_EXTENSIONS))
            {
                for (unsigned int subtype_idx = 0; subtype_idx < IEC61937_SUBTYPES; subtype_idx++)
                {
                    for (unsigned int extension_idx = 0; extension_idx < MAX_AUDIO_FORMAT_EXTENSIONS; extension_idx++)
                    {
                        old_aud_latency = p_ctx->config_params.audio_latencies[audio_format.codec][subtype_idx][extension_idx];
                        if (LIP_INVALID_LATENCY != old_aud_latency)
                        {
                            fprintf(
                                stderr, "WARNING: Overwriting audio latency value: %d with: %d!\n", old_aud_latency, aud_latency);
                        }

                        p_ctx->config_params.audio_latencies[audio_format.codec][subtype_idx][extension_idx] = aud_latency;
                    }
                }
            }
            // audio format provided and subtype provided, save given latency for ext
            else if (
                (audio_format.codec != IEC61937_AUDIO_CODECS) && (audio_format.subtype != IEC61937_SUBTYPES)
                && (audio_format.ext == MAX_AUDIO_FORMAT_EXTENSIONS))
            {
                for (unsigned int extension_idx = 0; extension_idx < MAX_AUDIO_FORMAT_EXTENSIONS; extension_idx++)
                {
                    old_aud_latency = p_ctx->config_params.audio_latencies[audio_format.codec][audio_format.subtype][extension_idx];
                    if (LIP_INVALID_LATENCY != old_aud_latency)
                    {
                        fprintf(stderr, "WARNING: Overwriting audio latency value: %d with: %d!\n", old_aud_latency, aud_latency);
                    }

                    p_ctx->config_params.audio_latencies[audio_format.codec][audio_format.subtype][extension_idx] = aud_latency;
                }
            }
            // only latency mode provided, save given latency for all audio formats
            else if (
                (audio_format.codec == IEC61937_AUDIO_CODECS) && (audio_format.subtype == IEC61937_SUBTYPES)
                && (audio_format.ext == MAX_AUDIO_FORMAT_EXTENSIONS))
            {
                for (uint8_t aud_frmt_idx = 0; aud_frmt_idx < IEC61937_AUDIO_CODECS; aud_frmt_idx++)
                {
                    for (unsigned int subtype_idx = 0; subtype_idx < IEC61937_SUBTYPES; subtype_idx++)
                    {
                        for (unsigned int extension_idx = 0; extension_idx < MAX_AUDIO_FORMAT_EXTENSIONS; extension_idx++)
                        {
                            old_aud_latency = p_ctx->config_params.audio_latencies[aud_frmt_idx][subtype_idx][extension_idx];
                            if (LIP_INVALID_LATENCY != old_aud_latency)
                            {
                                fprintf(
                                    stderr,
                                    "WARNING: Overwriting audio latency value: %d with: %d!\n",
                                    old_aud_latency,
                                    aud_latency);
                            }
                            p_ctx->config_params.audio_latencies[aud_frmt_idx][subtype_idx][extension_idx] = aud_latency;
                        }
                    }
                }
            }
        }
        else
        {
            fprintf(stderr, "ERROR: Invalid audio latency value provided in the input XML file!\n");
            return 1;
        }
    }
    else
    {
        fprintf(stderr, "ERROR: Audio latency without parameters in the input XML file!\n");
        return 1;
    }

    p_ctx->xml_cache.aud_latency_opened = false;
    return 0;
}

static int cache_video_latency_params(dlb_lip_xml_parser_t *p_ctx, char *attribute, char *value)
{
    if (!strncmp(attribute, "VIC", strlen("VIC")))
    {
        p_ctx->xml_cache.video_format.vic = (uint8_t)strtoul(value, NULL, 10);
    }
    else if (!strncmp(attribute, "color_format", strlen("color_format")))
    {
        if (!strncmp(value, "HDR_STATIC", strlen("HDR_STATIC")))
        {
            p_ctx->xml_cache.video_format.color_format        = LIP_COLOR_FORMAT_HDR_STATIC;
            p_ctx->xml_cache.video_format.hdr_mode.hdr_static = LIP_HDR_STATIC_COUNT;
        }
        else if (!strncmp(value, "HDR_DYNAMIC", strlen("HDR_DYNAMIC")))
        {
            p_ctx->xml_cache.video_format.color_format         = LIP_COLOR_FORMAT_HDR_DYNAMIC;
            p_ctx->xml_cache.video_format.hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_COUNT;
        }
        else if (!strncmp(value, "DV", strlen("DV")))
        {
            p_ctx->xml_cache.video_format.color_format          = LIP_COLOR_FORMAT_DOLBY_VISION;
            p_ctx->xml_cache.video_format.hdr_mode.dolby_vision = LIP_HDR_DOLBY_VISION_COUNT;
        }
        else
        {
            p_ctx->xml_cache.video_format.color_format = LIP_COLOR_FORMAT_COUNT;
        }
    }
    else if (!strncmp(attribute, "hdr_mode", strlen("hdr_mode")))
    {
        if (p_ctx->xml_cache.video_format.color_format == LIP_COLOR_FORMAT_HDR_STATIC)
        {
            if (strcmp(value, "SDR") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_static = LIP_HDR_STATIC_SDR;
            }
            else if (strcmp(value, "HDR") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_static = LIP_HDR_STATIC_HDR;
            }
            else if (strcmp(value, "SMPTE") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_static = LIP_HDR_STATIC_SMPTE_ST_2084;
            }
            else if (strcmp(value, "HLG") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_static = LIP_HDR_STATIC_HLG;
            }
            else
            {
                fprintf(stderr, "ERROR: invalid or unsupported video format!\n");
                return 1;
            }
        }
        else if (p_ctx->xml_cache.video_format.color_format == LIP_COLOR_FORMAT_HDR_DYNAMIC)
        {
            if (strcmp(value, "SMPTE_ST_2094_10") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_SMPTE_ST_2094_10;
            }
            else if (strcmp(value, "ETSI") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_ETSI_TS_103_433;
            }
            else if (strcmp(value, "ITU") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_ITU_T_H265;
            }
            else if (strcmp(value, "SMPTE_ST_2094_40") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.hdr_dynamic = LIP_HDR_DYNAMIC_SMPTE_ST_2094_40;
            }
            else
            {
                fprintf(stderr, "ERROR: invalid or unsupported video format!\n");
                return 1;
            }
        }
        else if (p_ctx->xml_cache.video_format.color_format == LIP_COLOR_FORMAT_DOLBY_VISION)
        {
            if (strcmp(value, "SINK") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.dolby_vision = LIP_HDR_DOLBY_VISION_SINK_LED;
            }
            else if (strcmp(value, "SOURCE") == 0)
            {
                p_ctx->xml_cache.video_format.hdr_mode.dolby_vision = LIP_HDR_DOLBY_VISION_SOURCE_LED;
            }
            else
            {
                fprintf(stderr, "ERROR: invalid or unsupported video format!\n");
                return 1;
            }
        }
        else
        {
            fprintf(stderr, "ERROR: invalid or unsupported video format!\n");
            return 1;
        }
    }
    else
    {
        fprintf(
            stderr,
            "ERROR: invalid or unsupported video format [%s=%s]!\n",
            attribute ? attribute : "NULL",
            value ? value : "NULL");
        return 1;
    }

    return 0;
}

dlb_lip_audio_codec_t get_codec_type_from_str(const char *const codec_str)
{
    dlb_lip_audio_codec_t codec = IEC61937_AUDIO_CODECS;

    for (unsigned int i = 0; i < IEC61937_AUDIO_CODECS && codec_str; i++)
    {
        if (codec_names[i].name && (strcmp(codec_str, codec_names[i].name) == 0))
        {
            codec = codec_names[i].codec;
            break;
        }
    }

    return codec;
}

static int cache_audio_latency_params(dlb_lip_xml_parser_t *p_ctx, char *attribute, char *value)
{
    if (!strncmp(attribute, "format", strlen("format")))
    {
        p_ctx->xml_cache.audio_format.codec = get_codec_type_from_str(value);

        if (p_ctx->xml_cache.audio_format.codec == IEC61937_AUDIO_CODECS)
        {
            fprintf(stderr, "WARNING: %s: invalid or unsupported audio format!\n", value);
            p_ctx->xml_cache.audio_format.codec = PCM;
            return 1;
        }
    }
    else if (!strncmp(attribute, "subtype", strlen("subtype")))
    {
        unsigned long subtype = strtoul(value, NULL, 10);

        if (subtype > IEC61937_SUBTYPES)
        {
            fprintf(stderr, "WARNING: %s: invalid or unsupported audio subtype !\n", value);
            return 1;
        }
        p_ctx->xml_cache.audio_format.subtype = (dlb_lip_audio_formats_subtypes_t)(subtype);
    }
    else if (!strncmp(attribute, "ext", strlen("ext")))
    {
        unsigned long ext = strtoul(value, NULL, 10);

        if (ext > MAX_AUDIO_FORMAT_EXTENSIONS)
        {
            fprintf(stderr, "WARNING: %s: invalid or unsupported audio extension !\n", value);
            return 1;
        }
        p_ctx->xml_cache.audio_format.ext = (uint8_t)(ext);
    }
    else
    {
        fprintf(stderr, "WARNING: %s: invalid or unsupported audio attribute !\n", value);
        return 1;
    }
    return 0;
}

/**
 * @brief "Element" callback for XML parser
 *
 * @param p_context: Passed through context pointer
 * @param tag: Tag string (name of the element)
 * @param text: Text enclosed inside the element's open and close tags, or NULL on open tag
 */
static int element_callback(void *p_context, char *tag, char *text)
{
    dlb_lip_xml_parser_t *p_ctx = (dlb_lip_xml_parser_t *)p_context;
    if (text == NULL) /* Open tag */
    {
        if (!strncmp(tag, "VidLatency", strlen("VidLatency")))
        {
            clear_xml_cache(&p_ctx->xml_cache);
            p_ctx->xml_cache.vid_latency_opened = true;
        }
        else if (!strncmp(tag, "AudLatency", strlen("AudLatency")))
        {
            clear_xml_cache(&p_ctx->xml_cache);
            p_ctx->xml_cache.aud_latency_opened = true;
        }
    }
    else /* Close tag */
    {
        if (!strncmp(tag, "AudioOutput", strlen("AudioOutput")))
        {
            p_ctx->config_params.audio_transcoding                = true;
            p_ctx->config_params.audio_transcoding_format.ext     = 0;
            p_ctx->config_params.audio_transcoding_format.subtype = 0;
            p_ctx->config_params.audio_transcoding_format.codec   = get_codec_type_from_str(text);

            if (p_ctx->config_params.audio_transcoding_format.codec == IEC61937_AUDIO_CODECS)
            {
                fprintf(stderr, "WARNING: %s: invalid or unsupported audio format!\n", text);
                p_ctx->config_params.audio_transcoding_format.codec = PCM;
                return 1;
            }
        }
        else if (!strncmp(tag, "UUID", strlen("UUID")))
        {
            p_ctx->config_params.uuid = (uint32_t)strtoul(text, NULL, 16);
        }
        else if (!strncmp(tag, "PhysicalAddress", strlen("PhysicalAddress")))
        {
            uint8_t addr[4];

            if (sscanf(text, "%" SCNu8 ".%" SCNu8 ".%" SCNu8 ".%" SCNu8 "", &addr[0], &addr[1], &addr[2], &addr[3]) == 4)
            {
                p_ctx->physical_address = addr[0] << 12 | addr[1] << 8 | addr[2] << 4 | addr[3];
            }
            else
            {
                fprintf(stderr, "ERROR: Invalid physical address in the input XML file!\n");
                return 1;
            }
        }
        else if (!strncmp(tag, "LogicalAddressMap", strlen("LogicalAddressMap")))
        {
            int8_t addr;

            if (sscanf(text, "%" SCNd8, &addr) == 1)
            {
                p_ctx->config_params.downstream_device_addr = (dlb_cec_logical_address_t)addr;
            }
            else
            {
                fprintf(stderr, "ERROR: Invalid LogicalAddressMap in the input XML file!\n");
                return 1;
            }
        }
        else if (!strncmp(tag, "DeviceType", strlen("DeviceType")))
        {
            if (!strncmp(text, "playback", strlen("playback")))
            {
                p_ctx->device_type = LIP_DEVICE_STB;
            }
            else if (!strncmp(text, "audio", strlen("audio")))
            {
                p_ctx->device_type = LIP_DEVICE_AVR;
            }
            else if (!strncmp(text, "tv", strlen("tv")))
            {
                p_ctx->device_type = LIP_DEVICE_TV;
            }
            else
            {
                fprintf(stderr, "ERROR: Invalid device type in the input XML file!\n");
                return 1;
            }
        }
        else if (!strncmp(tag, "Renderer", strlen("Renderer")))
        {
            if (!strncmp(text, "audio", strlen("audio")))
            {
                p_ctx->config_params.render_mode = LIP_AUDIO_RENDERER;
            }
            else if (!strncmp(text, "video", strlen("video")))
            {
                p_ctx->config_params.render_mode = LIP_VIDEO_RENDERER;
            }
            else if (!strncmp(text, "av", strlen("av")))
            {
                p_ctx->config_params.render_mode = LIP_AUDIO_RENDERER | LIP_VIDEO_RENDERER;
            }
            else
            {
                fprintf(stderr, "ERROR: Invalid Renderer type(%s) in the input XML file!\n", text ? text : "NULL");
                return 1;
            }
        }
        else if (!strncmp(tag, "VidLatency", strlen("VidLatency")))
        {
            save_video_latency(p_ctx, text);
        }
        else if (!strncmp(tag, "AudLatency", strlen("AudLatency")))
        {
            save_audio_latency(p_ctx, text);
        }
    }

    return 0;
}

/**
 * @brief "Attribute" callback for XML parser
 *
 * @param p_context: Passed through context pointer
 * @param tag: Tag string (name of the element)
 * @param attribute: Attribute string (name of the attribute)
 * @param value: Text enclosed inside the attribute's quotes
 */
static int attribute_callback(void *p_context, char *tag, char *attribute, char *value)
{
    dlb_lip_xml_parser_t *p_ctx = (dlb_lip_xml_parser_t *)p_context;
    int                   ret   = 0;

    if (!strncmp(tag, "VidLatency", strlen("VidLatency")))
    {
        ret = cache_video_latency_params(p_ctx, attribute, value);
    }
    else if (!strncmp(tag, "AudLatency", strlen("AudLatency")))
    {
        ret = cache_audio_latency_params(p_ctx, attribute, value);
    }
    else
    {
        fprintf(stderr, "ERROR: Invalid tag [%s] in the input XML file!\n", tag);
        ret = 1;
    }

    return ret;
}

/**
 * @brief Loads next line from a file
 *
 * @param p_context: Passed through context pointer, to load a line
 */
static char *line_callback(void *p_context)
{
    if (feof(((dlb_lip_xml_parser_t *)p_context)->config_file))
    {
        return NULL;
    }
    return fgets(((dlb_lip_xml_parser_t *)p_context)->xml_line, MAX_XML_LINE, ((dlb_lip_xml_parser_t *)p_context)->config_file);
}

/**
 * @brief "Error" callback for XML parser
 *
 * @param p_context: Passed through context pointer
 * @param tag: Tag string (name of the element)
 * @param attribute: Attribute string (name of the attribute)
 * @param value: Text enclosed inside the attribute's quotes
 */
static void error_callback(void *p_context, char *msg)
{
    (void)p_context;
    printf("ERR('%s')\n", msg);

    return;
}
