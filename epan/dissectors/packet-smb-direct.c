/*
 * packet-smb-direct.c
 *
 * Routines for [MS-SMBD] the RDMA transport layer for SMB2/3
 *
 * Copyright 2012-2014 Stefan Metzmacher <metze@samba.org>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/reassemble.h>
#include <epan/prefs.h>
#include <epan/proto_data.h>
#include "packet-windows-common.h"
#include "packet-iwarp-ddp-rdmap.h"
#include "packet-infiniband.h"

void proto_register_smb_direct(void);
void proto_reg_handoff_smb_direct(void);

static dissector_handle_t smb_direct_handle;

static int proto_smb_direct;

static int ett_smb_direct;
static int ett_smb_direct_hdr;
static int ett_smb_direct_flags;
static int ett_smb_direct_fragment;
static int ett_smb_direct_fragments;

static int hf_smb_direct_negotiate_request;
static int hf_smb_direct_negotiate_response;
static int hf_smb_direct_data_message;
static int hf_smb_direct_min_version;
static int hf_smb_direct_max_version;
static int hf_smb_direct_negotiated_version;
static int hf_smb_direct_credits_requested;
static int hf_smb_direct_credits_granted;
static int hf_smb_direct_status;
static int hf_smb_direct_max_read_write_size;
static int hf_smb_direct_preferred_send_size;
static int hf_smb_direct_max_receive_size;
static int hf_smb_direct_max_fragmented_size;
static int hf_smb_direct_flags;
static int hf_smb_direct_flags_response_requested;
static int hf_smb_direct_remaining_length;
static int hf_smb_direct_data_offset;
static int hf_smb_direct_data_length;
static int hf_smb_direct_fragments;
static int hf_smb_direct_fragment;
static int hf_smb_direct_fragment_overlap;
static int hf_smb_direct_fragment_overlap_conflict;
static int hf_smb_direct_fragment_multiple_tails;
static int hf_smb_direct_fragment_too_long_fragment;
static int hf_smb_direct_fragment_error;
static int hf_smb_direct_fragment_count;
static int hf_smb_direct_reassembled_in;
static int hf_smb_direct_reassembled_length;
static int hf_smb_direct_reassembled_data;

static const fragment_items smb_direct_frag_items = {
	&ett_smb_direct_fragment,
	&ett_smb_direct_fragments,
	&hf_smb_direct_fragments,
	&hf_smb_direct_fragment,
	&hf_smb_direct_fragment_overlap,
	&hf_smb_direct_fragment_overlap_conflict,
	&hf_smb_direct_fragment_multiple_tails,
	&hf_smb_direct_fragment_too_long_fragment,
	&hf_smb_direct_fragment_error,
	&hf_smb_direct_fragment_count,
	&hf_smb_direct_reassembled_in,
	&hf_smb_direct_reassembled_length,
	&hf_smb_direct_reassembled_data,
	"SMB Direct fragments"
};

enum SMB_DIRECT_HDR_TYPE {
	SMB_DIRECT_HDR_UNKNOWN = -1,
	SMB_DIRECT_HDR_NEG_REQ = 1,
	SMB_DIRECT_HDR_NEG_REP = 2,
	SMB_DIRECT_HDR_DATA = 3
};

#define SMB_DIRECT_RESPONSE_REQUESTED 0x0001

static heur_dissector_list_t smb_direct_heur_subdissector_list;

static bool smb_direct_reassemble = true;
static reassembly_table smb_direct_reassembly_table;
static bool smb_direct_infiniband_reassemble = true;

static void
dissect_smb_direct_payload(tvbuff_t *tvb, packet_info *pinfo,
			   proto_tree *tree, uint32_t remaining_length)
{
	bool save_fragmented = pinfo->fragmented;
	int save_visited = pinfo->fd->visited;
	conversation_t *conversation = NULL;
	fragment_head *fd_head = NULL;
	tvbuff_t *payload_tvb = NULL;
	bool more_frags = false;
	bool fd_head_not_cached = false;
	heur_dtbl_entry_t *hdtbl_entry;

	if (!smb_direct_reassemble) {
		payload_tvb = tvb;
		goto dissect_payload;
	}

	conversation = find_or_create_conversation(pinfo);

	if (remaining_length > 0) {
		more_frags = true;
	}

	fd_head = (fragment_head *)p_get_proto_data(wmem_file_scope(), pinfo, proto_smb_direct, 0);
	if (fd_head == NULL) {
		fd_head_not_cached = true;

		pinfo->fd->visited = 0;
		fd_head = fragment_add_seq_next(&smb_direct_reassembly_table,
						tvb, 0, pinfo,
						conversation->conv_index,
						NULL, tvb_captured_length(tvb),
						more_frags);
	}

	if (fd_head == NULL) {
		/*
		 * We really want the fd_head and pass it to
		 * process_reassembled_data()
		 *
		 * So that individual fragments gets the
		 * reassembled in field.
		 */
		fd_head = fragment_get_reassembled_id(&smb_direct_reassembly_table,
						      pinfo,
						      conversation->conv_index);
	}

	if (fd_head == NULL) {
		/*
		 * we need more data...
		 */
		goto done;
	}

	if (fd_head_not_cached) {
		p_add_proto_data(wmem_file_scope(), pinfo,
				 proto_smb_direct, 0, fd_head);
	}

	payload_tvb = process_reassembled_data(tvb, 0, pinfo,
					       "Reassembled SMB Direct",
					       fd_head,
					       &smb_direct_frag_items,
					       NULL, /* update_col_info*/
					       tree);
	if (payload_tvb == NULL) {
		/*
		 * we need more data...
		 */
		goto done;
	}

