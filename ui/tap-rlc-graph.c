/* tap-rlc-graph.c
 * LTE RLC channel graph info
 *
 * Originally from tcp_graph.c by Pavel Mores <pvl@uh.cz>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <stdlib.h>

#include "tap-rlc-graph.h"

#include <file.h>

#include <epan/epan_dissect.h>
#include <epan/tap.h>

/* Return true if the 2 sets of parameters refer to the same channel. */
bool compare_rlc_headers(uint8_t rat1, uint8_t rat2,
                             uint16_t ueid1, uint16_t channelType1, uint16_t channelId1, uint8_t rlcMode1, uint8_t direction1,
                             uint16_t ueid2, uint16_t channelType2, uint16_t channelId2, uint8_t rlcMode2, uint8_t direction2,
                             bool frameIsControl)
{
    if (rat1 != rat2) {
        return false;
    }

    /* Same direction, data - OK. */
    if (!frameIsControl) {
        return (direction1 == direction2) &&
               (ueid1 == ueid2) &&
               (channelType1 == channelType2) &&
               (channelId1 == channelId2) &&
               (rlcMode1 == rlcMode2);
    }
    else {
        /* Control case */
        if ((rlcMode1 == RLC_AM_MODE) && (rlcMode2 == RLC_AM_MODE)) {
            return ((direction1 != direction2) &&
                    (ueid1 == ueid2) &&
                    (channelType1 == channelType2) &&
                    (channelId1 == channelId2));
        }
        else {
            return false;
        }
    }
}

/* This is the tap function used to identify a list of channels found in the current frame.  It is only used for the single,
   currently selected frame. */
static tap_packet_status
tap_lte_rlc_packet(void *pct, packet_info *pinfo _U_, epan_dissect_t *edt _U_, const void *vip, tap_flags_t flags _U_)
{
    int       n;
    bool      is_unique = true;
    th_t     *th        = (th_t *)pct;
    const rlc_3gpp_tap_info *header = (const rlc_3gpp_tap_info*)vip;

    /* Check new header details against any/all stored ones */
    for (n=0; n < th->num_hdrs; n++) {
        rlc_3gpp_tap_info *stored = th->rlchdrs[n];

        if (compare_rlc_headers(stored->rat, header->rat,
                                stored->ueid, stored->channelType, stored->channelId, stored->rlcMode, stored->direction,
                                header->ueid, header->channelType, header->channelId, header->rlcMode, header->direction,
                                header->isControlPDU)) {
            is_unique = false;
            break;
        }
    }

    /* Add address if unique and have space for it */
    if (is_unique && (th->num_hdrs < MAX_SUPPORTED_CHANNELS)) {
        /* Copy the tap struct in as next header */
        /* Need to take a deep copy of the tap struct, it may not be valid
           to read after this function returns? */
        th->rlchdrs[th->num_hdrs] = g_new(rlc_3gpp_tap_info,1);
        *(th->rlchdrs[th->num_hdrs]) = *header;

        /* Store in direction of data though... */
        if (th->rlchdrs[th->num_hdrs]->isControlPDU) {
            th->rlchdrs[th->num_hdrs]->direction = !th->rlchdrs[th->num_hdrs]->direction;
        }
        th->num_hdrs++;
    }

    return TAP_PACKET_DONT_REDRAW; /* i.e. no immediate redraw requested */
}

/* Return an array of tap_info structs that were found while dissecting the current frame
 * in the packet list. Errors are passed back to the caller, as they will be reported differently
 * depending upon which GUI toolkit is being used. */
