
#include "Packet.h"
#include "Sessions.h"
#include "iosource/Manager.h"

extern "C" {
#ifdef HAVE_NET_ETHERNET_H
#include <net/ethernet.h>
#elif defined(HAVE_SYS_ETHERNET_H)
#include <sys/ethernet.h>
#elif defined(HAVE_NETINET_IF_ETHER_H)
#include <netinet/if_ether.h>
#elif defined(HAVE_NET_ETHERTYPES_H)
#include <net/ethertypes.h>
#endif
}

void Packet::Init(int arg_link_type, struct timeval *arg_ts, uint32 arg_caplen,
		  uint32 arg_len, const u_char *arg_data, int arg_copy,
		  std::string arg_tag)
	{
	if ( data && copy )
		delete [] data;

	link_type = arg_link_type;
	ts = *arg_ts;
	cap_len = arg_caplen;
	len = arg_len;
	tag = arg_tag;

	copy = arg_copy;

	if ( arg_data && arg_copy )
		{
		data = new u_char[arg_caplen];
		memcpy(const_cast<u_char *>(data), arg_data, arg_caplen);
		}
	else
		data = arg_data;

	time = ts.tv_sec + double(ts.tv_usec) / 1e6;
	hdr_size = GetLinkHeaderSize(arg_link_type);
	l3_proto = L3_UNKNOWN;
	eth_type = 0;
	vlan = 0;
	inner_vlan = 0;

	l2_valid = false;

	if ( data && cap_len < hdr_size )
		{
		Weird("truncated_link_header");
		return;
		}

	if ( data )
		ProcessLayer2();
	}

void Packet::Weird(const char* name)
	{
	sessions->Weird(name, this);
	l2_valid = false;
	}

int Packet::GetLinkHeaderSize(int link_type)
	{
	switch ( link_type ) {
	case DLT_NULL:
		return 4;

	case DLT_EN10MB:
		return 14;

	case DLT_FDDI:
		return 13 + 8;	// fddi_header + LLC

#ifdef DLT_LINUX_SLL
	case DLT_LINUX_SLL:
		return 16;
#endif

	case DLT_PPP_SERIAL:	// PPP_SERIAL
		return 4;

	case DLT_IEEE802_11_RADIO:	// 802.11 plus RadioTap
		return 59;

	case DLT_RAW:
		return 0;
	}

	return -1;
	}

