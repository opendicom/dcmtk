/*
 *
 *  Copyright (C) 2026, Open Connections GmbH
 *  All rights reserved.  See COPYRIGHT file for details.
 *
 *  This software and supporting documentation are maintained by
 *
 *    OFFIS e.V.
 *    R&D Division Health
 *    Escherweg 2
 *    D-26121 Oldenburg, Germany
 *
 *
 *  Module:  dcmseg
 *
 *  Author:  Michael Onken
 *
 *  Purpose: Tests for Pixel Padding Value / background handling in labelmap
 *           segmentations (Supplement 243).
 *
 */

#include "dcmtk/config/osconfig.h" /* make sure OS specific configuration is included first */

#include "dcmtk/dcmdata/dcdatset.h"
#include "dcmtk/dcmdata/dcdeftag.h"
#include "dcmtk/dcmdata/dcdict.h"
#include "dcmtk/dcmdata/dcfilefo.h"
#include "dcmtk/dcmdata/dcitem.h"
#include "dcmtk/dcmdata/dcsequen.h"
#include "dcmtk/dcmdata/dcvrss.h"
#include "dcmtk/dcmfg/fgfracon.h"
#include "dcmtk/dcmfg/fgpixmsr.h"
#include "dcmtk/dcmfg/fgplanor.h"
#include "dcmtk/dcmfg/fgplanpo.h"
#include "dcmtk/dcmiod/iodmacro.h"
#include "dcmtk/dcmiod/modmultiframedimension.h"
#include "dcmtk/dcmseg/segdoc.h"
#include "dcmtk/dcmseg/segment.h"
#include "dcmtk/dcmseg/segtypes.h"
#include "dcmtk/ofstd/ofmem.h"
#include "dcmtk/ofstd/oftempf.h"
#include "dcmtk/ofstd/oftest.h"
#include "dcmtk/ofstd/oftypes.h"
#include "dcmtk/ofstd/ofvector.h"

// Small dimensions keep these tests fast: 4 x 4 = 16 pixels, single frame.
static const Uint16 BG_ROWS   = 4;
static const Uint16 BG_COLS   = 4;
static const size_t BG_PIXELS = BG_ROWS * BG_COLS;

// All tests require the data dictionary. Fail (do not silently pass) when it is
// missing, so a misconfigured environment cannot mask a regression.
#define REQUIRE_DICT()                                                                  \
    do                                                                                  \
    {                                                                                   \
        if (!dcmDataDict.isDictionaryLoaded())                                          \
        {                                                                               \
            OFCHECK_FAIL("no data dictionary loaded, set " DCM_DICT_ENVIRONMENT_VARIABLE); \
            return;                                                                     \
        }                                                                               \
    } while (0)

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static void addMinimalDimensions(DcmSegmentation* seg);

/// Create a minimal, writable MONOCHROME2 labelmap (no segments, no frame)
static DcmSegmentation* createMiniLabelmap(const OFBool use16bit = OFFalse)
{
    IODGeneralEquipmentModule::EquipmentInfo eq("Open Connections", "OC SEG", "4711", "0.1");
    ContentIdentificationMacro ci("1", "LABEL", "DESCRIPTION", "Doe^John");
    DcmSegmentation* seg = NULL;
    OFCondition result   = DcmSegmentation::createLabelmapSegmentation(
        seg, BG_ROWS, BG_COLS, eq, ci, use16bit, DcmSegTypes::SLCM_MONOCHROME2);
    OFCHECK(result.good());
    if (!seg)
        return NULL;

    // Provide the attributes/functional groups required for a successful write
    seg->getFrameOfReference().setFrameOfReferenceUID("2.25.30853397773651184949181049330553108086");
    seg->getStudy().setStudyInstanceUID("1.2.276.0.7230010.3.1.2.8323329.14863.1565940357.864811");
    seg->getSeries().setSeriesInstanceUID("1.2.276.0.7230010.3.1.3.8323329.14863.1565940357.864812");
    seg->getSeries().setSeriesNumber("1");
    seg->getSOPCommon().setSOPInstanceUID("1.2.276.0.7230010.3.1.4.8323329.14863.1565940357.864813");

    FGPixelMeasures meas;
    meas.setPixelSpacing("0.1\\0.1");
    meas.setSliceThickness("1.0");
    meas.setSpacingBetweenSlices("0.05");
    FGPlanePosPatient planpo;
    planpo.setImagePositionPatient("0.0", "0.0", "0.0");
    FGPlaneOrientationPatient planor;
    planor.setImageOrientationPatient("1.0", "0.0", "0.0", "0.0", "1.0", "0.0");
    seg->addForAllFrames(meas);
    seg->addForAllFrames(planpo);
    seg->addForAllFrames(planor);
    addMinimalDimensions(seg);
    return seg;
}

