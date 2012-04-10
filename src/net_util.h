// See the file "COPYING" in the main distribution directory for copyright.

#ifndef netutil_h
#define netutil_h

#include "config.h"

// Define first.
typedef enum {
	TRANSPORT_UNKNOWN, TRANSPORT_TCP, TRANSPORT_UDP, TRANSPORT_ICMP,
} TransportProto;

typedef enum { IPv4, IPv6 } IPFamily;

#include <assert.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <arpa/inet.h>

#include <netinet/ip.h>
#ifdef HAVE_LINUX
#define __FAVOR_BSD
#endif
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include "util.h"

#ifdef HAVE_NETINET_IP6_H
#include <netinet/ip6.h>
#else
struct ip6_hdr {
	uint16		ip6_plen;
	uint8		ip6_nxt;
	uint8		ip6_hlim;
};
#endif

// For Solaris.
#if !defined(TCPOPT_WINDOW) && defined(TCPOPT_WSCALE)
#define	TCPOPT_WINDOW TCPOPT_WSCALE
#endif

#if !defined(TCPOPT_TIMESTAMP) && defined(TCPOPT_TSTAMP)
#define	TCPOPT_TIMESTAMP TCPOPT_TSTAMP
#endif

// True if sequence # a is between b and c (b <= a <= c).  It must be true
// that b <= c in the sequence space.
inline int seq_between(uint32 a, uint32 b, uint32 c)
	{
	if ( b <= c )
		return a >= b && a <= c;
	else
		return a >= b || a <= c;
	}

// Returns a - b, adjusted for sequence wraparound.
inline int seq_delta(uint32 a, uint32 b)
	{
	return int(a-b);
	}

class IPAddr;

// Returns the ones-complement checksum of a chunk of b short-aligned bytes.
extern int ones_complement_checksum(const void* p, int b, uint32 sum);

extern int ones_complement_checksum(const IPAddr& a, uint32 sum);

extern int icmp_checksum(const struct icmp* icmpp, int len);

#ifdef ENABLE_MOBILE_IPV6
class IP_Hdr;
extern int mobility_header_checksum(const IP_Hdr* ip);
#endif

// Returns 'A', 'B', 'C' or 'D'
extern char addr_to_class(uint32 addr);

extern const char* fmt_conn_id(const IPAddr& src_addr, uint32 src_port,
				const IPAddr& dst_addr, uint32 dst_port);
extern const char* fmt_conn_id(const uint32* src_addr, uint32 src_port,
				const uint32* dst_addr, uint32 dst_port);

// Read 4 bytes from data and return in network order.
extern uint32 extract_uint32(const u_char* data);

// Endian conversions for double.
// This is certainly not a very clean solution but should work on the
// major platforms. Alternativly, we could use a string format or the
// XDR library.

#ifdef WORDS_BIGENDIAN

inline double ntohd(double d)	{ return d; }
inline double htond(double d)	{ return d; }
inline uint64 ntohll(uint64 i)	{ return i; }
inline uint64 htonll(uint64 i)	{ return i; }

#else

inline double ntohd(double d)
	{
	assert(sizeof(d) == 8);

	double tmp;
	char* src = (char*) &d;
	char* dst = (char*) &tmp;

	dst[0] = src[7];
	dst[1] = src[6];
	dst[2] = src[5];
	dst[3] = src[4];
	dst[4] = src[3];
	dst[5] = src[2];
	dst[6] = src[1];
	dst[7] = src[0];

	return tmp;
	}

inline double htond(double d) { return ntohd(d); }

inline uint64 ntohll(uint64 i)
	{
	u_char c;
	union {
		uint64 i;
		u_char c[8];
	} x;

	x.i = i;
	c = x.c[0]; x.c[0] = x.c[7]; x.c[7] = c;
	c = x.c[1]; x.c[1] = x.c[6]; x.c[6] = c;
	c = x.c[2]; x.c[2] = x.c[5]; x.c[5] = c;
	c = x.c[3]; x.c[3] = x.c[4]; x.c[4] = c;
	return x.i;
	}

inline uint64 htonll(uint64 i) { return ntohll(i); }

#endif

#endif