dissect_payload:
	pinfo->fragmented = false;
	if (!dissector_try_heuristic(smb_direct_heur_subdissector_list,
				     payload_tvb, pinfo, tree, &hdtbl_entry, NULL)) {
		call_data_dissector(payload_tvb, pinfo, tree);
	}
done:
	pinfo->fragmented = save_fragmented;
	pinfo->fd->visited = save_visited;
	return;
}

static void
dissect_smb_direct(tvbuff_t *tvb, packet_info *pinfo, proto_tree *parent_tree,
		   enum SMB_DIRECT_HDR_TYPE hdr_type)

{
	proto_tree *tree = NULL;
	proto_item *item = NULL;
	proto_tree *neg_req_tree = NULL;
	proto_tree *neg_rep_tree = NULL;
	proto_tree *data_tree = NULL;
	int offset = 0;
	uint32_t status = 0;
	uint32_t remaining_length = 0;
	uint32_t data_offset = 0;
	uint32_t data_length = 0;
	unsigned rlen = tvb_reported_length(tvb);
	int len = 0;
	tvbuff_t *next_tvb = NULL;
	static int * const flags[] = {
		&hf_smb_direct_flags_response_requested,
		NULL
	};

	col_set_str(pinfo->cinfo, COL_PROTOCOL, "SMBDirect");
	col_clear(pinfo->cinfo, COL_INFO);

	if (parent_tree != NULL) {
		item = proto_tree_add_item(parent_tree, proto_smb_direct, tvb, 0, -1, ENC_NA);
		tree = proto_item_add_subtree(item, ett_smb_direct);
	}