/// Add a minimal but complete Multi-frame Dimension Module
static void addMinimalDimensions(DcmSegmentation* seg)
{
    IODMultiframeDimensionModule& dims = seg->getDimensions();
    dims.addDimensionIndex(DCM_StackID, "2.25.99887766554433221100", DCM_FrameContentSequence, "STACK_DIM");
    dims.addDimensionIndex(
        DCM_InStackPositionNumber, "2.25.99887766554433221100", DCM_FrameContentSequence, "STACK_DIM");
    OFunique_ptr<IODMultiframeDimensionModule::DimensionOrganizationItem> org(
        new IODMultiframeDimensionModule::DimensionOrganizationItem);
    org->setDimensionOrganizationUID("2.25.99887766554433221100");
    dims.getDimensionOrganizationSequence().push_back(org.release());
}

/// Add an ordinary (non-background) segment at the given labelmap number
static void addRegularSegment(DcmSegmentation* seg, const Uint16 number)
{
    DcmSegment* segment = NULL;
    CodeSequenceMacro category("85756007", "SCT", "Tissue");
    CodeSequenceMacro propType("51114001", "SCT", "Artery");
    OFCHECK(DcmSegment::create(segment, "SEG", category, propType, DcmSegTypes::SAT_AUTOMATIC, "OC").good());
    Uint16 n = number;
    OFCHECK(seg->addSegment(segment, n).good());
}

/// Add a single 8-bit frame whose 16 pixels are filled by repeating 'values'
static void addFrame8(DcmSegmentation* seg, const OFVector<Uint8>& values)
{
    FGFrameContent* fg = new FGFrameContent();
    fg->setStackID("1");
    fg->setInStackPositionNumber(1);
    fg->setDimensionIndexValues(1, 0);
    fg->setDimensionIndexValues(1, 1);
    OFVector<FGBase*> perFrame;
    perFrame.push_back(fg);
    Uint8 data[BG_PIXELS];
    for (size_t i = 0; i < BG_PIXELS; i++)
        data[i] = values[i % values.size()];
    OFCHECK(seg->addFrame<Uint8>(data, 0, perFrame).good());
    delete fg;
}

/// Add a single 16-bit frame whose 16 pixels are filled by repeating 'values'
static void addFrame16(DcmSegmentation* seg, const OFVector<Uint16>& values)
{
    FGFrameContent* fg = new FGFrameContent();
    fg->setStackID("1");
    fg->setInStackPositionNumber(1);
    fg->setDimensionIndexValues(1, 0);
    fg->setDimensionIndexValues(1, 1);
    OFVector<FGBase*> perFrame;
    perFrame.push_back(fg);
    Uint16 data[BG_PIXELS];
    for (size_t i = 0; i < BG_PIXELS; i++)
        data[i] = values[i % values.size()];
    OFCHECK(seg->addFrame<Uint16>(data, 0, perFrame).good());
    delete fg;
}

static OFVector<Uint8> values3(const Uint8 a, const Uint8 b, const Uint8 c)
{
    OFVector<Uint8> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    return v;
}

/// Return OFTrue if Segment Sequence contains an item with the given Segment
/// Number whose Segmented Property Type Code is (DCM,125040,"Background").
static OFBool hasBackgroundSegment(DcmDataset* dset, const Uint16 number)
{
    DcmSequenceOfItems* seq = NULL;
    if (dset->findAndGetSequence(DCM_SegmentSequence, seq).bad() || (seq == NULL))
        return OFFalse;
    for (unsigned long i = 0; i < seq->card(); i++)
    {
        DcmItem* item = seq->getItem(i);
        if (item == NULL)
            continue;
        Uint16 segNum = 0xffff;
        if (item->findAndGetUint16(DCM_SegmentNumber, segNum).good() && (segNum == number))
        {
            DcmItem* typeItem = NULL;
            if (item->findAndGetSequenceItem(DCM_SegmentedPropertyTypeCodeSequence, typeItem, 0).good()
                && (typeItem != NULL))
            {
                OFString codeValue, designator;
                typeItem->findAndGetOFString(DCM_CodeValue, codeValue);
                typeItem->findAndGetOFString(DCM_CodingSchemeDesignator, designator);
                return (codeValue == "125040") && (designator == "DCM");
            }
        }
    }
    return OFFalse;
}

static unsigned long segmentCount(DcmDataset* dset)
{
    DcmSequenceOfItems* seq = NULL;
    if (dset->findAndGetSequence(DCM_SegmentSequence, seq).good() && (seq != NULL))
        return seq->card();
    return 0;
}