rlc_3gpp_tap_info* select_rlc_lte_session(capture_file *cf,
                                          struct rlc_segment *hdrs,
                                          char **err_msg)
{
    frame_data     *fdata;
    wtap_rec        rec;
    epan_dissect_t  edt;
    dfilter_t      *sfcode;

    GString        *error_string;
    nstime_t        rel_ts;
    /* Initialised to no known channels */
    th_t            th = {0, {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}};

    if (cf->state == FILE_CLOSED) {
        return NULL;
    }

    /* No real filter yet */
    if (!dfilter_compile("rlc-lte or rlc-nr", &sfcode, NULL)) {
        return NULL;
    }

    fdata = cf->current_frame;

    /* Dissect the data from the current frame. */
    wtap_rec_init(&rec, 1514);
    if (!cf_read_current_record(cf)) {
        dfilter_free(sfcode);
        wtap_rec_cleanup(&rec);
        return NULL;  /* error reading the record */
    }

    /* Set tap listener that will populate th. */
    error_string = register_tap_listener("rlc-3gpp", &th, NULL, 0, NULL, tap_lte_rlc_packet, NULL, NULL);
    if (error_string){
        fprintf(stderr, "wireshark: Couldn't register rlc_lte_graph tap: %s\n",
                error_string->str);
        g_string_free(error_string, TRUE);
        dfilter_free(sfcode);
        return NULL;
    }

    epan_dissect_init(&edt, cf->epan, true, false);
    epan_dissect_prime_with_dfilter(&edt, sfcode);
    epan_dissect_run_with_taps(&edt, cf->cd_t, &cf->rec, fdata, NULL);
    rel_ts = edt.pi.rel_ts;
    epan_dissect_cleanup(&edt);
    remove_tap_listener(&th);

    if (th.num_hdrs == 0){
        /* This "shouldn't happen", as the graph menu items won't
         * even be enabled if the selected packet isn't an RLC PDU.
         */
        wtap_rec_cleanup(&rec);
        *err_msg = g_strdup("Selected packet doesn't have an RLC PDU");
        return NULL;
    }
    /* XXX fix this later, we should show a dialog allowing the user
     * to select which session he wants here */
    if (th.num_hdrs>1){
        /* Can only handle a single RLC channel yet */
        wtap_rec_cleanup(&rec);
        *err_msg = g_strdup("The selected packet has more than one LTE RLC channel in it.");
        return NULL;
    }

    /* For now, still always choose the first/only one */
    hdrs->num = fdata->num;
    hdrs->rel_secs = rel_ts.secs;
    hdrs->rel_usecs = rel_ts.nsecs/1000;

    hdrs->rat = th.rlchdrs[0]->rat;
    hdrs->ueid = th.rlchdrs[0]->ueid;
    hdrs->channelType = th.rlchdrs[0]->channelType;
    hdrs->channelId = th.rlchdrs[0]->channelId;
    hdrs->rlcMode = th.rlchdrs[0]->rlcMode;
    hdrs->isControlPDU = th.rlchdrs[0]->isControlPDU;
    /* Flip direction if have control PDU */
    hdrs->direction = !hdrs->isControlPDU ? th.rlchdrs[0]->direction : !th.rlchdrs[0]->direction;

    wtap_rec_cleanup(&rec);

    return th.rlchdrs[0];
}

