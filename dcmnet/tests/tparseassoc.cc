/*
 *
 *  Copyright (C) 2026, OFFIS e.V.
 *  All rights reserved.  See COPYRIGHT file for details.
 *
 *  This software and supporting documentation were developed by
 *
 *    OFFIS e.V.
 *    R&D Division Health
 *    Escherweg 2
 *    D-26121 Oldenburg, Germany
 *
 *
 *  Module:  dcmnet
 *
 *  Author:  Michael Onken
 *
 *  Purpose: tests for parseAssociate() error-path cleanup of the
 *           extended negotiation sub-item list (extNegList).
 *
 */


#include "dcmtk/config/osconfig.h"    /* make sure OS specific configuration is included first */

#include "dcmtk/ofstd/oftest.h"
#include "dcmtk/dcmnet/dulstruc.h"
#include "dcmtk/dcmnet/extneg.h"

/* parseAssociate is declared in the private dcmnet header. The test reaches
 * into libsrc/ deliberately to whitebox-test the PDU parser without going
 * through a real TCP association. */
#include "../libsrc/dulpriv.h"

/* ---------------------------------------------------------------------------
 *  PDU layout constants
 *
 *  An A-ASSOCIATE-RQ PDU is structured as:
 *      offset 0   : PDU type            (1 byte,  0x01)
 *      offset 1   : reserved            (1 byte,  0x00)
 *      offset 2   : PDU length          (4 bytes, big-endian) — counts only
 *                                        the bytes AFTER this field, i.e. it
 *                                        excludes the 6-byte preamble above.
 *      offset 6   : protocol version    (2 bytes)
 *      offset 8   : reserved            (2 bytes)
 *      offset 10  : called AE title     (16 bytes, padded with spaces)
 *      offset 26  : calling AE title    (16 bytes, padded with spaces)
 *      offset 42  : reserved            (32 bytes, zeros)
 *      offset 74  : variable items follow (presentation contexts, user info, ...)
 *
 *  Variable items (including the User Info sub-PDU and each sub-item inside
 *  it) share a 4-byte sub-PDU header:
 *      byte 0     : item type           (1 byte)
 *      byte 1     : reserved            (1 byte, 0x00)
 *      byte 2-3   : item length         (2 bytes, big-endian) — counts only
 *                                        the bytes AFTER this field.
 *
 *  A SOP Class Extended Negotiation sub-item (item type 0x56) has, after that
 *  4-byte header, a mandatory 2-byte sopClassUIDLength field; with an empty
 *  UID and no serviceClassAppInfo bytes that yields a 6-byte minimum.
 * ------------------------------------------------------------------------- */

static const unsigned long  ASSOC_RQ_HEADER_BYTES        = 74; // fixed header up to offset 74
static const unsigned long  PDU_PREAMBLE_BYTES           = 6;  // type + rsv + 4-byte length
static const unsigned long  SUBPDU_HEADER_BYTES          = 4;  // type + rsv + 2-byte length
static const unsigned short EXT_NEG_MIN_BODY_BYTES       = 2;  // sopClassUIDLength field only
static const unsigned long  EXT_NEG_MIN_SUBITEM_BYTES    = SUBPDU_HEADER_BYTES + EXT_NEG_MIN_BODY_BYTES; // 6


/* Write a 16-bit big-endian value into buf and advance the cursor. */
static void put_u16_be(unsigned char *&p, unsigned short v)
{
    *p++ = OFstatic_cast(unsigned char, (v >> 8) & 0xff);
    *p++ = OFstatic_cast(unsigned char, v & 0xff);
}

/* Write a 32-bit big-endian value into buf and advance the cursor. */
static void put_u32_be(unsigned char *&p, unsigned long v)
{
    *p++ = OFstatic_cast(unsigned char, (v >> 24) & 0xff);
    *p++ = OFstatic_cast(unsigned char, (v >> 16) & 0xff);
    *p++ = OFstatic_cast(unsigned char, (v >> 8) & 0xff);
    *p++ = OFstatic_cast(unsigned char, v & 0xff);
}

