/* packet-ismp.c
 * Routines for ISMP dissection
 * Enterasys Networks Home: http://www.enterasys.com/
 * Copyright 2003, Joshua Craig Douglas <jdouglas@enterasys.com>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/expert.h>
#include <epan/to_str.h>
#include <epan/etypes.h>
#include <epan/addr_resolv.h>
#include <epan/tfs.h>
#include <wsutil/array.h>

void proto_register_ismp(void);
void proto_reg_handoff_ismp(void);

static dissector_handle_t ismp_handle;

/* Initialize the protocol and registered fields */
static int proto_ismp;
static int hf_ismp_version;
static int hf_ismp_message_type;
static int hf_ismp_seq_num;
static int hf_ismp_code_length;
static int hf_ismp_auth_data;

/* Enterasys/Cabletron Discovery Protocol fields*/
static int hf_ismp_edp;
static int hf_ismp_edp_version;
static int hf_ismp_edp_module_ip;
static int hf_ismp_edp_module_mac;
static int hf_ismp_edp_module_port;
static int hf_ismp_edp_chassis_mac;
static int hf_ismp_edp_chassis_ip;
static int hf_ismp_edp_device_type;
static int hf_ismp_edp_module_rev;
static int hf_ismp_edp_options;
static int hf_ismp_edp_sfs_option_unused1;
static int hf_ismp_edp_sfs_option_sfssup;
static int hf_ismp_edp_sfs_option_lsp;
static int hf_ismp_edp_sfs_option_flood;
static int hf_ismp_edp_sfs_option_resolve;
static int hf_ismp_edp_sfs_option_unused2;
static int hf_ismp_edp_sfs_option_tagflood;
static int hf_ismp_edp_sfs_option_calltap;
static int hf_ismp_edp_sfs_option_conmsg;
static int hf_ismp_edp_sfs_option_redun;
static int hf_ismp_edp_sfs_option_isolated;
static int hf_ismp_edp_sfs_option_uplink_switch;
static int hf_ismp_edp_sfs_option_uplink_core;
static int hf_ismp_edp_sfs_option_uplink_port;
static int hf_ismp_edp_sfs_option_uplink_flood;
static int hf_ismp_edp_rtr_option_ssr;
static int hf_ismp_edp_rtr_option_igmp;
static int hf_ismp_edp_rtr_option_rip;
static int hf_ismp_edp_rtr_option_bgp;
static int hf_ismp_edp_rtr_option_ospf;
static int hf_ismp_edp_rtr_option_dvmrp;
static int hf_ismp_edp_rtr_option_8021q;
static int hf_ismp_edp_rtr_option_gvrp;
static int hf_ismp_edp_rtr_option_gmrp;
static int hf_ismp_edp_rtr_option_igmp_snoop;
static int hf_ismp_edp_rtr_option_route;
static int hf_ismp_edp_rtr_option_trans;
static int hf_ismp_edp_rtr_option_level1;
static int hf_ismp_edp_switch_option_8021q;
static int hf_ismp_edp_switch_option_gvrp;
static int hf_ismp_edp_switch_option_gmrp;
static int hf_ismp_edp_switch_option_igmp;
static int hf_ismp_edp_switch_option_route;
static int hf_ismp_edp_switch_option_trans;
static int hf_ismp_edp_switch_option_level1;
static int hf_ismp_edp_end_station_option_dhcp;
static int hf_ismp_edp_end_station_option_dns;
static int hf_ismp_edp_end_station_option_ad;
static int hf_ismp_edp_num_neighbors;
static int hf_ismp_edp_neighbors;
static int hf_ismp_edp_num_tuples;
static int hf_ismp_edp_tuples;
/* Generated from convert_proto_tree_add_text.pl */
static int hf_ismp_assigned_neighbor_state;
static int hf_ismp_hold_time;
static int hf_ismp_interface_name;
static int hf_ismp_tuple_length;
static int hf_ismp_neighborhood_mac_address;
static int hf_ismp_unknown_tuple_data;
static int hf_ismp_tuple_type;
static int hf_ismp_system_description;
static int hf_ismp_interface_ipx_address;


/* Initialize the subtree pointers */
static int ett_ismp;
static int ett_ismp_edp;
static int ett_ismp_edp_options;
static int ett_ismp_edp_neighbors;
static int ett_ismp_edp_neighbors_leaf;
static int ett_ismp_edp_tuples;
static int ett_ismp_edp_tuples_leaf;

static expert_field ei_ismp_malformed;

/* ISMP TYPES */
#define ISMPTYPE_EDP	2


