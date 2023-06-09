/******************************************************************************
 * This program is protected under international and U.S. copyright laws as
 * an unpublished work. This program is confidential and proprietary to the
 * copyright owners. Reproduction or disclosure, in whole or in part, or the
 * production of derivative works therefrom without the express permission of
 * the copyright owners is prohibited.
 *
 *                  Copyright (C) 2013-2017 by Dolby Laboratories.
 *                            All rights reserved.
 ******************************************************************************/

/** @file dlb_xml.h */

#ifndef DLB_XML_H
#define DLB_XML_H

#define DLB_XML_VERSION_MAJOR   2
#define DLB_XML_VERSION_MINOR   0
#define DLB_XML_VERSION_UPDATE  1

/* Error codes */
#define DLB_XML_SUCCESS         0 /**< No Error */
#define DLB_XML_ERROR           1 /**< Error */
#define DLB_XML_INVALID_POINTER 2 /**< Invalid pointer input parameter */

#if defined(_MSC_VER) && !defined(inline)
#  define inline __inline
#endif

/** @brief DLB_XML Version Parameters structure is the container of version information on the
 *  components of the dlb_xml library. */
typedef struct dlb_xml_version_s
{
    int version_major;      /**< major version number                   */
    int version_minor;      /**< minor version number                   */
    int version_update;     /**< update (build) version number          */
} dlb_xml_version;

/**
 *  @brief Returns the version on the dlb_xml library
 *  
 *  USAGE:
 *      Pass a pointer to a structure 'dlb_xml_version' into this function, 
 *      and it will be filled with the version information of this library.
 * 
 *  RETURN:
 *      DLB_XML_SUCCESS - on success
 *      DLB_XML_INVALID_POINTER - when p_version is NULL
 */
int 
dlb_xml_query_version
    ( dlb_xml_version *p_version   /**< [out] version information of this library */
    );

/**
 * @brief Simple XML parser
 * 
 * USAGE:
 *     Provide a callback function to read the XML line by line, and 
 *     two callback functions to handle parsed XML elements and attributes.
 *     A caller-side context can be provided for the callback functions.
 * 
 * LIMITATIONS:
 *     Non-validating parser, ignoring external entities
 *     Limited error and syntax checking
 *     Entities are not supported
 *     Maximum significant characters in element tags: 64
 *     Maximum text length per element: 2048
 * 
 *  RETURN:
 *      DLB_XML_SUCCESS - on success
 *      DLB_XML_ERROR - on parsing error
 *      DLB_XML_INVALID_POINTER - when line_callback is NULL
 * 
 * @param p_context: Context, passed back when callbacks are called
 *   Can be NULL if not needed by the callback functions.
 * @param line_callback: Callback to load the next line. 
 *   Has to return pointer to first char of next line, 
 *   or NULL when no more lines are available.
 * @param element_callback: Called when element is opened (text is NULL,
 *   before any attribute callbacks) or closed (with text enclosed by element).
 *   Can be NULL.
 * @param attribute_callback: Called for each attribute inside an element's
 *   open tag or declaration. Can be NULL.
 * @param error_callback: Called to report parsing errors.  Can be NULL
 */
int dlb_xml_parse2
    ( void *p_context
    , char *(*line_callback)      (void *p_context)
    , int   (*element_callback)   (void *p_context, char *tag, char *text)
    , int   (*attribute_callback) (void *p_context, char *tag, char *attribute, char *value)
    , void  (*error_callback)     (void *p_context, char *msg)
    );