/* This is the tapping function to update stats when dissecting the whole packet list */
static tap_packet_status rlc_lte_tap_for_graph_data(void *pct, packet_info *pinfo, epan_dissect_t *edt _U_, const void *vip, tap_flags_t flags _U_)
{
    struct rlc_graph *graph  = (struct rlc_graph *)pct;
    const rlc_3gpp_tap_info *rlchdr = (const rlc_3gpp_tap_info*)vip;

    /* See if this one matches graph's channel */
    if (compare_rlc_headers(graph->rat,    rlchdr->rat,
                            graph->ueid,   graph->channelType,  graph->channelId,  graph->rlcMode,   graph->direction,
                            rlchdr->ueid,  rlchdr->channelType, rlchdr->channelId, rlchdr->rlcMode,  rlchdr->direction,
                            rlchdr->isControlPDU)) {

        /* It matches.  Copy segment details out of tap struct */
        struct rlc_segment *segment = g_new(struct rlc_segment, 1);
        segment->next = NULL;
        segment->num = pinfo->num;
        segment->rel_secs = (uint32_t) pinfo->rel_ts.secs;
        segment->rel_usecs = pinfo->rel_ts.nsecs/1000;

        segment->rat = rlchdr->rat;
        segment->ueid = rlchdr->ueid;
        segment->channelType = rlchdr->channelType;
        segment->channelId = rlchdr->channelId;
        segment->direction = rlchdr->direction;
        segment->rlcMode = rlchdr->rlcMode;
        segment->sequenceNumberLength = rlchdr->sequenceNumberLength;

        segment->isControlPDU = rlchdr->isControlPDU;

        if (!rlchdr->isControlPDU) {
            if (rlchdr->sequenceNumberGiven) {
                /* Data */
                segment->SN = rlchdr->sequenceNumber;
                segment->isResegmented = rlchdr->isResegmented;
                segment->pduLength = rlchdr->pduLength;
            }
            else {
                /* No sequence number, so not going to show at all */
                g_free(segment);
                return TAP_PACKET_DONT_REDRAW; /* i.e. no immediate redraw requested */
            }
        }
        else {
            /* Status PDU */
            int n;
            segment->ACKNo = rlchdr->ACKNo;
            segment->noOfNACKs = rlchdr->noOfNACKs;
            for (n=0; (n < rlchdr->noOfNACKs) && (n < MAX_NACKs); n++) {
                segment->NACKs[n] = rlchdr->NACKs[n];
            }
        }

        /* Add segment to end of list */
        if (graph->segments) {
            /* Add to end of existing last element */
            graph->last_segment->next = segment;
        } else {
            /* Make this the first (only) segment */
            graph->segments = segment;
        }

        /* This one is now the last one */
        graph->last_segment = segment;
    }

    return TAP_PACKET_DONT_REDRAW; /* i.e. no immediate redraw requested */
}

/* If don't have a channel, try to get one from current frame, then read all frames looking for data
 * for that channel. */
bool rlc_graph_segment_list_get(capture_file *cf, struct rlc_graph *g, bool stream_known,
                                    char **err_string)
{
    struct rlc_segment current;
    GString    *error_string;

    if (!cf || !g) {
        /* Really shouldn't happen */
        return false;
    }

    if (!stream_known) {
        struct rlc_3gpp_tap_info *header = select_rlc_lte_session(cf, &current, err_string);
        if (!header) {
            /* Didn't have a channel, and current frame didn't provide one */
            return false;
        }
        g->channelSet = true;

        g->rat = header->rat;
        g->ueid = header->ueid;
        g->channelType = header->channelType;
        g->channelId = header->channelId;
        g->rlcMode = header->rlcMode;
        g->direction = header->direction;
    }


    /* Rescan all the packets and pick up all interesting RLC headers.
     * We only filter for RLC frames here for speed and do the actual compare
     * in the tap listener
     */

    g->last_segment = NULL;
    error_string = register_tap_listener("rlc-3gpp",           // tap name
                                         g,
                                         "rlc-lte or rlc-nr", // filter name
                                         0, NULL, rlc_lte_tap_for_graph_data, NULL, NULL);
    if (error_string) {
        fprintf(stderr, "wireshark: Couldn't register rlc_graph tap: %s\n",
                error_string->str);
        g_string_free(error_string, TRUE);
        return false;
    }
    cf_retap_packets(cf);
    remove_tap_listener(g);

    if (g->last_segment == NULL) {
        *err_string = g_strdup("No packets found");
        return false;
    }

    return true;
}

/* Free and zero the segments list of an rlc_graph struct */
void rlc_graph_segment_list_free(struct rlc_graph * g)
{
    struct rlc_segment *segment;

    /* Free all segments */
    while (g->segments) {
        segment = g->segments->next;
        g_free(g->segments);
        g->segments = segment;
    }
}