/* EDP DEVICE TYPES */
#define EDP_DEVICE_TYPE_SFS17		1
#define EDP_DEVICE_TYPE_SFS18		2
#define EDP_DEVICE_TYPE_ROUTER		3
#define EDP_DEVICE_TYPE_BRIDGE		4
#define EDP_DEVICE_TYPE_VLANMAN		5
#define EDP_DEVICE_TYPE_NTSERVER	6
#define	EDP_DEVICE_TYPE_NTCLIENT	7
#define EDP_DEVICE_TYPE_WIN95		8
#define EDP_DEVICE_TYPE_WIN98		9
#define EDP_DEVICE_TYPE_UNIXSERVER	10
#define EDP_DEVICE_TYPE_UNIXCLIENT	11
#define EDP_DEVICE_TYPE_ACCESSPOINT	12


static const value_string edp_device_types[] = {
	{ EDP_DEVICE_TYPE_SFS17,       "Network Switch running SecureFast version 1.7 or lower" },
	{ EDP_DEVICE_TYPE_SFS18,       "Network Switch running SecureFast version 1.8 or greater" },
	{ EDP_DEVICE_TYPE_ROUTER,      "Router" },
	{ EDP_DEVICE_TYPE_BRIDGE,      "Bridge" },
	{ EDP_DEVICE_TYPE_VLANMAN,     "Cabletron VLAN Manager" },
	{ EDP_DEVICE_TYPE_NTSERVER,    "Network Server (NT)" },
	{ EDP_DEVICE_TYPE_NTCLIENT,    "Network Workstation (NT)" },
	{ EDP_DEVICE_TYPE_WIN95,       "Windows95" },
	{ EDP_DEVICE_TYPE_WIN98,       "Windows98" },
	{ EDP_DEVICE_TYPE_UNIXSERVER,  "UNIX Server" },
	{ EDP_DEVICE_TYPE_UNIXCLIENT,  "UNIX Workstation" },
	{ EDP_DEVICE_TYPE_ACCESSPOINT, "Roamabout wireless access point" },
	{ 0,			NULL },
};


/* EDP SFS Options */
#define EDP_SFS_OPTION_UNUSED1		0x00000001
#define EDP_SFS_OPTION_SFSSUP		0x00000002
#define EDP_SFS_OPTION_LSP          0x00000004
#define EDP_SFS_OPTION_FLOOD		0x00000008
#define EDP_SFS_OPTION_RESOLVE		0x00000010
#define EDP_SFS_OPTION_UNUSED2		0x00000020
#define EDP_SFS_OPTION_TAGFLOOD		0x00000040
#define EDP_SFS_OPTION_CALLTAP		0x00000080
#define EDP_SFS_OPTION_CONMSG		0x00000100
#define EDP_SFS_OPTION_REDUN		0x00000200
#define EDP_SFS_OPTION_ISOLATED		0x00000400
#define EDP_SFS_OPTION_UPLINK_SWITCH	0x00000800
#define EDP_SFS_OPTION_UPLINK_CORE	0x00001000
#define EDP_SFS_OPTION_UPLINK_PORT	0x00002000
#define EDP_SFS_OPTION_UPLINK_FLOOD	0x00004000

/* EDP Router Options */
#define EDP_RTR_OPTION_SSR			0x00000001
#define EDP_RTR_OPTION_IGMP			0x00000002
#define EDP_RTR_OPTION_RIP			0x00000004
#define EDP_RTR_OPTION_BGP			0x00000008
#define EDP_RTR_OPTION_OSPF			0x00000010
#define EDP_RTR_OPTION_DVMRP		0x00000020
#define EDP_RTR_OPTION_8021Q		0x00000040
#define EDP_RTR_OPTION_GVRP			0x00000080
#define EDP_RTR_OPTION_GMRP			0x00000100
#define EDP_RTR_OPTION_IGMP_SNOOP	0x00000200
#define EDP_RTR_OPTION_ROUTE		0x00000400
#define EDP_RTR_OPTION_TRANS		0x00000800
#define EDP_RTR_OPTION_LEVEL1		0x00001000

/* EDP Switch Options */
#define EDP_SWITCH_OPTION_8021Q		0x00000001
#define EDP_SWITCH_OPTION_GVRP		0x00000002
#define EDP_SWITCH_OPTION_GMRP		0x00000004
#define EDP_SWITCH_OPTION_IGMP		0x00000008
#define EDP_SWITCH_OPTION_ROUTE		0x00000010
#define EDP_SWITCH_OPTION_TRANS		0x00000020
#define EDP_SWITCH_OPTION_LEVEL1	0x00000040

