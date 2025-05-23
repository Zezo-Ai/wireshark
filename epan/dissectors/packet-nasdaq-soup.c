/* packet-nasdaq-soup.c
 * Routines for NASDAQ SOUP 2.0 Protocol dissection
 * Copyright 2007,2008 Didier Gautheron <dgautheron@magic.fr>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Documentation: http://www.nasdaqtrader.com/Trader.aspx?id=DPSpecs
 * ex:
 * http://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/souptcp.pdf
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/prefs.h>


void proto_register_nasdaq_soup(void);
void proto_reg_handoff_nasdaq_soup(void);

static const value_string message_types_val[] = {
      { 'S', "Sequenced Data" },
      { 'R', "Client Heartbeat" },
      { 'H', "Server Heartbeat" },
      { '+' , "Debug Packet" },
      { 'A', "Login Accepted" },
      { 'J', "Login Rejected" },
      { 'L', "Login Request" },
      { 'U', "Unsequenced Data" },
      { 'O', "Logout Request" },
      { 0, NULL }
};

static const value_string reject_code_val[] = {
      { 'A', "Not authorized" },
      { 'S', "Session not available" },
      { 0, NULL }
};

/* Initialize the protocol and registered fields */
static int proto_nasdaq_soup;
static dissector_handle_t nasdaq_soup_handle;
static dissector_handle_t nasdaq_itch_handle;

/* desegmentation of Nasdaq Soup */
static bool nasdaq_soup_desegment = true;

/* Initialize the subtree pointers */
static int ett_nasdaq_soup;

static int hf_nasdaq_soup_packet_type;
static int hf_nasdaq_soup_message;
static int hf_nasdaq_soup_text;
static int hf_nasdaq_soup_packet_eol;
static int hf_nasdaq_soup_username;
static int hf_nasdaq_soup_password;
static int hf_nasdaq_soup_session;
static int hf_nasdaq_soup_seq_number;
static int hf_nasdaq_soup_reject_code;

static void
dissect_nasdaq_soup_packet(tvbuff_t *tvb, packet_info *pinfo, proto_tree *parent_tree, proto_tree *tree, int offset, int linelen)
{
    uint8_t  nasdaq_soup_type;
    tvbuff_t *new_tvb = NULL;

    nasdaq_soup_type = tvb_get_uint8(tvb, offset);
    proto_tree_add_item(tree, hf_nasdaq_soup_packet_type, tvb, offset, 1, ENC_ASCII);
    offset++;

    switch (nasdaq_soup_type) {
    case '+': /* debug msg */
        proto_tree_add_item(tree, hf_nasdaq_soup_text, tvb, offset, linelen -1, ENC_ASCII);
        offset += linelen -1;
        break;
    case 'A': /* login accept */
        proto_tree_add_item(tree, hf_nasdaq_soup_session, tvb, offset, 10, ENC_ASCII);
        offset += 10;

        proto_tree_add_item(tree, hf_nasdaq_soup_seq_number, tvb, offset, 10, ENC_ASCII);
        offset += 10;
        break;
    case 'J': /* login reject */
        proto_tree_add_item(tree, hf_nasdaq_soup_reject_code, tvb, offset, 1, ENC_ASCII);
        offset++;
        break;

    case 'U': /* unsequenced data packed */
    case 'S': /* sequenced data packed */
        if (linelen > 1 && nasdaq_itch_handle) {
            new_tvb = tvb_new_subset_length(tvb, offset,linelen -1);
        } else {
            proto_tree_add_item(tree, hf_nasdaq_soup_message, tvb, offset, linelen -1, ENC_ASCII);
        }
        offset += linelen -1;
        break;

    case 'L': /* login request */
        proto_tree_add_item(tree, hf_nasdaq_soup_username, tvb, offset, 6, ENC_ASCII);
        offset += 6;

        proto_tree_add_item(tree, hf_nasdaq_soup_password, tvb, offset, 10, ENC_ASCII);
        offset += 10;

        proto_tree_add_item(tree, hf_nasdaq_soup_session, tvb, offset, 10, ENC_ASCII);
        offset += 10;

        proto_tree_add_item(tree, hf_nasdaq_soup_seq_number, tvb, offset, 10, ENC_ASCII);
        offset += 10;
        break;

    case 'H': /* server heartbeat */
    case 'O': /* logout request */
    case 'R': /* client heartbeat */
        /* no payload */
        break;
    default:
        /* unknown */
        proto_tree_add_item(tree, hf_nasdaq_soup_message, tvb, offset, linelen -1, ENC_ASCII);
        offset += linelen -1;
        break;
    }

    proto_tree_add_item(tree, hf_nasdaq_soup_packet_eol, tvb, offset, 1, ENC_ASCII);
    if (new_tvb) {
        call_dissector(nasdaq_itch_handle, new_tvb, pinfo, parent_tree);
    }
    return;
}

