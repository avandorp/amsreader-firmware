/**
 * @copyright Utilitech AS 2023
 * License: Fair Source
 * 
 */

#ifndef _DATAPASERSER_H
#define _DATAPASERSER_H

#define DATA_TAG_NONE 0x00
#define DATA_TAG_AUTO 0x01
#define DATA_TAG_HDLC 0x7E
#define DATA_TAG_LLC 0xE6
#define DATA_TAG_DLMS 0x0F
#define DATA_TAG_DSMR 0x2F
#define DATA_TAG_MBUS 0x68
#define DATA_TAG_GBT 0xE0
#define DATA_TAG_GCM 0xDB
#define DATA_TAG_SNRM 0x81
#define DATA_TAG_AARQ 0x60
#define DATA_TAG_AARE 0x61
#define DATA_TAG_RES 0xC4 // Get Response

#define DATA_PARSE_OK 0
#define DATA_PARSE_FAIL -1
#define DATA_PARSE_INCOMPLETE -2
#define DATA_PARSE_BOUNDRY_FLAG_MISSING -3
#define DATA_PARSE_HEADER_CHECKSUM_ERROR -4
#define DATA_PARSE_FOOTER_CHECKSUM_ERROR -5
#define DATA_PARSE_INTERMEDIATE_SEGMENT -6
#define DATA_PARSE_FINAL_SEGMENT -7
#define DATA_PARSE_UNKNOWN_DATA -9

struct DataParserContext {
    uint8_t type;
    uint16_t length;
    time_t timestamp;
    uint8_t system_title[8];
};

#endif