/**
 * @brief Parse XML file to determine if any attribute values need additional memory allocated
 * 
 * USAGE:
 *     Provide a callback function to read the XML line by line, and 
 *     two callback functions to handle parsed XML elements and attributes.
 *     A caller-side context can be provided for the callback functions.
 *     Also provide a callback function to handle the length of attribute values.
 * 
 * LIMITATIONS:
 *     Non-validating parser, ignoring external entities
 *     Limited error and syntax checking
 *     Entities are not supported
 *     Maximum significant characters in element tags: 64
 *     Maximum text length per element without external dynamic memory allocation: 2048
 * 
 *  RETURN:
 *      DLB_XML_SUCCESS - on success
 *      DLB_XML_ERROR - on parsing error
 *      DLB_XML_INVALID_POINTER - when line_callback is NULL
 * 
 * @param p_context: Context, passed back when callbacks are called
 *   Can be NULL if not needed by the callback functions.
 * @param line_callback: Callback to load the next line. 
 *   Has to return pointer to first char of next line, 
 *   or NULL when no more lines are available.
 * @param element_callback: Called when element is opened (text is NULL,
 *   before any attribute callbacks) or closed (with text enclosed by element).
 *   Can be NULL.
 * @param error_callback: Called to report parsing errors.  Can be NULL
 * @param query_callback: Called for each attribute inside an element's
 *   open tag or declaration.
 *   The length of an attribute's value will be written into the length argument.
 *   Cannot be NULL.
 */
int dlb_xml_query_mem
    (void *p_context
    , char *(*line_callback)   (void *p_context)
    , int   (*element_callback)(void *p_context, char *tag, char *text)
    , void  (*error_callback)     (void *p_context, char *msg)
    , int   (*query_callback)  (void *p_context, char *tag, char *attribute, char *value, int *length)
    );

/**
 * @brief backwards-compatible interface to the parser
 */
static inline
int dlb_xml_parse
    ( void *p_context
    , char *(*line_callback)      (void *p_context)
    , int   (*element_callback)   (void *p_context, char *tag, char *text)
    , int   (*attribute_callback) (void *p_context, char *tag, char *attribute, char *value)
    )
{
    return dlb_xml_parse2(p_context, line_callback, element_callback, attribute_callback, NULL);
}

/**
 * @brief Simple XML parser, to be used only in conjunction with dlb_xml_query_mem()
 * 
 * USAGE:
 *     Provide a callback function to read the XML line by line, and 
 *     two callback functions to handle parsed XML elements and attributes.
 *     A caller-side context can be provided for the callback functions.
 *     Also provide a pointer to an allocated block of memory (the size of which is determined by calling dlb_xml_query_mem())
 * 
 * LIMITATIONS:
 *     Non-validating parser, ignoring external entities
 *     Limited error and syntax checking
 *     Entities are not supported
 *     Maximum significant characters in element tags: 64
 *     Maximum text length per element without external dynamic memory allocation: 2048
 * 
 *  RETURN:
 *      DLB_XML_SUCCESS - on success
 *      DLB_XML_ERROR - on parsing error
 *      DLB_XML_INVALID_POINTER - when line_callback is NULL
 * 
 * @param p_context: Context, passed back when callbacks are called
 *   Can be NULL if not needed by the callback functions.
 * @param line_callback: Callback to load the next line. 
 *   Has to return pointer to first char of next line, 
 *   or NULL when no more lines are available.
 * @param element_callback: Called when element is opened (text is NULL,
 *   before any attribute callbacks) or closed (with text enclosed by element).
 *   Can be NULL.
 * @param attribute_callback: Called for each attribute inside an element's
 *   open tag or declaration. Can be NULL.
 * @param error_callback: Called to report parsing errors.  Can be NULL
 * @param p_mem: Pointer to an allocated block of memory (the size of which is determined by calling dlb_xml_query_mem())
 */
int dlb_xml_parse_extra_mem
    ( void *p_context
    , char *(*line_callback)      (void *p_context)
    , int   (*element_callback)   (void *p_context, char *tag, char *text)
    , int   (*attribute_callback) (void *p_context, char *tag, char *attribute, char *value)
    , void  (*error_callback)     (void *p_context, char *msg)
    , void *p_mem
    );

#endif /* DLB_XML_H */