// Return the Segmented Property Category Code value of the segment with the given
// Segment Number (empty string if not found).
static OFString segmentCategoryCode(DcmDataset* dset, const Uint16 number)
{
    DcmSequenceOfItems* seq = NULL;
    if (dset->findAndGetSequence(DCM_SegmentSequence, seq).bad() || (seq == NULL))
        return "";
    for (unsigned long i = 0; i < seq->card(); i++)
    {
        DcmItem* item = seq->getItem(i);
        if (item == NULL)
            continue;
        Uint16 segNum = 0xffff;
        if (item->findAndGetUint16(DCM_SegmentNumber, segNum).good() && (segNum == number))
        {
            DcmItem* cat = NULL;
            if (item->findAndGetSequenceItem(DCM_SegmentedPropertyCategoryCodeSequence, cat, 0).good()
                && (cat != NULL))
            {
                OFString cv;
                cat->findAndGetOFString(DCM_CodeValue, cv);
                return cv;
            }
        }
    }
    return "";
}

// Replace Pixel Padding Value in a saved file with a signed-short (SS) element of
// the given value, to simulate a (non-conformant) foreign object. Returns OFTrue
// on success.
static OFBool injectSignedPPV(const OFString& fn, const Sint16 value)
{
    DcmFileFormat ff;
    if (ff.loadFile(fn.c_str()).bad())
        return OFFalse;
    DcmDataset* ds = ff.getDataset();
    ds->findAndDeleteElement(DCM_PixelPaddingValue);
    DcmElement* elem = new DcmSignedShort(DcmTag(DCM_PixelPaddingValue, EVR_SS));
    if (elem->putSint16(value).bad())
    {
        delete elem;
        return OFFalse;
    }
    if (ds->insert(elem, OFTrue /*replaceOld*/).bad())
    {
        delete elem;
        return OFFalse;
    }
    return ff.saveFile(fn.c_str(), EXS_LittleEndianExplicit).good();
}

// -------------------------------------------------------------------------
// Tests
// -------------------------------------------------------------------------

// Default mode (when creating a labelmap from scratch) is BGH_AddSegment: an uncovered
// background value (0) leads to an automatically inserted Background segment + Pixel
// Padding Value.
OFTEST(dcmseg_labelmap_bg_implicit_add)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    OFCHECK(seg != NULL);
    if (!seg)
        return;
    OFCHECK(seg->getBackgroundHandlingMode() == DcmSegTypes::BGH_AddSegment);
    // Segments for 1 and 2 only; pixel value 0 is left "implicit" background
    addRegularSegment(seg, 1);
    addRegularSegment(seg, 2);
    addFrame8(seg, values3(0, 1, 2));

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    // Background segment (number 0) was added automatically
    OFCHECK(segmentCount(&ds) == 3);
    OFCHECK(hasBackgroundSegment(&ds, 0));
    // Default category is Spatial and Relational Concept (not "Background")
    OFCHECK(segmentCategoryCode(&ds, 0) == "309825002");
    // Pixel Padding Value (0028,0120) was written and equals 0
    Uint16 ppv = 0xffff;
    OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
    OFCHECK(ppv == 0);
    // Pixel Padding Range Limit must never be present
    OFCHECK(!ds.tagExists(DCM_PixelPaddingRangeLimit));
    delete seg;
}

// Same as above but for a 16-bit labelmap with non-contiguous label values
OFTEST(dcmseg_labelmap_bg_16bit)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap(OFTrue /* 16 bit */);
    if (!seg)
        return;
    addRegularSegment(seg, 300);
    addRegularSegment(seg, 1000);
    OFVector<Uint16> v;
    v.push_back(0);
    v.push_back(300);
    v.push_back(1000);
    addFrame16(seg, v); // value 0 is the implicit background

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    Uint16 ba = 0;
    OFCHECK(ds.findAndGetUint16(DCM_BitsAllocated, ba).good());
    OFCHECK(ba == 16);
    OFCHECK(hasBackgroundSegment(&ds, 0));
    Uint16 ppv = 0xffff;
    OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
    OFCHECK(ppv == 0);
    delete seg;
}

// setBackgroundPixelValue() eagerly inserts the Background segment and yields PPV
OFTEST(dcmseg_labelmap_bg_explicit)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    OFCHECK(seg->setBackgroundPixelValue(0).good());
    // The Background segment exists immediately (eager insertion), before write
    OFCHECK(seg->getSegment(0) != NULL);
    Uint16 bgNum = 0xffff;
    OFCHECK(seg->getBackgroundSegmentNumber(bgNum));
    OFCHECK(bgNum == 0);

    addRegularSegment(seg, 1);
    addRegularSegment(seg, 2);
    addFrame8(seg, values3(0, 1, 2));

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    OFCHECK(hasBackgroundSegment(&ds, 0));
    OFCHECK(segmentCategoryCode(&ds, 0) == "309825002"); // Spatial and Relational Concept
    Uint16 ppv = 0xffff;
    OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
    OFCHECK(ppv == 0);
    delete seg;
}