/* EDP End Station and Server Options */
#define EDP_END_STATION_OPTION_DHCP	0x1
#define EDP_END_STATION_OPTION_DNS	0x2
#define EDP_END_STATION_OPTION_AD	0x4

/* EDP Tuple Types */
#define EDP_TUPLE_UNKNOWN		0
#define EDP_TUPLE_HOLD			1
#define EDP_TUPLE_INT_NAME		2
#define EDP_TUPLE_SYS_DESCRIPT	3
#define EDP_TUPLE_IPX_ADDR		4

static const value_string edp_tuple_types[] =
{
	{ EDP_TUPLE_UNKNOWN,"Unknown" },
	{ EDP_TUPLE_HOLD,"Hold Time" },
	{ EDP_TUPLE_INT_NAME,"Interface Name" },
	{ EDP_TUPLE_SYS_DESCRIPT,"System Description" },
	{ EDP_TUPLE_IPX_ADDR,"IPX Address" },
	{ 0,NULL }
};

static char*
ipx_addr_to_str(wmem_allocator_t *scope, const uint32_t net, const uint8_t *ad)
{
	char        *buf;
	const char *name;

	name = get_ether_name_if_known(ad);

	if (name) {
		buf = wmem_strdup_printf(scope, "%s.%s",
				get_ipxnet_name(scope, net),
				name);
	}
	else {
		buf = wmem_strdup_printf(scope, "%s.%s",
				get_ipxnet_name(scope, net),
				bytes_to_str_punct(scope, ad, 6, '\0'));
	}
	return buf;
}