void Packet::ProcessLayer2()
	{
	l2_valid = true;

	// Unfortunately some packets on the link might have MPLS labels
	// while others don't. That means we need to ask the link-layer if
	// labels are in place.
	bool have_mpls = false;

	const u_char* pdata = data;
	const u_char* end_of_data = data + cap_len;

	switch ( link_type ) {
	case DLT_NULL:
		{
		int protocol = (pdata[3] << 24) + (pdata[2] << 16) + (pdata[1] << 8) + pdata[0];
		pdata += GetLinkHeaderSize(link_type);

		// From the Wireshark Wiki: "AF_INET6, unfortunately, has
		// different values in {NetBSD,OpenBSD,BSD/OS},
		// {FreeBSD,DragonFlyBSD}, and {Darwin/Mac OS X}, so an IPv6
		// packet might have a link-layer header with 24, 28, or 30
		// as the AF_ value." As we may be reading traces captured on
		// platforms other than what we're running on, we accept them
		// all here.

		if ( protocol == AF_INET )
			l3_proto = L3_IPV4;
		else if ( protocol == 24 || protocol == 28 || protocol == 30 )
			l3_proto = L3_IPV6;
		else
			{
			Weird("non_ip_packet_in_null_transport");
			return;
			}

		break;
		}

	case DLT_EN10MB:
		{
		// Get protocol being carried from the ethernet frame.
		int protocol = (pdata[12] << 8) + pdata[13];
		pdata += GetLinkHeaderSize(link_type);
		eth_type = protocol;

		switch ( protocol )
			{
			// MPLS carried over the ethernet frame.
			case 0x8847:
				have_mpls = true;
				break;

			// VLAN carried over the ethernet frame.
			// 802.1q / 802.1ad
			case 0x8100:
			case 0x9100:
				if ( pdata + 4 >= end_of_data )
					{
					Weird("truncated_link_header");
					return;
					}

				vlan = ((pdata[0] << 8) + pdata[1]) & 0xfff;
				protocol = ((pdata[2] << 8) + pdata[3]);
				pdata += 4; // Skip the vlan header

				// Check for MPLS in VLAN.
				if ( protocol == 0x8847 )
					{
					have_mpls = true;
					break;
					}

				// Check for double-tagged (802.1ad)
				if ( protocol == 0x8100 || protocol == 0x9100 )
					{
					if ( pdata + 4 >= end_of_data )
						{
						Weird("truncated_link_header");
						return;
						}

					inner_vlan = ((pdata[0] << 8) + pdata[1]) & 0xfff;
					protocol = ((pdata[2] << 8) + pdata[3]);
					pdata += 4; // Skip the vlan header
					}

				eth_type = protocol;
				break;

			// PPPoE carried over the ethernet frame.
			case 0x8864:
				if ( pdata + 8 >= end_of_data )
					{
					Weird("truncated_link_header");
					return;
					}

				protocol = (pdata[6] << 8) + pdata[7];
				pdata += 8; // Skip the PPPoE session and PPP header

				if ( protocol == 0x0021 )
					l3_proto = L3_IPV4;
				else if ( protocol == 0x0057 )
					l3_proto = L3_IPV6;
				else
					{
					// Neither IPv4 nor IPv6.
					Weird("non_ip_packet_in_pppoe_encapsulation");
					return;
					}

				break;
			}

		// Normal path to determine Layer 3 protocol.
		if ( ! have_mpls && l3_proto == L3_UNKNOWN )
			{
			if ( protocol == 0x800 )
				l3_proto = L3_IPV4;
			else if ( protocol == 0x86dd )
				l3_proto = L3_IPV6;
			else if ( protocol == 0x0806 || protocol == 0x8035 )
				l3_proto = L3_ARP;
			else
				{
				// Neither IPv4 nor IPv6.
				Weird("non_ip_packet_in_ethernet");
				return;
				}
			}

		break;
		}

	case DLT_PPP_SERIAL:
		{
		// Get PPP protocol.
		int protocol = (pdata[2] << 8) + pdata[3];
		pdata += GetLinkHeaderSize(link_type);

		if ( protocol == 0x0281 )
			{
			// MPLS Unicast. Remove the pdata link layer and
			// denote a header size of zero before the IP header.
			have_mpls = true;
			}
		else if ( protocol == 0x0021 )
			l3_proto = L3_IPV4;
		else if ( protocol == 0x0057 )
			l3_proto = L3_IPV6;
		else
			{
			// Neither IPv4 nor IPv6.
			Weird("non_ip_packet_in_ppp_encapsulation");
			return;
			}
		break;
		}

	case DLT_IEEE802_11_RADIO:
		{
		if ( pdata + 3 >= end_of_data )
			{
			Weird("truncated_radiotap_header");
			return;
			}
		// Skip over the RadioTap header
		int rtheader_len = (pdata[3] << 8) + pdata[2];
		if ( pdata + rtheader_len >= end_of_data )
			{
			Weird("truncated_radiotap_header");
			return;
			}
		pdata += rtheader_len;

		int type_80211 = pdata[0];
		int len_80211 = 0;
		if ( (type_80211 >> 4) & 0x04 )
			{
			//identified a null frame (we ignore for now).  no weird.
			return;
			}
		// Look for the QoS indicator bit.
		if ( (type_80211 >> 4) & 0x08 )
			len_80211 = 26;
		else
			len_80211 = 24;

		if ( pdata + len_80211 >= end_of_data )
			{
			Weird("truncated_radiotap_header");
			return;
			}
		// skip 802.11 data header
		pdata += len_80211;

		if ( pdata + 8 >= end_of_data )
			{
			Weird("truncated_radiotap_header");
			return;
			}
		// Check that the DSAP and SSAP are both SNAP and that the control
		// field indicates that this is an unnumbered frame.
		// The organization code (24bits) needs to also be zero to
		// indicate that this is encapsulated ethernet.
		if ( pdata[0] == 0xAA && pdata[1] == 0xAA && pdata[2] == 0x03 &&
		     pdata[3] == 0 && pdata[4] == 0 && pdata[5] == 0 )
			{
			pdata += 6;
			}
		else
			{
			// If this is a logical link control frame without the
			// possibility of having a protocol we care about, we'll
			// just skip it for now.
			return;
			}

		int protocol = (pdata[0] << 8) + pdata[1];
		if ( protocol == 0x0800 )
			l3_proto = L3_IPV4;
		else if ( protocol == 0x86DD )
			l3_proto = L3_IPV6;
		else
			{
			Weird("non_ip_packet_in_ieee802_11_radio_encapsulation");
			return;
			}
		pdata += 2;

		break;
		}

	default:
		{
		// Assume we're pointing at IP. Just figure out which version.
		pdata += GetLinkHeaderSize(link_type);
		if ( pdata + sizeof(struct ip) >= end_of_data )
			{
			Weird("truncated_link_header");
			return;
			}

		const struct ip* ip = (const struct ip *)pdata;

		if ( ip->ip_v == 4 )
			l3_proto = L3_IPV4;
		else if ( ip->ip_v == 6 )
			l3_proto = L3_IPV6;
		else
			{
			// Neither IPv4 nor IPv6.
			Weird("non_ip_packet");
			return;
			}

		break;
		}
	}

	if ( have_mpls )
		{
		// Skip the MPLS label stack.
		bool end_of_stack = false;

		while ( ! end_of_stack )
			{
			if ( pdata + 4 >= end_of_data )
				{
				Weird("truncated_link_header");
				return;
				}

			end_of_stack = *(pdata + 2) & 0x01;
			pdata += 4;
			}

		// We assume that what remains is IP
		if ( pdata + sizeof(struct ip) >= end_of_data )
			{
			Weird("no_ip_in_mpls_payload");
			return;
			}

		const struct ip* ip = (const struct ip *)pdata;

		if ( ip->ip_v == 4 )
			l3_proto = L3_IPV4;
		else if ( ip->ip_v == 6 )
			l3_proto = L3_IPV6;
		else
			{
			// Neither IPv4 nor IPv6.
			Weird("no_ip_in_mpls_payload");
			return;
			}
		}

	else if ( encap_hdr_size )
		{
		// Blanket encapsulation. We assume that what remains is IP.
		if ( pdata + encap_hdr_size + sizeof(struct ip) >= end_of_data )
			{
			Weird("no_ip_left_after_encap");
			return;
			}

		pdata += encap_hdr_size;

		const struct ip* ip = (const struct ip *)pdata;

		if ( ip->ip_v == 4 )
			l3_proto = L3_IPV4;
		else if ( ip->ip_v == 6 )
			l3_proto = L3_IPV6;
		else
			{
			// Neither IPv4 nor IPv6.
			Weird("no_ip_in_encap");
			return;
			}

		}

	// We've now determined (a) L3_IPV4 vs (b) L3_IPV6 vs (c) L3_ARP vs
	// (d) L3_UNKNOWN.

	// Calculate how much header we've used up.
	hdr_size = (pdata - data);
}