// Only a single Background segment is kept: changing the value moves it
OFTEST(dcmseg_labelmap_bg_single_invariant)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    OFCHECK(seg->setBackgroundPixelValue(0).good());
    OFCHECK(seg->getSegment(0) != NULL);
    // Move the background to value 5: the old Background segment (0) is dropped
    OFCHECK(seg->setBackgroundPixelValue(5).good());
    OFCHECK(seg->getSegment(0) == NULL);
    OFCHECK(seg->getSegment(5) != NULL);
    Uint16 bgNum = 0xffff;
    OFCHECK(seg->getBackgroundSegmentNumber(bgNum));
    OFCHECK(bgNum == 5);
    // Refuse to put the background on a value already used by another segment
    addRegularSegment(seg, 1);
    OFCHECK(seg->setBackgroundPixelValue(1).bad());
    delete seg;
}

// Custom Segmented Property Type/Category codes for the Background segment
OFTEST(dcmseg_labelmap_bg_custom_codes)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    CodeSequenceMacro type("125040", "DCM", "Background");
    CodeSequenceMacro category("91723000", "SCT", "Anatomical Structure");
    OFCHECK(seg->setBackgroundPixelValue(0, type, category).good());
    addRegularSegment(seg, 1);
    addFrame8(seg, values3(0, 1, 1));

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    OFCHECK(hasBackgroundSegment(&ds, 0)); // type code still (DCM,125040)
    // The custom category code was written
    DcmSequenceOfItems* seq = NULL;
    OFCHECK(ds.findAndGetSequence(DCM_SegmentSequence, seq).good());
    OFBool foundCategory = OFFalse;
    if (seq)
    {
        for (unsigned long i = 0; i < seq->card(); i++)
        {
            DcmItem* item   = seq->getItem(i);
            Uint16 segNum   = 0xffff;
            if (item && item->findAndGetUint16(DCM_SegmentNumber, segNum).good() && (segNum == 0))
            {
                DcmItem* cat = NULL;
                if (item->findAndGetSequenceItem(DCM_SegmentedPropertyCategoryCodeSequence, cat, 0).good() && cat)
                {
                    OFString cv;
                    cat->findAndGetOFString(DCM_CodeValue, cv);
                    foundCategory = (cv == "91723000");
                }
            }
        }
    }
    OFCHECK(foundCategory);
    delete seg;
}

// Pixel Padding Value leads: a PPV set directly (no Background segment, value not
// in the Pixel Data) is honored on write. Under BGH_AddSegment the missing
// Background segment is materialized.
OFTEST(dcmseg_labelmap_bg_ppv_leads_addsegment)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap(); // default mode BGH_AddSegment
    if (!seg)
        return;
    addRegularSegment(seg, 1);
    addRegularSegment(seg, 2);
    addFrame8(seg, values3(1, 2, 2)); // values 1,2 covered; 5 is not in the data
    // Designate value 5 as background via Pixel Padding Value only
    OFCHECK(seg->getEquipment().setPixelPaddingValue(OFstatic_cast(Uint16, 5)).good());

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    Uint16 ppv = 0xffff;
    OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
    OFCHECK(ppv == 5);
    OFCHECK(hasBackgroundSegment(&ds, 5)); // materialized under AddSegment
    delete seg;
}

// Pixel Padding Value leads, but under BGH_Warn a "foreign" object is not
// modified: the PPV is kept and a warning issued, no Background segment is added.
OFTEST(dcmseg_labelmap_bg_ppv_leads_warn)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    seg->setBackgroundHandlingMode(DcmSegTypes::BGH_Warn);
    addRegularSegment(seg, 1);
    addRegularSegment(seg, 2);
    addFrame8(seg, values3(1, 2, 2));
    OFCHECK(seg->getEquipment().setPixelPaddingValue(OFstatic_cast(Uint16, 5)).good());

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    Uint16 ppv = 0xffff;
    OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good()); // never dropped
    OFCHECK(ppv == 5);
    OFCHECK(!hasBackgroundSegment(&ds, 5)); // not added under Warn
    delete seg;
}

// BGH_Error: any uncovered pixel value makes writing fail
OFTEST(dcmseg_labelmap_bg_mode_error)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    seg->setBackgroundHandlingMode(DcmSegTypes::BGH_Error);
    addRegularSegment(seg, 1);
    addRegularSegment(seg, 2);
    addFrame8(seg, values3(0, 1, 2)); // value 0 uncovered

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).bad());
    delete seg;
}

// BGH_AddSegment bails out with an error if a non-background value is uncovered
OFTEST(dcmseg_labelmap_bg_nonzero_uncovered)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    // Default AddSegment mode; segments for 1 and 2 exist, but the frame also
    // contains 0 (background, could be auto-added) AND 7 (cannot be guessed).
    addRegularSegment(seg, 1);
    addRegularSegment(seg, 2);
    OFVector<Uint8> v;
    v.push_back(0);
    v.push_back(1);
    v.push_back(2);
    v.push_back(7);
    addFrame8(seg, v);

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).bad());
    delete seg;
}