/* Function to dissect EDP portion of ISMP message */
static void
dissect_ismp_edp(tvbuff_t *tvb, packet_info *pinfo, int offset, proto_tree *ismp_tree)
{
	/* local variables used for EDP dissection */
	int neighbors_count = 0;
	int tuples_count = 0;
	uint16_t device_type = 0;
	uint16_t num_neighbors = 0;
	uint16_t num_tuples = 0;
	uint16_t tuple_type = 0;
	uint32_t tuple_length = 0;
	char* ipx_addr_str;

	/* Set up structures needed to add the protocol subtree and manage it */
	proto_item *edp_ti;
	proto_tree *edp_tree;

	proto_item *edp_neighbors_ti;
	proto_tree *edp_neighbors_tree;

	proto_tree *edp_neighbors_leaf_tree;

	proto_item *edp_tuples_ti;
	proto_tree *edp_tuples_tree;

	proto_tree *edp_tuples_leaf_tree;

	/* add column information marking this as EDP (Enterasys Discover Protocol */
	col_set_str(pinfo->cinfo, COL_PROTOCOL, "ISMP.EDP");
	col_clear(pinfo->cinfo, COL_INFO);

	/* create display subtree for EDP */
		edp_ti  = proto_tree_add_item(ismp_tree, hf_ismp_edp, tvb, offset, -1, ENC_NA);
		edp_tree = proto_item_add_subtree(edp_ti, ett_ismp_edp);

		col_add_fstr(pinfo->cinfo, COL_INFO, "MIP %s, MMAC %s, ifIdx %d",
			tvb_ip_to_str(pinfo->pool, tvb, offset+2),
			tvb_ether_to_str(pinfo->pool, tvb, offset+6),
			tvb_get_ntohl(tvb, offset+12));

		proto_tree_add_item(edp_tree, hf_ismp_edp_version, tvb, offset, 2, ENC_BIG_ENDIAN);
		offset += 2;
		proto_tree_add_item(edp_tree, hf_ismp_edp_module_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
		offset += 4;
		proto_tree_add_item(edp_tree, hf_ismp_edp_module_mac, tvb, offset, 6, ENC_NA);
		offset += 6;
		proto_tree_add_item(edp_tree, hf_ismp_edp_module_port, tvb, offset, 4, ENC_BIG_ENDIAN);
		offset += 4;
		proto_tree_add_item(edp_tree, hf_ismp_edp_chassis_mac, tvb, offset, 6, ENC_NA);
		offset += 6;
		proto_tree_add_item(edp_tree, hf_ismp_edp_chassis_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
		offset += 4;
		device_type = tvb_get_ntohs(tvb, offset);
		proto_tree_add_item(edp_tree, hf_ismp_edp_device_type, tvb, offset, 2, ENC_BIG_ENDIAN);
		offset += 2;
		proto_tree_add_uint_format_value(edp_tree, hf_ismp_edp_module_rev, tvb, offset, 4, tvb_get_ntohl(tvb, offset),
			"%02x.%02x.%02x.%02x", tvb_get_uint8(tvb, offset),
			tvb_get_uint8(tvb, offset+1), tvb_get_uint8(tvb, offset+2), tvb_get_uint8(tvb, offset+3));
		offset += 4;

		/* depending on device_type, show the appropriate options */
		switch (device_type) {
			case EDP_DEVICE_TYPE_SFS17:
			case EDP_DEVICE_TYPE_SFS18:
                {
		        static int * const options[] = {
			        &hf_ismp_edp_sfs_option_uplink_flood,
			        &hf_ismp_edp_sfs_option_uplink_port,
			        &hf_ismp_edp_sfs_option_uplink_core,
			        &hf_ismp_edp_sfs_option_uplink_switch,
			        &hf_ismp_edp_sfs_option_isolated,
			        &hf_ismp_edp_sfs_option_redun,
			        &hf_ismp_edp_sfs_option_conmsg,
			        &hf_ismp_edp_sfs_option_calltap,
			        &hf_ismp_edp_sfs_option_tagflood,
			        &hf_ismp_edp_sfs_option_unused2,
			        &hf_ismp_edp_sfs_option_resolve,
			        &hf_ismp_edp_sfs_option_flood,
			        &hf_ismp_edp_sfs_option_lsp,
			        &hf_ismp_edp_sfs_option_sfssup,
			        &hf_ismp_edp_sfs_option_unused1,
		            NULL
		        };
		        proto_tree_add_bitmask(edp_tree, tvb, offset, hf_ismp_edp_options, ett_ismp_edp_options, options, ENC_BIG_ENDIAN);
                }
				break;
			case EDP_DEVICE_TYPE_ROUTER:
                {
		        static int * const options[] = {
			        &hf_ismp_edp_rtr_option_level1,
			        &hf_ismp_edp_rtr_option_trans,
			        &hf_ismp_edp_rtr_option_route,
			        &hf_ismp_edp_rtr_option_igmp_snoop,
			        &hf_ismp_edp_rtr_option_gmrp,
			        &hf_ismp_edp_rtr_option_gvrp,
			        &hf_ismp_edp_rtr_option_8021q,
			        &hf_ismp_edp_rtr_option_dvmrp,
			        &hf_ismp_edp_rtr_option_ospf,
			        &hf_ismp_edp_rtr_option_bgp,
			        &hf_ismp_edp_rtr_option_rip,
			        &hf_ismp_edp_rtr_option_igmp,
			        &hf_ismp_edp_rtr_option_ssr,
		            NULL
		        };

		        proto_tree_add_bitmask(edp_tree, tvb, offset, hf_ismp_edp_options, ett_ismp_edp_options, options, ENC_BIG_ENDIAN);
                }
				break;
			case EDP_DEVICE_TYPE_BRIDGE:
                {
		        static int * const options[] = {
			        &hf_ismp_edp_switch_option_level1,
			        &hf_ismp_edp_switch_option_trans,
			        &hf_ismp_edp_switch_option_route,
			        &hf_ismp_edp_switch_option_igmp,
			        &hf_ismp_edp_switch_option_gmrp,
			        &hf_ismp_edp_switch_option_gvrp,
			        &hf_ismp_edp_switch_option_8021q,
		            NULL
		        };

		        proto_tree_add_bitmask(edp_tree, tvb, offset, hf_ismp_edp_options, ett_ismp_edp_options, options, ENC_BIG_ENDIAN);
                }
				break;
			case EDP_DEVICE_TYPE_NTSERVER:
			case EDP_DEVICE_TYPE_NTCLIENT:
			case EDP_DEVICE_TYPE_WIN95:
			case EDP_DEVICE_TYPE_WIN98:
			case EDP_DEVICE_TYPE_UNIXSERVER:
			case EDP_DEVICE_TYPE_UNIXCLIENT:
                {
		        static int * const options[] = {
			        &hf_ismp_edp_end_station_option_ad,
			        &hf_ismp_edp_end_station_option_dns,
			        &hf_ismp_edp_end_station_option_dhcp,
		            NULL
		        };

		        proto_tree_add_bitmask(edp_tree, tvb, offset, hf_ismp_edp_options, ett_ismp_edp_options, options, ENC_BIG_ENDIAN);
                }
				break;
			case EDP_DEVICE_TYPE_VLANMAN:
			case EDP_DEVICE_TYPE_ACCESSPOINT:
			default:
		        proto_tree_add_item(edp_tree, hf_ismp_edp_options, tvb, offset, 4, ENC_BIG_ENDIAN);
				break;
		}
		offset += 4;

		/* determine the number of neighbors and create EDP neighbors subtree */
		num_neighbors = tvb_get_ntohs(tvb, offset);
		proto_tree_add_item(edp_tree, hf_ismp_edp_num_neighbors, tvb, offset, 2, ENC_BIG_ENDIAN);
		offset += 2;
		if (num_neighbors > 0)
		{
			edp_neighbors_ti = proto_tree_add_item(edp_tree, hf_ismp_edp_neighbors, tvb,
										offset, num_neighbors*10, ENC_NA);
			edp_neighbors_tree = proto_item_add_subtree(edp_neighbors_ti, ett_ismp_edp_neighbors);
			while ( neighbors_count < num_neighbors && tvb_reported_length_remaining(tvb, offset) >= 10)
			{
				edp_neighbors_leaf_tree = proto_tree_add_subtree_format(edp_neighbors_tree, tvb, offset, 10,
										ett_ismp_edp_neighbors_leaf, NULL, "Neighbor%d", (neighbors_count+1));

				proto_tree_add_item(edp_neighbors_leaf_tree, hf_ismp_neighborhood_mac_address, tvb, offset, 6, ENC_NA);
				proto_tree_add_item(edp_neighbors_leaf_tree, hf_ismp_assigned_neighbor_state, tvb, offset, 4, ENC_BIG_ENDIAN);
				offset += 10;
				neighbors_count++;
			}
			if (neighbors_count != num_neighbors)
			{
				proto_tree_add_expert(edp_tree, pinfo, &ei_ismp_malformed, tvb, offset, -1);
				return;
			}
		}

		/* determine data remains, if so, count tuples
		   and create EDP tuples subtree */
		if (tvb_reported_length_remaining(tvb, offset) != 0 &&
			tvb_reported_length_remaining(tvb, offset) >= 2)
		{
			num_tuples = tvb_get_ntohs(tvb, offset);
			proto_tree_add_item(edp_tree, hf_ismp_edp_num_tuples, tvb, offset, 2, ENC_BIG_ENDIAN);
			offset += 2;
		}
		else if (tvb_reported_length_remaining(tvb, offset) > 0) {
			proto_tree_add_expert(edp_tree, pinfo, &ei_ismp_malformed, tvb, offset, -1);
			return;
		}
		else
		{
			return;
		}

		/* start populating tuple information */
		if (num_tuples && tvb_reported_length_remaining(tvb, offset) >= 4)
		{
			edp_tuples_ti = proto_tree_add_bytes_format(edp_tree, hf_ismp_edp_tuples, tvb,
				offset, -1, NULL, "Tuples");
			edp_tuples_tree = proto_item_add_subtree(edp_tuples_ti, ett_ismp_edp_tuples);

			while ( (tuples_count < num_tuples) && (tvb_reported_length_remaining(tvb, offset) >= 4) )
			{

				edp_tuples_leaf_tree = proto_tree_add_subtree_format(edp_tuples_tree, tvb, offset, 4,
					ett_ismp_edp_tuples_leaf, NULL, "Tuple%d", tuples_count+1);

				tuple_type = tvb_get_ntohs(tvb, offset);
				proto_tree_add_item(edp_tuples_leaf_tree, hf_ismp_tuple_type, tvb, offset, 2, ENC_BIG_ENDIAN);
				offset += 2;
				proto_tree_add_item_ret_uint(edp_tuples_leaf_tree, hf_ismp_tuple_length, tvb, offset, 2, ENC_BIG_ENDIAN, &tuple_length);
				if (tuple_length < 4) {
					proto_tree_add_expert(edp_tree, pinfo, &ei_ismp_malformed, tvb, offset, 2);
					return;
				}
				offset += 2;
				proto_item_set_len(edp_tuples_leaf_tree, tuple_length);
				tuple_length -= 4;

				if ((unsigned)tvb_reported_length_remaining(tvb, offset) >= tuple_length)
				{
					switch (tuple_type)
					{
						case EDP_TUPLE_HOLD:
							proto_tree_add_item(edp_tuples_leaf_tree, hf_ismp_hold_time, tvb, offset, tuple_length, ENC_BIG_ENDIAN);
							break;
						case EDP_TUPLE_INT_NAME:
							proto_tree_add_item(edp_tuples_leaf_tree, hf_ismp_interface_name, tvb, offset, tuple_length, ENC_ASCII);
							col_append_fstr(pinfo->cinfo, COL_INFO, ", ifName %s",
								tvb_format_text(pinfo->pool, tvb, offset, tuple_length));
							break;
						case EDP_TUPLE_SYS_DESCRIPT:
							proto_tree_add_item(edp_tuples_leaf_tree, hf_ismp_system_description, tvb, offset, tuple_length, ENC_ASCII);
							break;
						case EDP_TUPLE_IPX_ADDR:
							if (tuple_length != 4+6) {
								proto_tree_add_expert(edp_tree, pinfo, &ei_ismp_malformed, tvb, offset, tuple_length);
								return;
							}
							ipx_addr_str = ipx_addr_to_str(pinfo->pool, tvb_get_ntohl(tvb, offset), tvb_get_ptr(tvb, offset+4, tuple_length-4));
							proto_tree_add_string(edp_tuples_leaf_tree, hf_ismp_interface_ipx_address ,tvb, offset, tuple_length, ipx_addr_str);
							break;
						case EDP_TUPLE_UNKNOWN:
						default:
							proto_tree_add_item(edp_tuples_leaf_tree, hf_ismp_unknown_tuple_data, tvb, offset, tuple_length, ENC_ASCII);
							break;
					}
				}
				offset += tuple_length;

				tuples_count++;
			}
			if (tuples_count != num_tuples)
				proto_tree_add_expert(edp_tree, pinfo, &ei_ismp_malformed, tvb, offset, -1);

			return;
		}
}


/* Code to actually dissect the packets */
static int
dissect_ismp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_)
{
	int offset = 0;
	uint16_t message_type = 0;
	uint8_t code_length = 0;
	uint8_t weird_stuff[3] = { 0x42, 0x42, 0x03 };

/* Set up structures needed to add the protocol subtree and manage it */
	proto_item *ti;
	proto_tree *ismp_tree;

/* Make entries in Protocol column and Info column on summary display */
	col_set_str(pinfo->cinfo, COL_PROTOCOL, "ISMP");
	col_clear(pinfo->cinfo, COL_INFO);

	/*
	 * XXX - I've seen captures with packets that have the ISMP
	 * Ethernet frame type, but with the payload being 0x42 0x42 0x03
	 * followed by what appears to be an ISMP frame.
	 *
	 * 0x4242 is not a valid ISMP version number.
	 */
	if (tvb_memeql(tvb, offset, weird_stuff, sizeof weird_stuff) == 0)
		offset += 3;	/* skip the weird stuff, for now */

	/* create display subtree for ismp */
	ti = proto_tree_add_item(tree, proto_ismp, tvb, offset, -1, ENC_NA);
	ismp_tree = proto_item_add_subtree(ti, ett_ismp);

	/* add an items to the subtree */
	proto_tree_add_item(ismp_tree, hf_ismp_version, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;
	message_type = tvb_get_ntohs(tvb, offset);
	proto_tree_add_item(ismp_tree, hf_ismp_message_type, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;
	proto_tree_add_item(ismp_tree, hf_ismp_seq_num, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;
	code_length = tvb_get_uint8(tvb, offset);
	proto_tree_add_item(ismp_tree, hf_ismp_code_length, tvb, offset, 1, ENC_BIG_ENDIAN);
	offset += 1;
	proto_tree_add_item(ismp_tree, hf_ismp_auth_data, tvb, offset, code_length, ENC_NA);
	offset += code_length;

	/* if Enterasys Discover Protocol, dissect it */
	if(message_type == ISMPTYPE_EDP)
		dissect_ismp_edp(tvb, pinfo, offset, tree);

	return tvb_captured_length(tvb);
}


/* Register this protocol with Wireshark */
void
proto_register_ismp(void)
{

/* Setup list of header fields  See Section 1.6.1 for details*/
	static hf_register_info hf[] = {
		{ &hf_ismp_version,
			{ "Version",           "ismp.version",
			FT_UINT16, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_message_type,
			{ "Message Type", "ismp.msgtype",
			FT_UINT16, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_seq_num,
			{ "Sequence Number", "ismp.seqnum",
			FT_UINT16, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_code_length,
			{ "Auth Code Length", "ismp.codelen",
			FT_UINT8, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_auth_data,
			{ "Auth Data", "ismp.authdata",
			FT_BYTES, BASE_NONE, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp,
			{ "EDP", "ismp.edp",
			FT_PROTOCOL, BASE_NONE, NULL, 0x0,
			"Enterasys Discovery Protocol", HFILL }
		},
		{ &hf_ismp_edp_version,
			{ "Version", "ismp.edp.version",
			FT_UINT16, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_module_ip,
			{ "Module IP Address", "ismp.edp.modip",
			FT_IPv4, BASE_NONE, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_module_mac,
			{ "Module MAC Address", "ismp.edp.modmac",
			FT_ETHER, BASE_NONE, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_module_port,
			{ "Module Port (ifIndex num)", "ismp.edp.modport",
			FT_UINT32, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_chassis_mac,
			{ "Chassis MAC Address", "ismp.edp.chassismac",
			FT_ETHER, BASE_NONE, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_chassis_ip,
			{ "Chassis IP Address", "ismp.edp.chassisip",
			FT_IPv4, BASE_NONE, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_device_type,
			{ "Device Type", "ismp.edp.devtype",
			FT_UINT16, BASE_DEC, VALS(edp_device_types), 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_module_rev,
			{ "Module Firmware Revision", "ismp.edp.rev",
			FT_UINT32, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_options,
			{ "Device Options", "ismp.edp.options",
			FT_UINT32, BASE_HEX, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_unused1,
			{ "Unused", "ismp.edp.sfs_option_unused1",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_UNUSED1,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_sfssup,
			{ "SFS Support", "ismp.edp.sfs_option_sfssup",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_SFSSUP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_lsp,
			{ "LSP Support", "ismp.edp.sfs_option_lsp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_LSP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_flood,
			{ "Flood Path Support", "ismp.edp.sfs_option_flood",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_FLOOD,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_resolve,
			{ "Resolve Support", "ismp.edp.sfs_option_resolve",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_RESOLVE,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_unused2,
			{ "Unused", "ismp.edp.sfs_option_unused2",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_UNUSED2,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_tagflood,
			{ "Tagged Flood Support", "ismp.edp.sfs_option_tagflood",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_TAGFLOOD,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_calltap,
			{ "Call Tap Support", "ismp.edp.sfs_option_calltap",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_CALLTAP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_conmsg,
			{ "Connection Message Support", "ismp.edp.sfs_option_conmsg",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_CONMSG,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_redun,
			{ "Redundant Access Support", "ismp.edp.sfs_option_redun",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_REDUN,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_isolated,
			{ "Isolated Switch", "ismp.edp.sfs_option_isolated",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_ISOLATED,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_uplink_switch,
			{ "Uplink Switch", "ismp.edp.sfs_option_uplink_switch",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_UPLINK_SWITCH,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_uplink_core,
			{ "Uplink Core", "ismp.edp.sfs_option_uplink_core",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_UPLINK_CORE,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_uplink_port,
			{ "Uplink Port", "ismp.edp.sfs_option_uplink_port",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_UPLINK_PORT,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_sfs_option_uplink_flood,
			{ "Uplink Flood Support", "ismp.edp.sfs_option_uplink_flood",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SFS_OPTION_UPLINK_FLOOD,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_ssr,
			{ "SSR Type Device", "ismp.edp.rtr_option_ssr",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_SSR,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_igmp,
			{ "IGMP Active", "ismp.edp.rtr_option_igmp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_IGMP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_rip,
			{ "RIP Active", "ismp.edp.rtr_option_rip",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_RIP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_bgp,
			{ "BGP Active", "ismp.edp.rtr_option_bgp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_BGP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_ospf,
			{ "OSPF Active", "ismp.edp.rtr_option_ospf",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_OSPF,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_dvmrp,
			{ "DVMRP Active", "ismp.edp.rtr_option_dvmrp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_DVMRP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_8021q,
			{ "802.1Q Support", "ismp.edp.rtr_option_8021q",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_8021Q,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_gvrp,
			{ "GVRP Support", "ismp.edp.rtr_option_gvrp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_GVRP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_gmrp,
			{ "GMRP Support", "ismp.edp.rtr_option_gmrp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_GMRP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_igmp_snoop,
			{ "IGMP Snooping Support", "ismp.edp.rtr_option_igmp_snoop",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_IGMP_SNOOP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_route,
			{ "Route Bridging", "ismp.edp.rtr_option_route",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_ROUTE,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_trans,
			{ "Transparent Bridging", "ismp.edp.rtr_option_trans",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_TRANS,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_rtr_option_level1,
			{ "Level 1 Functionality", "ismp.edp.rtr_option_level1",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_RTR_OPTION_LEVEL1,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_switch_option_8021q,
			{ "802.1Q Support", "ismp.edp.switch_option_8021q",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SWITCH_OPTION_8021Q,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_switch_option_gvrp,
			{ "GVRP Support", "ismp.edp.switch_option_gvrp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SWITCH_OPTION_GVRP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_switch_option_gmrp,
			{ "GMRP Support", "ismp.edp.switch_option_gmrp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SWITCH_OPTION_GMRP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_switch_option_igmp,
			{ "IGMP Snooping Support", "ismp.edp.switch_option_igmp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SWITCH_OPTION_IGMP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_switch_option_route,
			{ "Route Bridging", "ismp.edp.switch_option_route",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SWITCH_OPTION_ROUTE,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_switch_option_trans,
			{ "Transparent Bridging", "ismp.edp.switch_option_trans",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SWITCH_OPTION_TRANS,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_switch_option_level1,
			{ "Level 1 Functionality", "ismp.edp.switch_option_level1",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_SWITCH_OPTION_LEVEL1,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_end_station_option_dhcp,
			{ "DHCP Enabled", "ismp.edp.end_station_option_dhcp",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_END_STATION_OPTION_DHCP,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_end_station_option_dns,
			{ "DNS Enabled", "ismp.edp.end_station_option_dns",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_END_STATION_OPTION_DNS,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_end_station_option_ad,
			{ "Active Directory Enabled", "ismp.edp.end_station_option_ad",
			FT_BOOLEAN, 32, TFS(&tfs_set_notset), EDP_END_STATION_OPTION_AD,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_num_neighbors,
			{ "Number of Known Neighbors", "ismp.edp.maccount",
			FT_UINT16, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_neighbors,
			{ "Neighbors", "ismp.edp.nbrs",
			FT_BYTES, BASE_NONE, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_num_tuples,
			{ "Number of Tuples", "ismp.edp.numtups",
			FT_UINT16, BASE_DEC, NULL, 0x0,
			NULL, HFILL }
		},
		{ &hf_ismp_edp_tuples,
			{ "Number of Tuples", "ismp.edp.tups",
			FT_BYTES, BASE_NONE, NULL, 0x0,
			NULL, HFILL }
		},

		/* Generated from convert_proto_tree_add_text.pl */
		{ &hf_ismp_neighborhood_mac_address, { "MAC Address", "ismp.neighborhood_mac_address", FT_ETHER, BASE_NONE, NULL, 0x0, NULL, HFILL }},
		{ &hf_ismp_assigned_neighbor_state, { "Assigned Neighbor State", "ismp.assigned_neighbor_state", FT_UINT32, BASE_HEX, NULL, 0x0, NULL, HFILL }},
		{ &hf_ismp_tuple_type, { "Tuple Type", "ismp.tuple_type", FT_UINT16, BASE_DEC, VALS(edp_tuple_types), 0x0, NULL, HFILL }},
		{ &hf_ismp_tuple_length, { "Tuple Length", "ismp.tuple_length", FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL }},
		{ &hf_ismp_hold_time, { "Hold Time", "ismp.hold_time", FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL }},
		{ &hf_ismp_interface_name, { "Interface Name", "ismp.interface_name", FT_STRING, BASE_NONE, NULL, 0x0, NULL, HFILL }},
		{ &hf_ismp_system_description, { "System Description", "ismp.system_description", FT_STRING, BASE_NONE, NULL, 0x0, NULL, HFILL }},
		{ &hf_ismp_interface_ipx_address, { "Interface IPX_address", "ismp.interface_ipx_address", FT_STRING, BASE_NONE, NULL, 0x0, NULL, HFILL }},
		{ &hf_ismp_unknown_tuple_data, { "Unknown Tuple Data", "ismp.unknown_tuple_data", FT_STRING, BASE_NONE, NULL, 0x0, NULL, HFILL }},
	};

/* Setup protocol subtree array */
	static int *ett[] = {
		&ett_ismp,
		&ett_ismp_edp,
		&ett_ismp_edp_options,
		&ett_ismp_edp_neighbors,
		&ett_ismp_edp_neighbors_leaf,
		&ett_ismp_edp_tuples,
		&ett_ismp_edp_tuples_leaf,
	};

	static ei_register_info ei[] = {
		{ &ei_ismp_malformed, { "ismp.malformed", PI_MALFORMED, PI_ERROR, "Malformed packet", EXPFILL }},
	};

	expert_module_t* expert_ismp;

/* Register the protocol name and description */
	proto_ismp = proto_register_protocol("InterSwitch Message Protocol",
	    "ISMP", "ismp");

/* Required function calls to register the header fields and subtrees used */
	proto_register_field_array(proto_ismp, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));
	expert_ismp = expert_register_protocol(proto_ismp);
	expert_register_field_array(expert_ismp, ei, array_length(ei));

	ismp_handle = register_dissector("ismp", dissect_ismp, proto_ismp);
}


/* If this dissector uses sub-dissector registration add a registration routine.
   This format is required because a script is used to find these routines and
   create the code that calls these routines.
*/
void
proto_reg_handoff_ismp(void)
{
	dissector_add_uint("ethertype", ETHERTYPE_ISMP, ismp_handle);
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