RecordVal* Packet::BuildPktHdrVal() const
	{
	static RecordType* l2_hdr_type = 0;
	static RecordType* raw_pkt_hdr_type = 0;

	if ( ! raw_pkt_hdr_type )
		{
		raw_pkt_hdr_type = internal_type("raw_pkt_hdr")->AsRecordType();
		l2_hdr_type = internal_type("l2_hdr")->AsRecordType();
		}

	RecordVal* pkt_hdr = new RecordVal(raw_pkt_hdr_type);
	RecordVal* l2_hdr = new RecordVal(l2_hdr_type);

	int is_ethernet = (link_type == DLT_EN10MB) ? 1 : 0;

	int l3 = BifEnum::L3_UNKNOWN;

	if ( l3_proto == L3_IPV4 )
		l3 = BifEnum::L3_IPV4;

	else if ( l3_proto == L3_IPV6 )
		l3 = BifEnum::L3_IPV6;

	else if ( l3_proto == L3_ARP )
		l3 = BifEnum::L3_ARP;

	// l2_hdr layout:
	//      encap: link_encap;      ##< L2 link encapsulation
	//      len: count;		##< Total frame length on wire
	//      cap_len: count;		##< Captured length
	//      src: string &optional;  ##< L2 source (if ethernet)
	//      dst: string &optional;  ##< L2 destination (if ethernet)
	//      vlan: count &optional;  ##< VLAN tag if any (and ethernet)
	//      inner_vlan: count &optional;  ##< Inner VLAN tag if any (and ethernet)
	//      ethertype: count &optional; ##< If ethernet
	//      proto: layer3_proto;    ##< L3 proto

	if ( is_ethernet )
		{
		// Ethernet header layout is:
		//    dst[6bytes] src[6bytes] ethertype[2bytes]...
		l2_hdr->Assign(0, new EnumVal(BifEnum::LINK_ETHERNET, BifType::Enum::link_encap));
		l2_hdr->Assign(3, FmtEUI48(data + 6));	// src
		l2_hdr->Assign(4, FmtEUI48(data));  	// dst

		if ( vlan )
			l2_hdr->Assign(5, new Val(vlan, TYPE_COUNT));

		if ( inner_vlan )
			l2_hdr->Assign(6, new Val(inner_vlan, TYPE_COUNT));

		l2_hdr->Assign(7, new Val(eth_type, TYPE_COUNT));

		if ( eth_type == ETHERTYPE_ARP || eth_type == ETHERTYPE_REVARP )
			// We also identify ARP for L3 over ethernet
			l3 = BifEnum::L3_ARP;
		}
	else
		l2_hdr->Assign(0, new EnumVal(BifEnum::LINK_UNKNOWN, BifType::Enum::link_encap));

	l2_hdr->Assign(1, new Val(len, TYPE_COUNT));
	l2_hdr->Assign(2, new Val(cap_len, TYPE_COUNT));

	l2_hdr->Assign(8, new EnumVal(l3, BifType::Enum::layer3_proto));

	pkt_hdr->Assign(0, l2_hdr);

	if ( l3_proto == L3_IPV4 )
		{
		IP_Hdr ip_hdr((const struct ip*)(data + hdr_size), false);
		return ip_hdr.BuildPktHdrVal(pkt_hdr, 1);
		}

	else if ( l3_proto == L3_IPV6 )
		{
		IP_Hdr ip6_hdr((const struct ip6_hdr*)(data + hdr_size), false, cap_len);
		return ip6_hdr.BuildPktHdrVal(pkt_hdr, 1);
		}

	else
		return pkt_hdr;
	}