// BGH_Warn: exactly one uncovered value is tolerated (written as-is, no PPV);
// two or more uncovered values fail.
OFTEST(dcmseg_labelmap_bg_mode_warn)
{
    REQUIRE_DICT();
    // (a) single uncovered value -> success, but no Background segment / no PPV
    {
        DcmSegmentation* seg = createMiniLabelmap();
        if (!seg)
            return;
        seg->setBackgroundHandlingMode(DcmSegTypes::BGH_Warn);
        addRegularSegment(seg, 1);
        addRegularSegment(seg, 2);
        addFrame8(seg, values3(0, 1, 2)); // only 0 uncovered

        DcmDataset ds;
        OFCHECK(seg->writeDataset(ds).good());
        OFCHECK(segmentCount(&ds) == 2);              // nothing added
        OFCHECK(!hasBackgroundSegment(&ds, 0));
        OFCHECK(!ds.tagExists(DCM_PixelPaddingValue)); // no background segment -> no PPV
        delete seg;
    }
    // (b) two uncovered values -> write fails
    {
        DcmSegmentation* seg = createMiniLabelmap();
        if (!seg)
            return;
        seg->setBackgroundHandlingMode(DcmSegTypes::BGH_Warn);
        addRegularSegment(seg, 1);
        OFVector<Uint8> v;
        v.push_back(0);
        v.push_back(1);
        v.push_back(7);
        addFrame8(seg, v); // 0 and 7 uncovered
        DcmDataset ds;
        OFCHECK(seg->writeDataset(ds).bad());
        delete seg;
    }
}

// Round-trip: Pixel Padding Value survives save/load and is interpreted on read
OFTEST(dcmseg_labelmap_bg_roundtrip)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    OFCHECK(seg->setBackgroundPixelValue(0).good());
    addRegularSegment(seg, 1);
    addRegularSegment(seg, 2);
    addFrame8(seg, values3(0, 1, 2));

    OFTempFile tf;
    OFString fn = tf.getFilename();
    OFCHECK(!fn.empty());
    OFCHECK(seg->saveFile(fn.c_str(), EXS_LittleEndianExplicit).good());
    delete seg;
    seg = NULL;

    // Re-load and verify the background was interpreted from Pixel Padding Value
    DcmSegmentation* loaded = NULL;
    OFCHECK(DcmSegmentation::loadFile(fn, loaded).good());
    OFCHECK(loaded != NULL);
    if (loaded)
    {
        OFCHECK(loaded->getBackgroundPixelValue() == 0);
        Uint16 bgNum = 0xffff;
        OFCHECK(loaded->getBackgroundSegmentNumber(bgNum));
        OFCHECK(bgNum == 0);
        // Loaded objects default to BGH_Warn
        OFCHECK(loaded->getBackgroundHandlingMode() == DcmSegTypes::BGH_Warn);
        // Writing again keeps the Pixel Padding Value
        DcmDataset ds;
        OFCHECK(loaded->writeDataset(ds).good());
        Uint16 ppv = 0xffff;
        OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
        OFCHECK(ppv == 0);
        OFCHECK(hasBackgroundSegment(&ds, 0));
        delete loaded;
    }
}

// BGH_Ignore: more than one uncovered value is written anyway (with warnings)
OFTEST(dcmseg_labelmap_bg_mode_ignore)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    seg->setBackgroundHandlingMode(DcmSegTypes::BGH_Ignore);
    addRegularSegment(seg, 1);
    // values 0 and 7 are both uncovered (only value 1 has a segment)
    OFVector<Uint8> v;
    v.push_back(0);
    v.push_back(1);
    v.push_back(7);
    addFrame8(seg, v);

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good()); // Ignore writes despite >1 uncovered
    OFCHECK(segmentCount(&ds) == 1);       // nothing added
    OFCHECK(!hasBackgroundSegment(&ds, 0));
    OFCHECK(!ds.tagExists(DCM_PixelPaddingValue)); // no background designated
    delete seg;
}