/* ---------------------------- */
static int
dissect_nasdaq_soup(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_)
{
    proto_item *ti;
    proto_tree *nasdaq_soup_tree = NULL;
    uint8_t nasdaq_soup_type;
    int  linelen;
    int next_offset;
    int  offset = 0;
    int counter = 0;

    while (tvb_offset_exists(tvb, offset)) {
      /* there's only a \n no \r */
      linelen = tvb_find_line_end(tvb, offset, -1, &next_offset, nasdaq_soup_desegment && pinfo->can_desegment);
      if (linelen == -1) {
        /*
         * We didn't find a line ending, and we're doing desegmentation;
         * tell the TCP dissector where the data for this message starts
         * in the data it handed us, and tell it we need one more byte
         * (we may need more, but we'll try again if what we get next
         * isn't enough), and return.
         */
        pinfo->desegment_offset = offset;
        pinfo->desegment_len = DESEGMENT_ONE_MORE_SEGMENT;
        return tvb_captured_length(tvb);
      }

      nasdaq_soup_type = tvb_get_uint8(tvb, offset);
      if (counter == 0) {
        col_set_str(pinfo->cinfo, COL_PROTOCOL, "Nasdaq-SOUP");
        col_clear(pinfo->cinfo, COL_INFO);
      }
      if (counter) {
        col_append_str(pinfo->cinfo, COL_INFO, "; ");
        col_set_fence(pinfo->cinfo, COL_INFO);
      }
      col_append_str(pinfo->cinfo, COL_INFO, val_to_str(nasdaq_soup_type, message_types_val, "Unknown packet type (0x%02x)"));

      counter++;
      ti = proto_tree_add_item(tree, proto_nasdaq_soup, tvb, offset, linelen +1, ENC_NA);
      nasdaq_soup_tree = proto_item_add_subtree(ti, ett_nasdaq_soup);

      dissect_nasdaq_soup_packet(tvb, pinfo, tree, nasdaq_soup_tree, offset, linelen);
      offset = next_offset;
    }
    return tvb_captured_length(tvb);
}

void
proto_register_nasdaq_soup(void)
{

/* Setup list of header fields  See Section 1.6.1 for details*/
    static hf_register_info hf[] = {

    { &hf_nasdaq_soup_packet_type,
      { "Packet Type",       "nasdaq-soup.packet_type",
        FT_CHAR, BASE_HEX, VALS(message_types_val), 0x0,
        NULL, HFILL }},

    { &hf_nasdaq_soup_reject_code,
      { "Login Reject Code", "nasdaq-soup.reject_code",
        FT_CHAR, BASE_HEX, VALS(reject_code_val), 0x0,
        NULL, HFILL }},

    { &hf_nasdaq_soup_message,
      { "Message",           "nasdaq-soup.message",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_nasdaq_soup_text,
      { "Debug Text",        "nasdaq-soup.text",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_nasdaq_soup_username,
      { "User Name",         "nasdaq-soup.username",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_nasdaq_soup_password,
      { "Password",          "nasdaq-soup.password",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_nasdaq_soup_session,
      { "Session",           "nasdaq-soup.session",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Session ID", HFILL }},

    { &hf_nasdaq_soup_seq_number,
      { "Sequence number",   "nasdaq-soup.seq_number",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_nasdaq_soup_packet_eol,
      { "End Of Packet",     "nasdaq-soup.packet_eol",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }}
    };

    /* Setup protocol subtree array */
    static int *ett[] = {
        &ett_nasdaq_soup
    };

    module_t *nasdaq_soup_module;

    /* Register the protocol name and description */
    proto_nasdaq_soup = proto_register_protocol("Nasdaq-SoupTCP version 2.0","NASDAQ-SOUP", "nasdaq_soup");

    /* Required function calls to register the header fields and subtrees used */
    proto_register_field_array(proto_nasdaq_soup, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));

    /* Register the dissector */
    nasdaq_soup_handle = register_dissector("nasdaq_soup", dissect_nasdaq_soup, proto_nasdaq_soup);

    /* Register preferences */
    nasdaq_soup_module = prefs_register_protocol(proto_nasdaq_soup, NULL);
    prefs_register_bool_preference(nasdaq_soup_module, "desegment",
        "Reassemble Nasdaq-SoupTCP messages spanning multiple TCP segments",
        "Whether the Nasdaq-SoupTCP dissector should reassemble messages spanning multiple TCP segments.",
        &nasdaq_soup_desegment);
}

/* If this dissector uses sub-dissector registration add a registration routine.
   This format is required because a script is used to find these routines and
   create the code that calls these routines.
*/
void
proto_reg_handoff_nasdaq_soup(void)
{
    nasdaq_itch_handle = find_dissector_add_dependency("nasdaq-itch", proto_nasdaq_soup);
    dissector_add_uint_range_with_preference("tcp.port", "", nasdaq_soup_handle);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
