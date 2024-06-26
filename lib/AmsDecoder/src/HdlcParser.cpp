/**
 * @copyright Utilitech AS 2023
 * License: Fair Source
 * 
 */

#include "HdlcParser.h"
#include "lwip/def.h"
#include "crc.h"

int8_t HDLCParser::parse(uint8_t *d, DataParserContext &ctx) {
    int len;

    uint8_t* ptr;
    if(ctx.length < 3)
        return DATA_PARSE_INCOMPLETE;

    HDLCHeader* h = (HDLCHeader*) d;
    ptr = (uint8_t*) &h[1];

    // Frame format type 3
    if((h->format & 0xF0) == 0xA0) {
        // Length field (11 lsb of format)
        len = (ntohs(h->format) & 0x7FF) + 2;
        if(len > ctx.length)
            return DATA_PARSE_INCOMPLETE;

        HDLCFooter* f = (HDLCFooter*) (d + len - sizeof *f);

        // First and last byte should be HDLC_FLAG
        if(h->flag != HDLC_FLAG || f->flag != HDLC_FLAG)
            return DATA_PARSE_BOUNDRY_FLAG_MISSING;

        // Verify FCS
        if(ntohs(f->fcs) != crc16_x25(d + 1, len - sizeof *f - 1))
            return DATA_PARSE_FOOTER_CHECKSUM_ERROR;

        // Skip destination address, LSB marks last byte
        while(((*ptr) & 0x01) == 0x00) {
            ptr++;
        }
        ptr++;

        // Skip source address, LSB marks last byte
        while(((*ptr) & 0x01) == 0x00) {
            ptr++;
        }
        ptr++;

        HDLC3CtrlHcs* t3 = (HDLC3CtrlHcs*) (ptr);

        // Verify HCS
        if(ntohs(t3->hcs) != crc16_x25(d + 1, ptr-d))
            return DATA_PARSE_HEADER_CHECKSUM_ERROR;
        ptr += 3;

        // Exclude all of header and 3 byte footer
        ctx.length -= ptr-d;
        if(ctx.length > 1) {
            ctx.length -= 3;
        }

        // Payload incomplete
        if((h->format & 0x08) == 0x08) {
            if(lastSequenceNumber == 0) {
                if(buf == NULL) buf = (uint8_t *)malloc((size_t)1024);
                pos = 0;
            }

            if(buf == NULL) return DATA_PARSE_FAIL;

            memcpy(buf + pos, ptr+3, ctx.length); // +3 to skip LLC
            pos += ctx.length;

            lastSequenceNumber++;
            return DATA_PARSE_INTERMEDIATE_SEGMENT;
        } else if(lastSequenceNumber > 0) {
            lastSequenceNumber = 0;
            if(buf == NULL) return DATA_PARSE_FAIL;

            memcpy(buf + pos, ptr+3, ctx.length); // +3 to skip LLC
            pos += ctx.length;

            memcpy((uint8_t *) d, buf, pos);
            free(buf);
            buf = NULL;
            ctx.length = pos;
            pos = 0;
            return DATA_PARSE_OK;
        } else {
            return ptr-d;
        }
    }
    return DATA_PARSE_UNKNOWN_DATA;
}