	switch (hdr_type) {
	case SMB_DIRECT_HDR_UNKNOWN:
		break;

	case SMB_DIRECT_HDR_NEG_REQ:
		col_append_str(pinfo->cinfo, COL_INFO, "NegotiateRequest");

		if (tree == NULL) {
			break;
		}

		item = proto_tree_add_item(tree, hf_smb_direct_negotiate_request, tvb, 0, -1, ENC_NA);
		neg_req_tree = proto_item_add_subtree(item, ett_smb_direct_hdr);

		proto_tree_add_item(neg_req_tree, hf_smb_direct_min_version,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_item(neg_req_tree, hf_smb_direct_max_version,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		/* 2 bytes reserved */
		offset += 2;

		proto_tree_add_item(neg_req_tree, hf_smb_direct_credits_requested,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_item(neg_req_tree, hf_smb_direct_preferred_send_size,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		proto_tree_add_item(neg_req_tree, hf_smb_direct_max_receive_size,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		proto_tree_add_item(neg_req_tree, hf_smb_direct_max_fragmented_size,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		/* offset += 4; */
		break;

	case SMB_DIRECT_HDR_NEG_REP:
		col_append_str(pinfo->cinfo, COL_INFO, "NegotiateResponse");

		status = tvb_get_letohl(tvb, 12);
		if (status != 0) {
			col_append_fstr(
				pinfo->cinfo, COL_INFO, ", Error: %s",
				val_to_str_ext(status, &NT_errors_ext, "Unknown (0x%08X)"));
		}

		if (tree == NULL) {
			break;
		}

		item = proto_tree_add_item(tree, hf_smb_direct_negotiate_response, tvb, 0, -1, ENC_NA);
		neg_rep_tree = proto_item_add_subtree(item, ett_smb_direct_hdr);

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_min_version,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_max_version,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_negotiated_version,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		/* 2 bytes reserved */
		offset += 2;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_credits_requested,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_credits_granted,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_status,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_max_read_write_size,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_preferred_send_size,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_max_receive_size,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		proto_tree_add_item(neg_rep_tree, hf_smb_direct_max_fragmented_size,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		/* offset += 4; */
		break;

	case SMB_DIRECT_HDR_DATA:
		col_append_str(pinfo->cinfo, COL_INFO, "DataMessage");

		rlen = MIN(rlen, 24);

		item = proto_tree_add_item(tree, hf_smb_direct_data_message, tvb, 0, rlen, ENC_NA);
		data_tree = proto_item_add_subtree(item, ett_smb_direct_hdr);

		proto_tree_add_item(data_tree, hf_smb_direct_credits_requested,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_item(data_tree, hf_smb_direct_credits_granted,
				    tvb, offset, 2, ENC_LITTLE_ENDIAN);
		offset += 2;

		proto_tree_add_bitmask(data_tree, tvb, offset, hf_smb_direct_flags,
			       ett_smb_direct_flags, flags, ENC_LITTLE_ENDIAN);
		offset += 2;

		/* 2 bytes reserved */
		offset += 2;

		remaining_length = tvb_get_letohl(tvb, offset);
		proto_tree_add_item(data_tree, hf_smb_direct_remaining_length,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		data_offset = tvb_get_letohl(tvb, offset);
		proto_tree_add_item(data_tree, hf_smb_direct_data_offset,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		data_length = tvb_get_letohl(tvb, offset);
		proto_tree_add_item(data_tree, hf_smb_direct_data_length,
				    tvb, offset, 4, ENC_LITTLE_ENDIAN);
		offset += 4;

		if (data_length > 0 && data_offset > (uint32_t)offset) {
			len = tvb_reported_length_remaining(tvb, data_offset);
		}

		if (data_length <= (uint32_t)len) {
			next_tvb = tvb_new_subset_length(tvb, data_offset,
						  data_length);
		}

		if (next_tvb != NULL) {
			dissect_smb_direct_payload(next_tvb, pinfo,
						   parent_tree, remaining_length);
		}

		/* offset = data_offset + data_length; */
		break;
	}

	return;
}

static enum SMB_DIRECT_HDR_TYPE
is_smb_direct(tvbuff_t *tvb, packet_info *pinfo _U_)
{
	bool maybe_neg_req = false;
	bool maybe_data = false;
	unsigned len = tvb_reported_length(tvb);

	if (len < 20) {
		return SMB_DIRECT_HDR_UNKNOWN;
	}

	if (len == 32 &&
	    tvb_get_letohs(tvb, 0) == 0x0100 && /* min version */
	    tvb_get_letohs(tvb, 2) == 0x0100 && /* max version */
	    tvb_get_letohs(tvb, 4) == 0x0100 && /* negotiated version */
	    tvb_get_letohs(tvb, 6) == 0x0000)   /* reserved */
	{
		/* Negotiate Response */
		return SMB_DIRECT_HDR_NEG_REP;
	}

	if (tvb_get_letohs(tvb, 0) == 0x0100 && /* min version */
	    tvb_get_letohs(tvb, 2) == 0x0100 && /* max version */
	    tvb_get_letohs(tvb, 4) == 0x0000)   /* reserved */
	{
		maybe_neg_req = true;
	}

	if (tvb_get_letohs(tvb, 0) <= 255 &&    /* credits up to 255 */
	    tvb_get_letohs(tvb, 2) <= 255 &&    /* credits up to 255 */
	    tvb_get_letohs(tvb, 4) <= 1   &&    /* flags 0 or 1 */
	    tvb_get_letohs(tvb, 6) == 0)    /* reserved */
	{
		maybe_data = true;
	}

	if (len == 20) {
		if (tvb_get_letohl(tvb, 8) != 0) { /* remaining */
			maybe_data = false;
		}
		if (tvb_get_letohl(tvb, 12) != 0) { /* data offset */
			maybe_data = false;
		}
		if (tvb_get_letohl(tvb, 16) != 0) { /* data length */
			maybe_data = false;
		}

		if (maybe_neg_req && !maybe_data) {
			/* Negotiate Request */
			return SMB_DIRECT_HDR_NEG_REQ;
		}
		/* maybe_neg_req = false; */
		if (maybe_data) {
			/* Data Message */
			return SMB_DIRECT_HDR_DATA;
		}
	}

	if (len <= 24) {
		return SMB_DIRECT_HDR_UNKNOWN;
	}

	if (tvb_get_letohl(tvb, 12) != 24) { /* data offset */
		return SMB_DIRECT_HDR_UNKNOWN;
	}

	if (tvb_get_letohl(tvb, 16) == 0) {  /* data length */
		return SMB_DIRECT_HDR_UNKNOWN;
	}

	if (tvb_get_letohl(tvb, 20) != 0) { /* padding */
		return SMB_DIRECT_HDR_UNKNOWN;
	}

	if (maybe_data) {
		/* Data Message */
		return SMB_DIRECT_HDR_DATA;
	}

	return SMB_DIRECT_HDR_UNKNOWN;
}

static bool
dissect_smb_direct_iwarp_heur(tvbuff_t *tvb, packet_info *pinfo,
			      proto_tree *parent_tree, void *data)
{
	struct rdmapinfo *info = (struct rdmapinfo *)data;
	enum SMB_DIRECT_HDR_TYPE hdr_type;

	if (info == NULL) {
		return false;
	}

	switch (info->opcode) {
	case RDMA_SEND:
	case RDMA_SEND_INVALIDATE:
	case RDMA_SEND_SE:
	case RDMA_SEND_SE_INVALIDATE:
		break;
	default:
		return false;
	}

	hdr_type = is_smb_direct(tvb, pinfo);
	if (hdr_type == SMB_DIRECT_HDR_UNKNOWN) {
		return false;
	}

	dissect_smb_direct(tvb, pinfo, parent_tree, hdr_type);
	return true;
}

static int
dissect_smb_direct_infiniband(tvbuff_t *tvb, packet_info *pinfo,
				   proto_tree *parent_tree, void *data)
{
	struct infinibandinfo *info = (struct infinibandinfo *)data;
	enum SMB_DIRECT_HDR_TYPE hdr_type;

	if (info == NULL) {
		return 0;
	}

	switch (info->opCode) {
	case RC_SEND_FIRST:
	case RC_SEND_MIDDLE:
	case RC_SEND_LAST:
	case RC_SEND_LAST_IMM:
	case RC_SEND_ONLY:
	case RC_SEND_ONLY_IMM:
	case RC_SEND_LAST_INVAL:
	case RC_SEND_ONLY_INVAL:
		break;
	default:
		return 0;
	}

	hdr_type = is_smb_direct(tvb, pinfo);
	if (hdr_type == SMB_DIRECT_HDR_UNKNOWN) {
		return 0;
	}

	if (!info->do_rc_send_reassembling && smb_direct_infiniband_reassemble) {
		/*
		 * Let the infiniband layer
		 * do RC Send reassembling
		 * for this conversation
		 */
		info->do_rc_send_reassembling = smb_direct_infiniband_reassemble;
		return 0;
	}

	dissect_smb_direct(tvb, pinfo, parent_tree, hdr_type);
	return tvb_captured_length(tvb);
}

static bool
dissect_smb_direct_infiniband_heur(tvbuff_t *tvb, packet_info *pinfo,
				proto_tree *parent_tree, void *data)
{
	return (dissect_smb_direct_infiniband(tvb, pinfo, parent_tree, data) > 0);
}

void proto_register_smb_direct(void)
{
	static int *ett[] = {
		&ett_smb_direct,
		&ett_smb_direct_hdr,
		&ett_smb_direct_flags,
		&ett_smb_direct_fragment,
		&ett_smb_direct_fragments,
	};

	static hf_register_info hf[] = {
	{ &hf_smb_direct_negotiate_request,
		{ "NegotiateRequest", "smb_direct.negotiate_request",
		FT_NONE, BASE_NONE, NULL, 0x0, NULL, HFILL }},

	{ &hf_smb_direct_negotiate_response,
		{ "NegotiateResponse", "smb_direct.negotiate_response",
		FT_NONE, BASE_NONE, NULL, 0x0, NULL, HFILL }},

	{ &hf_smb_direct_data_message,
		{ "DataMessage", "smb_direct.data_message",
		FT_NONE, BASE_NONE, NULL, 0x0, NULL, HFILL }},

	{ &hf_smb_direct_min_version,
		{ "MinVersion", "smb_direct.version.min",
		FT_UINT16, BASE_HEX, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_max_version,
		{ "MaxVersion", "smb_direct.version.max",
		FT_UINT16, BASE_HEX, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_negotiated_version,
		{ "NegotiatedVersion", "smb_direct.version.negotiated",
		FT_UINT16, BASE_HEX, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_credits_requested,
		{ "CreditsRequested", "smb_direct.credits.requested",
		FT_UINT16, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_credits_granted,
		{ "CreditsGranted", "smb_direct.credits.granted",
		FT_UINT16, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_status,
		{ "Status", "smb_direct.status",
		FT_UINT32, BASE_HEX|BASE_EXT_STRING, &NT_errors_ext, 0,
		"NT Status code", HFILL }},

	{ &hf_smb_direct_max_read_write_size,
		{ "MaxReadWriteSize", "smb_direct.max_read_write_size",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_preferred_send_size,
		{ "PreferredSendSize", "smb_direct.preferred_send_size",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_max_receive_size,
		{ "MaxReceiveSize", "smb_direct.max_receive_size",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_max_fragmented_size,
		{ "MaxFragmentedSize", "smb_direct.max_fragmented_size",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_flags,
		{ "Flags", "smb_direct.flags",
		FT_UINT16, BASE_HEX, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_flags_response_requested,
		{ "ResponseRequested", "smb_direct.flags.response_requested",
		FT_BOOLEAN, 16, NULL, SMB_DIRECT_RESPONSE_REQUESTED,
		NULL, HFILL }},

	{ &hf_smb_direct_remaining_length,
		{ "RemainingLength", "smb_direct.remaining_length",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_data_offset,
		{ "DataOffset", "smb_direct.data_offset",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_data_length,
		{ "DataLength", "smb_direct.data_length",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_fragments,
		{ "Reassembled SMB Direct Fragments", "smb_direct.fragments",
		FT_NONE, BASE_NONE, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_fragment,
		{ "SMB Direct Fragment", "smb_direct.fragment",
		FT_FRAMENUM, BASE_NONE, NULL, 0, NULL, HFILL }},
	{ &hf_smb_direct_fragment_overlap,
		{ "Fragment overlap", "smb_direct.fragment.overlap",
		FT_BOOLEAN, BASE_NONE, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_fragment_overlap_conflict,
		{ "Conflicting data in fragment overlap", "smb_direct.fragment.overlap.conflict",
		FT_BOOLEAN, BASE_NONE, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_fragment_multiple_tails,
		{ "Multiple tail fragments found", "smb_direct.fragment.multipletails",
		FT_BOOLEAN, BASE_NONE, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_fragment_too_long_fragment,
		{ "Fragment too long", "smb_direct.fragment.toolongfragment",
		FT_BOOLEAN, BASE_NONE, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_fragment_error,
		{ "Defragmentation error", "smb_direct.fragment.error",
		FT_FRAMENUM, BASE_NONE, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_fragment_count,
		{ "Fragment count", "smb_direct.fragment.count",
		FT_UINT32, BASE_DEC, NULL, 0x0, NULL, HFILL }},

	{ &hf_smb_direct_reassembled_in,
		{ "Reassembled PDU in frame", "smb_direct.reassembled_in",
		FT_FRAMENUM, BASE_NONE, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_reassembled_length,
		{ "Reassembled SMB Direct length", "smb_direct.reassembled.length",
		FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},

	{ &hf_smb_direct_reassembled_data,
		{ "Reassembled SMB Direct data", "smb_direct.reassembled.data",
		FT_BYTES, BASE_NONE, NULL, 0, NULL, HFILL }},

	};
	module_t *smb_direct_module;

	proto_smb_direct = proto_register_protocol("SMB-Direct (SMB RDMA Transport)",
						   "SMBDirect", "smb_direct");
	proto_register_subtree_array(ett, array_length(ett));
	proto_register_field_array(proto_smb_direct, hf, array_length(hf));

	smb_direct_handle = register_dissector("smb_direct", dissect_smb_direct_infiniband, proto_smb_direct);

	smb_direct_heur_subdissector_list = register_heur_dissector_list_with_description("smb_direct", "SMB-Direct payload", proto_smb_direct);

	smb_direct_module = prefs_register_protocol(proto_smb_direct, NULL);
	prefs_register_bool_preference(smb_direct_module,
				       "reassemble_smb_direct",
				       "Reassemble SMB Direct fragments",
				       "Whether the SMB Direct dissector should reassemble fragmented payloads",
				       &smb_direct_reassemble);
	reassembly_table_register(&smb_direct_reassembly_table,
	    &addresses_ports_reassembly_table_functions);
	prefs_register_bool_preference(smb_direct_module,
				       "reassemble_smb_direct_infiniband",
				       "Reassemble Infiniband Send fragments for SMB Direct",
				       "Whether the SMB Direct dissector should reassemble Infiniband Send fragments",
				       &smb_direct_infiniband_reassemble);
}

void
proto_reg_handoff_smb_direct(void)
{
	heur_dissector_add("iwarp_ddp_rdmap",
				dissect_smb_direct_iwarp_heur,
				"SMB Direct over iWARP", "smb_direct_iwarp",
				proto_smb_direct, HEURISTIC_ENABLE);
	heur_dissector_add("infiniband.payload",
				dissect_smb_direct_infiniband_heur,
				"SMB Direct Infiniband", "smb_direct_infiniband",
				proto_smb_direct, HEURISTIC_ENABLE);
	dissector_add_for_decode_as("infiniband", smb_direct_handle);

}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
