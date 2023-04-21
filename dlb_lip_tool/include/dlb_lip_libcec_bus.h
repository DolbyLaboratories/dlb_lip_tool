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
 *  @file       dlb_lip_libcec_bus.h
 *  @brief      LIP libcec bus implementation
 *
 */
#pragma once

#include <cectypes.h>
#include "dlb_lip.h"
#include "dlb_lip_cec_bus.h"
#include "dlb_lip_types.h"

typedef enum dlb_lip_device_type_e
{
    LIP_DEVICE_TV,
    LIP_DEVICE_STB,
    LIP_DEVICE_AVR,

    LIP_DEVICE_TYPES
} dlb_lip_device_type_t;

/**
 * @brief Initialize CEC bus transport
 * @return CEC bus interface or NULL
 */
dlb_cec_bus_t *dlb_cec_bus_init(
    const uint16_t        physical_address,
    const char *          port_name,
    dlb_lip_device_type_t device_type,
    printf_callback_t     func,
    void *                arg,
    bool                  sim_arc);

/**
 * @brief Destroy cec bus transport
 */
void dlb_cec_bus_destroy(void);

int dlb_cec_poll_device(cec_logical_address downstream_device);