/* Write the fixed-size A-ASSOCIATE-RQ header (ASSOC_RQ_HEADER_BYTES = 74,
 * see the layout comment above). Variable items follow at offset 74.
 * Returns the cursor positioned at the first variable-item byte.
 * `pduPayloadLen` is the value placed into the PDU length field, which by the
 * spec excludes the first PDU_PREAMBLE_BYTES (= 6) of the PDU. */
static unsigned char *write_assoc_rq_header(unsigned char *buf, unsigned long pduPayloadLen)
{
    const int AE_TITLE_BYTES          = 16; // called/calling AET fields (each)
    const int TRAILING_RESERVED_BYTES = 32; // reserved block after the AETs
    // Each AE-title literal is exactly AE_TITLE_BYTES (16) characters so no
    // padding is required and the field contents look like a realistic
    // upper-case ASCII AET rather than an all-spaces placeholder.
    const char *calledAE  = "DCMNETTESTCALLED";
    const char *callingAE = "DCMNETTESTCALLER";

    unsigned char *p = buf;
    *p++ = 0x01;                              // PDU type: A-ASSOCIATE-RQ
    *p++ = 0x00;                              // reserved
    put_u32_be(p, pduPayloadLen);        // PDU length (excludes PDU_PREAMBLE_BYTES)
    put_u16_be(p, DUL_PROTOCOL);              // protocol version
    *p++ = 0x00; *p++ = 0x00;                 // reserved
    for (int i = 0; i < AE_TITLE_BYTES; ++i) *p++ = OFstatic_cast(unsigned char, calledAE[i]);
    for (int i = 0; i < AE_TITLE_BYTES; ++i) *p++ = OFstatic_cast(unsigned char, callingAE[i]);
    for (int i = 0; i < TRAILING_RESERVED_BYTES; ++i) *p++ = 0x00;  // reserved
    return p;
}


/* Regression test for the primary leak from DCMTK issue #1216:
 *   destroyUserInformationLists() freed only the OFList container, orphaning
 *   the SOPClassExtendedNegotiationSubItem* members already pushed into
 *   userInfo->extNegList when parseExtNeg() failed late in the user-info loop.
 *
 * Payload: N valid 6-byte 0x56 sub-items followed by one truncated sub-item
 * (5 bytes), which makes parseExtNeg() reject the last entry at availData < 6.
 * Pre-fix this leaks all N already-parsed items + their serviceClassAppInfo
 * buffers on every malicious association. Functional assertion below catches
 * a regression in the cleanup logic (e.g. accidental double-free); the leak
 * itself is only directly observable under LeakSanitizer (e.g. by building with
 * DCMTK_WITH_SANITIZERS=ON).
 */
OFTEST(dcmnet_parseAssociate_extNeg_truncated)
{
    const int validItems = 10;
    // Each valid sub-item is EXT_NEG_MIN_SUBITEM_BYTES (6) on the wire; the
    // truncated one is intentionally one byte short to trip parseExtNeg's
    // availData < 6 check.
    const unsigned long validBytes = OFstatic_cast(unsigned long, validItems) * EXT_NEG_MIN_SUBITEM_BYTES;
    const unsigned long truncBytes = EXT_NEG_MIN_SUBITEM_BYTES - 1;
    const unsigned short userInfoPayload = OFstatic_cast(unsigned short, validBytes + truncBytes);
    // totalLen = fixed A-ASSOCIATE-RQ header + User Info sub-PDU header + payload.
    const unsigned long totalLen = ASSOC_RQ_HEADER_BYTES + SUBPDU_HEADER_BYTES + userInfoPayload;
    // The PDU length field excludes the leading PDU_PREAMBLE_BYTES.
    const unsigned long pduPayloadLen = totalLen - PDU_PREAMBLE_BYTES;

    unsigned char *buf = new unsigned char[totalLen];
    unsigned char *p = write_assoc_rq_header(buf, pduPayloadLen);

    // User Info sub-PDU header: type + reserved + 2-byte item length = SUBPDU_HEADER_BYTES.
    *p++ = DUL_TYPEUSERINFO;
    *p++ = 0x00;
    put_u16_be(p, userInfoPayload);

    // N valid extended-negotiation sub-items: itemLength = EXT_NEG_MIN_BODY_BYTES
    // (just the 2-byte sopClassUIDLength field), sopClassUIDLength = 0; so each
    // sub-item occupies exactly EXT_NEG_MIN_SUBITEM_BYTES on the wire.
    for (int i = 0; i < validItems; ++i) {
        *p++ = DUL_TYPESOPCLASSEXTENDEDNEGOTIATION;
        *p++ = 0x00;
        put_u16_be(p, EXT_NEG_MIN_BODY_BYTES);   // itemLength
        put_u16_be(p, 0);                        // sopClassUIDLength
    }

    // One truncated sub-item: claims itemLength = EXT_NEG_MIN_BODY_BYTES but
    // delivers only EXT_NEG_MIN_SUBITEM_BYTES - 1 bytes, so parseExtNeg trips
    // availData < 6 AFTER the outer loop has already pushed `validItems`
    // complete sub-items into extNegList — that is the leaked set.
    *p++ = DUL_TYPESOPCLASSEXTENDEDNEGOTIATION;  // type    (1)
    *p++ = 0x00;                                 // reserved (1)
    *p++ = 0x00; *p++ = 0x02;                    // itemLength = 2 (2)
    *p++ = 0x00;                                 // sopClassUIDLength: missing
                                                 // one byte to deliberately truncate

    OFCHECK_EQUAL(OFstatic_cast(unsigned long, p - buf), totalLen);

    PRV_ASSOCIATEPDU assoc;
    OFCondition cond = parseAssociate(buf, pduPayloadLen, &assoc);

    // Must reject the malformed PDU.
    OFCHECK(cond.bad());
    // Cleanup ran: list container was freed and pointer cleared.
    OFCHECK(assoc.userInfo.extNegList == NULL);

    delete[] buf;
}