// Reading a "foreign" object: Pixel Padding Value present but no Background
// segment. The value is interpreted on read, preserved on re-write under the
// loaded default (BGH_Warn, no mutation), and the segment is materialized only on
// opt-in (BGH_AddSegment).
OFTEST(dcmseg_labelmap_bg_foreign_roundtrip)
{
    REQUIRE_DICT();
    // Build a foreign-style object: PPV=0 set directly, no Background segment,
    // value 0 present in the Pixel Data. BGH_Warn lets us write it as-is.
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    seg->setBackgroundHandlingMode(DcmSegTypes::BGH_Warn);
    addRegularSegment(seg, 1);
    addFrame8(seg, values3(0, 1, 1)); // value 0 uncovered
    OFCHECK(seg->getEquipment().setPixelPaddingValue(OFstatic_cast(Uint16, 0)).good());

    OFTempFile tf;
    OFString fn = tf.getFilename();
    OFCHECK(!fn.empty());
    OFCHECK(seg->saveFile(fn.c_str(), EXS_LittleEndianExplicit).good());
    delete seg;
    seg = NULL;

    // Read it back: Pixel Padding Value is interpreted, but no Background segment exists.
    DcmSegmentation* loaded = NULL;
    OFCHECK(DcmSegmentation::loadFile(fn, loaded).good());
    OFCHECK(loaded != OFnullptr);
    if (!loaded)
        return;
    OFCHECK(loaded->getBackgroundPixelValue() == 0);    // taken from Pixel Padding Value
    Uint16 bgNum = 0xffff;
    OFCHECK(!loaded->getBackgroundSegmentNumber(bgNum)); // no Background segment present
    OFCHECK(loaded->getBackgroundHandlingMode() == DcmSegTypes::BGH_Warn);

    // Re-write under the loaded default (Warn): PPV preserved, segment not added.
    {
        DcmDataset ds;
        OFCHECK(loaded->writeDataset(ds).good());
        Uint16 ppv = 0xffff;
        OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
        OFCHECK(ppv == 0);
        OFCHECK(!hasBackgroundSegment(&ds, 0));
    }
    // Opt in to repair: AddSegment materializes the missing Background segment.
    {
        loaded->setBackgroundHandlingMode(DcmSegTypes::BGH_AddSegment);
        DcmDataset ds;
        OFCHECK(loaded->writeDataset(ds).good());
        Uint16 ppv = 0xffff;
        OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
        OFCHECK(ppv == 0);
        OFCHECK(hasBackgroundSegment(&ds, 0));
    }
    delete loaded;
}

// Pixel Padding Value must not be written for non-Labelmap segmentations
OFTEST(dcmseg_labelmap_bg_nonlabelmap_strip)
{
    REQUIRE_DICT();
    IODGeneralEquipmentModule::EquipmentInfo eq("Open Connections", "OC SEG", "4711", "0.1");
    ContentIdentificationMacro ci("1", "LABEL", "DESCRIPTION", "Doe^John");
    DcmSegmentation* seg = NULL;
    OFCHECK(DcmSegmentation::createBinarySegmentation(seg, BG_ROWS, BG_COLS, eq, ci).good());
    if (!seg)
        return;
    seg->getFrameOfReference().setFrameOfReferenceUID("2.25.30853397773651184949181049330553108086");
    seg->getStudy().setStudyInstanceUID("1.2.276.0.7230010.3.1.2.8323329.14863.1565940357.864811");
    seg->getSeries().setSeriesInstanceUID("1.2.276.0.7230010.3.1.3.8323329.14863.1565940357.864812");
    seg->getSeries().setSeriesNumber("1");
    seg->getSOPCommon().setSOPInstanceUID("1.2.276.0.7230010.3.1.4.8323329.14863.1565940357.864813");
    seg->setCheckFGOnWrite(OFFalse);
    seg->setCheckDimensionsOnWrite(OFFalse);

    FGPixelMeasures meas;
    meas.setPixelSpacing("0.1\\0.1");
    meas.setSliceThickness("1.0");
    meas.setSpacingBetweenSlices("0.05");
    FGPlanePosPatient planpo;
    planpo.setImagePositionPatient("0.0", "0.0", "0.0");
    FGPlaneOrientationPatient planor;
    planor.setImageOrientationPatient("1.0", "0.0", "0.0", "0.0", "1.0", "0.0");
    seg->addForAllFrames(meas);
    seg->addForAllFrames(planpo);
    seg->addForAllFrames(planor);
    addMinimalDimensions(seg);

    DcmSegment* segment = NULL;
    CodeSequenceMacro category("85756007", "SCT", "Tissue");
    CodeSequenceMacro propType("51114001", "SCT", "Artery");
    OFCHECK(DcmSegment::create(segment, "SEG", category, propType, DcmSegTypes::SAT_AUTOMATIC, "OC").good());
    Uint16 segNum = 0;
    OFCHECK(seg->addSegment(segment, segNum).good());

    FGFrameContent* fg = new FGFrameContent();
    fg->setStackID("1");
    fg->setInStackPositionNumber(1);
    fg->setDimensionIndexValues(1, 0);
    fg->setDimensionIndexValues(1, 1);
    OFVector<FGBase*> perFrame;
    perFrame.push_back(fg);
    Uint8 data[BG_PIXELS];
    for (size_t i = 0; i < BG_PIXELS; i++)
        data[i] = (i % 2) ? 1 : 0;
    OFCHECK(seg->addFrame<Uint8>(data, segNum, perFrame).good());
    delete fg;

    // Manually set a (bogus) Pixel Padding Value; it must be stripped on write
    OFCHECK(seg->getEquipment().setPixelPaddingValue(OFstatic_cast(Uint16, 5)).good());

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    OFCHECK(!ds.tagExists(DCM_PixelPaddingValue));
    delete seg;
}