Val *Packet::FmtEUI48(const u_char *mac) const
	{
	char buf[20];
	snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return new StringVal(buf);
	}

void Packet::Describe(ODesc* d) const
	{
	const IP_Hdr ip = IP();
	d->Add(ip.SrcAddr());
	d->Add("->");
	d->Add(ip.DstAddr());
	}

bool Packet::Serialize(SerialInfo* info) const
	{
	return SERIALIZE(uint32(ts.tv_sec)) &&
		SERIALIZE(uint32(ts.tv_usec)) &&
		SERIALIZE(uint32(len)) &&
		SERIALIZE(link_type) &&
		info->s->Write(tag.c_str(), tag.length(), "tag") &&
		info->s->Write((const char*)data, cap_len, "data");
	}

#ifdef DEBUG
static iosource::PktDumper* dump = 0;
#endif

Packet* Packet::Unserialize(UnserialInfo* info)
	{
	struct timeval ts;
	uint32 len, link_type;

	if ( ! (UNSERIALIZE((uint32 *)&ts.tv_sec) &&
		UNSERIALIZE((uint32 *)&ts.tv_usec) &&
		UNSERIALIZE(&len) &&
		UNSERIALIZE(&link_type)) )
		return 0;

	char* tag;
	if ( ! info->s->Read((char**) &tag, 0, "tag") )
		return 0;

	const u_char* pkt;
	int caplen;
	if ( ! info->s->Read((char**) &pkt, &caplen, "data") )
		{
		delete [] tag;
		return 0;
		}

	Packet *p = new Packet(link_type, &ts, caplen, len, pkt, true,
			       std::string(tag));
	delete [] tag;

	// For the global timer manager, we take the global network_time as the
	// packet's timestamp for feeding it into our packet loop.
	if ( p->tag == "" )
		p->time = timer_mgr->Time();
	else
		p->time = p->ts.tv_sec + double(p->ts.tv_usec) / 1e6;

#ifdef DEBUG
	if ( debug_logger.IsEnabled(DBG_TM) )
		{
		if ( ! dump )
			dump = iosource_mgr->OpenPktDumper("tm.pcap", true);

		if ( dump )
			{
			dump->Dump(p);
			}
		}
#endif

	return p;
	}
