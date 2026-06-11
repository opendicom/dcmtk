/*
 *
 *  Copyright (C) 2015-2026, Open Connections GmbH
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
 *  Module:  dcmseg
 *
 *  Author:  Michael Onken
 *
 *  Purpose: main test program
 *
 */

#include "dcmtk/config/osconfig.h"

#include "dcmtk/ofstd/oftest.h"

OFTEST_REGISTER(dcmseg_bigdim);
OFTEST_REGISTER(dcmseg_debugByte2Bin);
OFTEST_REGISTER(dcmseg_packBinaryFrame);
OFTEST_REGISTER(dcmseg_packAndUnpackBinaryFrame);
OFTEST_REGISTER(dcmseg_concatBinaryFrames);
OFTEST_REGISTER(dcmseg_extractBinaryFrames);
OFTEST_REGISTER(dcmseg_segments_add_get_binary);
OFTEST_REGISTER(dcmseg_segments_add_get_labelmap);
OFTEST_REGISTER(dcmseg_segments_get_segment_number);
OFTEST_REGISTER(dcmseg_segments_compat_layer);
OFTEST_REGISTER(dcmseg_segments_compat_layer_labelmap);
OFTEST_REGISTER(dcmseg_segments_many);
OFTEST_REGISTER(dcmseg_segments_sparse_labelmap);
OFTEST_REGISTER(dcmseg_segments_compat_caching);
OFTEST_REGISTER(dcmseg_roundtrip);
OFTEST_REGISTER(dcmseg_concat_binary);
OFTEST_REGISTER(dcmseg_labelmap_8bit_mono2);
OFTEST_REGISTER(dcmseg_labelmap_16bit_mono2);
OFTEST_REGISTER(dcmseg_labelmap_8bit_palette);
OFTEST_REGISTER(dcmseg_labelmap_16bit_palette);
OFTEST_REGISTER(dcmseg_labelmap_bg_implicit_add);
OFTEST_REGISTER(dcmseg_labelmap_bg_16bit);
OFTEST_REGISTER(dcmseg_labelmap_bg_explicit);
OFTEST_REGISTER(dcmseg_labelmap_bg_single_invariant);
OFTEST_REGISTER(dcmseg_labelmap_bg_custom_codes);
OFTEST_REGISTER(dcmseg_labelmap_bg_ppv_leads_addsegment);
OFTEST_REGISTER(dcmseg_labelmap_bg_ppv_leads_warn);
OFTEST_REGISTER(dcmseg_labelmap_bg_mode_error);
OFTEST_REGISTER(dcmseg_labelmap_bg_nonzero_uncovered);
OFTEST_REGISTER(dcmseg_labelmap_bg_mode_warn);
OFTEST_REGISTER(dcmseg_labelmap_bg_mode_ignore);
OFTEST_REGISTER(dcmseg_labelmap_bg_foreign_roundtrip);
OFTEST_REGISTER(dcmseg_labelmap_bg_roundtrip);
OFTEST_REGISTER(dcmseg_labelmap_bg_nonlabelmap_strip);
OFTEST_REGISTER(dcmseg_labelmap_bg_rewrite_no_stale_ppv);
OFTEST_REGISTER(dcmseg_labelmap_bg_ppv_nonbackground_segment);
OFTEST_REGISTER(dcmseg_labelmap_bg_signed_ppv);
OFTEST_REGISTER(dcmseg_labelmap_bg_move_keeps_nonbackground_segment);
OFTEST_REGISTER(dcmseg_labelmap_bg_redesignate_refreshes_codes);
OFTEST_REGISTER(dcmseg_labelmap_segment_replace);
OFTEST_MAIN("dcmseg")