// A background change after a first write must not be reverted by the stale Pixel
// Padding Value element left from that write: the designated value leads.
OFTEST(dcmseg_labelmap_bg_rewrite_no_stale_ppv)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    addRegularSegment(seg, 1);
    addFrame8(seg, values3(1, 1, 1)); // only value 1 (covered); background not in data
    OFCHECK(seg->setBackgroundPixelValue(0).good());

    DcmDataset ds1;
    OFCHECK(seg->writeDataset(ds1).good()); // writes Pixel Padding Value 0
    Uint16 ppv1 = 0xffff;
    OFCHECK(ds1.findAndGetUint16(DCM_PixelPaddingValue, ppv1).good());
    OFCHECK(ppv1 == 0);

    // Move the background to 7 and write again. The stale element (0) must not win.
    OFCHECK(seg->setBackgroundPixelValue(7).good());
    DcmDataset ds2;
    OFCHECK(seg->writeDataset(ds2).good());
    Uint16 ppv2 = 0xffff;
    OFCHECK(ds2.findAndGetUint16(DCM_PixelPaddingValue, ppv2).good());
    OFCHECK(ppv2 == 7);                     // the current value, not the stale 0
    OFCHECK(hasBackgroundSegment(&ds2, 7)); // single background, moved to 7
    OFCHECK(!hasBackgroundSegment(&ds2, 0));
    OFCHECK(segmentCount(&ds2) == 2);       // background(7) + segment 1, no duplicate
    delete seg;
}

// A Pixel Padding Value that designates a non-Background-typed segment is written
// with a warning, not an error.
OFTEST(dcmseg_labelmap_bg_ppv_nonbackground_segment)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    addRegularSegment(seg, 3);        // an "Artery" segment at value 3
    addFrame8(seg, values3(3, 3, 3)); // all pixels covered by the Artery segment
    // Point Pixel Padding Value at the (non-background) Artery segment
    OFCHECK(seg->getEquipment().setPixelPaddingValue(OFstatic_cast(Uint16, 3)).good());

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good()); // warning only, not an error
    Uint16 ppv = 0xffff;
    OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good());
    OFCHECK(ppv == 3);
    OFCHECK(!hasBackgroundSegment(&ds, 3)); // it is the Artery segment, not Background
    delete seg;
}

// A signed (SS) Pixel Padding Value is non-conformant for a Labelmap and is
// normalized on reading: a non-negative value is converted to US and used, a
// negative value is removed.
OFTEST(dcmseg_labelmap_bg_signed_ppv)
{
    REQUIRE_DICT();
    // (a) non-negative SS -> converted to US and used as the background
    {
        DcmSegmentation* seg = createMiniLabelmap();
        if (!seg)
            return;
        OFCHECK(seg->setBackgroundPixelValue(5).good());
        addRegularSegment(seg, 1);
        OFVector<Uint8> v;
        v.push_back(5);
        v.push_back(1);
        v.push_back(1);
        addFrame8(seg, v); // value 5 = background, 1 = segment
        OFTempFile tf;
        OFString fn = tf.getFilename();
        OFCHECK(!fn.empty());
        OFCHECK(seg->saveFile(fn.c_str(), EXS_LittleEndianExplicit).good());
        delete seg;
        seg = NULL;
        OFCHECK(injectSignedPPV(fn, OFstatic_cast(Sint16, 5))); // rewrite PPV as SS

        DcmSegmentation* loaded = NULL;
        OFCHECK(DcmSegmentation::loadFile(fn, loaded).good());
        OFCHECK(loaded != NULL);
        if (loaded)
        {
            OFCHECK(loaded->getBackgroundPixelValue() == 5); // SS interpreted as background
            DcmDataset ds;
            OFCHECK(loaded->writeDataset(ds).good());
            Uint16 ppv = 0xffff;
            OFCHECK(ds.findAndGetUint16(DCM_PixelPaddingValue, ppv).good()); // US getter works
            OFCHECK(ppv == 5);
            DcmElement* e = NULL;
            OFCHECK(ds.findAndGetElement(DCM_PixelPaddingValue, e).good());
            OFCHECK((e != NULL) && (e->getVR() == EVR_US)); // written as US, not SS
            delete loaded;
        }
    }
    // (b) negative SS -> removed (no background, no Pixel Padding Value written)
    {
        DcmSegmentation* seg = createMiniLabelmap();
        if (!seg)
            return;
        addRegularSegment(seg, 1);
        addFrame8(seg, values3(1, 1, 1)); // only value 1, fully covered; no background
        OFTempFile tf;
        OFString fn = tf.getFilename();
        OFCHECK(!fn.empty());
        OFCHECK(seg->saveFile(fn.c_str(), EXS_LittleEndianExplicit).good());
        delete seg;
        seg = NULL;
        OFCHECK(injectSignedPPV(fn, OFstatic_cast(Sint16, -3)));

        DcmSegmentation* loaded = NULL;
        OFCHECK(DcmSegmentation::loadFile(fn, loaded).good());
        OFCHECK(loaded != NULL);
        if (loaded)
        {
            Uint16 bgNum = 0xffff;
            OFCHECK(!loaded->getBackgroundSegmentNumber(bgNum)); // invalid value -> no background
            DcmDataset ds;
            OFCHECK(loaded->writeDataset(ds).good());
            OFCHECK(!ds.tagExists(DCM_PixelPaddingValue)); // removed, not re-written
            delete loaded;
        }
    }
}