/* Regression test for another leak tracked as DCMTK issue #TODO:
 *   parseUserInfo() allocates a SOPClassExtendedNegotiationSubItem* stub
 *   before invoking parseExtNeg(). When parseExtNeg() returned an error the
 *   stub was never pushed to extNegList and not freed either.
 *
 * Payload: a single 6-byte 0x56 sub-item with itemLength=1, which trips the
 * "itemLength < 2" check inside parseExtNeg() AFTER the stub allocation but
 * BEFORE serviceClassAppInfo is allocated. Before fixing the code leaked one
 * stub per call. Functional assertions below only catch a regression in the
 * cleanup logic (e.g. accidental double-free); the leak itself is only
 * directly observable under LeakSanitizer (build with
 * DCMTK_WITH_SANITIZERS=ON on Linux).
 */
OFTEST(dcmnet_parseAssociate_extNeg_malformed_itemLength)
{
    // The User Info payload is one minimum-size extended-negotiation sub-item
    // (EXT_NEG_MIN_SUBITEM_BYTES on the wire); the payload value is a uint16
    // because that is what the sub-PDU length field carries.
    const unsigned short userInfoPayload = OFstatic_cast(unsigned short, EXT_NEG_MIN_SUBITEM_BYTES);
    const unsigned long totalLen = ASSOC_RQ_HEADER_BYTES + SUBPDU_HEADER_BYTES + userInfoPayload;
    const unsigned long pduPayloadLen = totalLen - PDU_PREAMBLE_BYTES;

    unsigned char *buf = new unsigned char[totalLen];
    unsigned char *p = write_assoc_rq_header(buf, pduPayloadLen);

    *p++ = DUL_TYPEUSERINFO;
    *p++ = 0x00;
    put_u16_be(p, userInfoPayload);

    // Malformed extended-negotiation sub-item: itemLength = 1 violates the
    // (itemLength >= EXT_NEG_MIN_BODY_BYTES) invariant in parseExtNeg.
    *p++ = DUL_TYPESOPCLASSEXTENDEDNEGOTIATION;
    *p++ = 0x00;
    put_u16_be(p, 1);                            // itemLength = 1 (invalid; < EXT_NEG_MIN_BODY_BYTES)
    put_u16_be(p, 0);                            // sopClassUIDLength

    OFCHECK_EQUAL(OFstatic_cast(unsigned long, p - buf), totalLen);

    PRV_ASSOCIATEPDU assoc;
    OFCondition cond = parseAssociate(buf, pduPayloadLen, &assoc);

    OFCHECK(cond.bad());
    // The malformed item never reached the list, so it stays NULL.
    OFCHECK(assoc.userInfo.extNegList == NULL);

    delete[] buf;
}