// When a read Pixel Padding Value designates a non-Background segment, moving the
// background must NOT delete that segment (never clobber a user segment).
OFTEST(dcmseg_labelmap_bg_move_keeps_nonbackground_segment)
{
    REQUIRE_DICT();
    // Build a file whose Pixel Padding Value designates a regular (Artery) segment.
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    seg->setBackgroundHandlingMode(DcmSegTypes::BGH_Warn);
    addRegularSegment(seg, 3);            // "Artery" at value 3
    addFrame8(seg, values3(3, 3, 3));     // all pixels value 3 (covered)
    OFCHECK(seg->getEquipment().setPixelPaddingValue(OFstatic_cast(Uint16, 3)).good());
    OFTempFile tf;
    OFString fn = tf.getFilename();
    OFCHECK(!fn.empty());
    OFCHECK(seg->saveFile(fn.c_str(), EXS_LittleEndianExplicit).good());
    delete seg;
    seg = NULL;

    DcmSegmentation* loaded = NULL;
    OFCHECK(DcmSegmentation::loadFile(fn, loaded).good());
    OFCHECK(loaded != NULL);
    if (loaded)
    {
        // PPV designated value 3, but segment 3 is the Artery (not Background).
        OFCHECK(loaded->getBackgroundPixelValue() == 3);
        OFCHECK(loaded->getSegment(3) != NULL);
        OFCHECK(!DcmSegmentation::isBackgroundSegment(loaded->getSegment(3)));
        // Move the background to value 5: the Artery at 3 must survive.
        OFCHECK(loaded->setBackgroundPixelValue(5).good());
        OFCHECK(loaded->getSegment(3) != NULL);                                // Artery preserved
        OFCHECK(!DcmSegmentation::isBackgroundSegment(loaded->getSegment(3)));
        OFCHECK(loaded->getSegment(5) != NULL);                                // new background
        OFCHECK(DcmSegmentation::isBackgroundSegment(loaded->getSegment(5)));
        OFCHECK(loaded->getBackgroundPixelValue() == 5);
        delete loaded;
    }
}

// Re-designating the same value with different codes refreshes the existing
// Background segment's codes in place (no duplicate segment, codes updated).
OFTEST(dcmseg_labelmap_bg_redesignate_refreshes_codes)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    OFCHECK(seg->setBackgroundPixelValue(0).good());                  // default codes
    OFCHECK(DcmSegmentation::isBackgroundSegment(seg->getSegment(0)));
    // Re-designate the same value 0 with a custom category code.
    CodeSequenceMacro type("125040", "DCM", "Background");
    CodeSequenceMacro category("91723000", "SCT", "Anatomical Structure");
    OFCHECK(seg->setBackgroundPixelValue(0, type, category).good());
    addRegularSegment(seg, 1);
    addFrame8(seg, values3(0, 1, 1));

    DcmDataset ds;
    OFCHECK(seg->writeDataset(ds).good());
    OFCHECK(segmentCount(&ds) == 2);                    // no duplicate Background segment
    OFCHECK(segmentCategoryCode(&ds, 0) == "91723000"); // category refreshed in place
    // Designating a value occupied by a non-Background segment is rejected.
    OFCHECK(seg->setBackgroundPixelValue(1).bad());
    delete seg;
}

// addSegment on an occupied Labelmap number replaces the segment (frees the
// old one, count unchanged) instead of leaking it.
OFTEST(dcmseg_labelmap_segment_replace)
{
    REQUIRE_DICT();
    DcmSegmentation* seg = createMiniLabelmap();
    if (!seg)
        return;
    addRegularSegment(seg, 1);                          // first segment at 1
    OFCHECK(seg->getNumberOfSegments() == 1);
    // Replace the segment at number 1 with a new, differently-typed one.
    DcmSegment* repl = NULL;
    CodeSequenceMacro category("85756007", "SCT", "Tissue");
    CodeSequenceMacro propType("125040", "DCM", "Background");
    OFCHECK(DcmSegment::create(repl, "REPLACED", category, propType, DcmSegTypes::SAT_AUTOMATIC, "OC").good());
    Uint16 n = 1;
    OFCHECK(seg->addSegment(repl, n).good());
    OFCHECK(seg->getNumberOfSegments() == 1);           // count unchanged (replace, not add)
    OFCHECK(seg->getSegment(1) == repl);                // new segment in place
    OFCHECK(DcmSegmentation::isBackgroundSegment(seg->getSegment(1))); // carries the new codes
    delete seg;
}
