/*
 * socket-io.c: Socket IO internal calls
 *
 * Authors:
 *	Dick Porter (dick@ximian.com)
 *	Gonzalo Paniagua Javier (gonzalo@ximian.com)
 *
 * Copyright 2001-2003 Ximian, Inc (http://www.ximian.com)
 * Copyright 2004-2009 Novell, Inc (http://www.novell.com)
 *
 * This file has been re-licensed under the MIT License:
 * http://opensource.org/licenses/MIT
 */

#include <config.h>

#ifndef DISABLE_SOCKETS

#if defined(__APPLE__) || defined(__FreeBSD__)
#define __APPLE_USE_RFC_3542
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#ifdef HOST_WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <arpa/inet.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include <sys/types.h>

#include <mono/metadata/object.h>
#include <mono/io-layer/io-layer.h>
#include <mono/metadata/socket-io.h>
#include <mono/metadata/exception.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/file-io.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/threads-types.h>
#include <mono/utils/mono-poll.h>
/* FIXME change this code to not mess so much with the internals */
#include <mono/metadata/class-internals.h>
#include <mono/metadata/threadpool-internals.h>
#include <mono/metadata/domain-internals.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/mono-memory-model.h>
#include <mono/utils/networking.h>

#include <time.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>     /* defines FIONBIO and FIONREAD */
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>    /* defines SIOCATMARK */
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef HAVE_GETIFADDRS
// <net/if.h> must be included before <ifaddrs.h>
#include <ifaddrs.h>
#endif

#include "mono/io-layer/socket-wrappers.h"

#define LOGDEBUG(...)  
/* define LOGDEBUG(...) g_message(__VA_ARGS__)  */

static gint32 convert_family(MonoAddressFamily mono_family)
{
	gint32 family=-1;
	
	switch(mono_family) {
	case AddressFamily_Unknown:
	case AddressFamily_ImpLink:
	case AddressFamily_Pup:
	case AddressFamily_Chaos:
	case AddressFamily_Iso:
	case AddressFamily_Ecma:
	case AddressFamily_DataKit:
	case AddressFamily_Ccitt:
	case AddressFamily_DataLink:
	case AddressFamily_Lat:
	case AddressFamily_HyperChannel:
	case AddressFamily_NetBios:
	case AddressFamily_VoiceView:
	case AddressFamily_FireFox:
	case AddressFamily_Banyan:
	case AddressFamily_Atm:
	case AddressFamily_Cluster:
	case AddressFamily_Ieee12844:
	case AddressFamily_NetworkDesigners:
		g_warning("System.Net.Sockets.AddressFamily has unsupported value 0x%x", mono_family);
		break;
		
	case AddressFamily_Unspecified:
		family=AF_UNSPEC;
		break;
		
	case AddressFamily_Unix:
		family=AF_UNIX;
		break;
		
	case AddressFamily_InterNetwork:
		family=AF_INET;
		break;
		
	case AddressFamily_Ipx:
#ifdef AF_IPX
		family=AF_IPX;
#endif
		break;
		
	case AddressFamily_Sna:
#ifdef AF_SNA
		family=AF_SNA;
#endif
		break;
		
	case AddressFamily_DecNet:
#ifdef AF_DECnet
		family=AF_DECnet;
#endif
		break;
		
	case AddressFamily_AppleTalk:
		family=AF_APPLETALK;
		break;
		
	case AddressFamily_InterNetworkV6:
		family=AF_INET6;
		break;

	case AddressFamily_Irda:
#ifdef AF_IRDA	
		family=AF_IRDA;
#endif
		break;
	default:
		g_warning("System.Net.Sockets.AddressFamily has unknown value 0x%x", mono_family);
	}

	return(family);
}

static MonoAddressFamily convert_to_mono_family(guint16 af_family)
{
	MonoAddressFamily family=AddressFamily_Unknown;
	
	switch(af_family) {
	case AF_UNSPEC:
		family=AddressFamily_Unspecified;
		break;
		
	case AF_UNIX:
		family=AddressFamily_Unix;
		break;
		
	case AF_INET:
		family=AddressFamily_InterNetwork;
		break;
		
#ifdef AF_IPX
	case AF_IPX:
		family=AddressFamily_Ipx;
		break;
#endif
		
#ifdef AF_SNA
	case AF_SNA:
		family=AddressFamily_Sna;
		break;
#endif
		
#ifdef AF_DECnet
	case AF_DECnet:
		family=AddressFamily_DecNet;
		break;
#endif
		
	case AF_APPLETALK:
		family=AddressFamily_AppleTalk;
		break;
		
	case AF_INET6:
		family=AddressFamily_InterNetworkV6;
		break;
		
#ifdef AF_IRDA	
	case AF_IRDA:
		family=AddressFamily_Irda;
		break;
#endif
	default:
		g_warning("unknown address family 0x%x", af_family);
	}

	return(family);
}

static gint32 convert_type(MonoSocketType mono_type)
{
	gint32 type=-1;
	
	switch(mono_type) {
	case SocketType_Stream:
		type=SOCK_STREAM;
		break;

	case SocketType_Dgram:
		type=SOCK_DGRAM;
		break;
		
	case SocketType_Raw:
		type=SOCK_RAW;
		break;

	case SocketType_Rdm:
#ifdef SOCK_RDM
		type=SOCK_RDM;
#endif
		break;

	case SocketType_Seqpacket:
		type=SOCK_SEQPACKET;
		break;

	case SocketType_Unknown:
		g_warning("System.Net.Sockets.SocketType has unsupported value 0x%x", mono_type);
		break;

	default:
		g_warning("System.Net.Sockets.SocketType has unknown value 0x%x", mono_type);
	}

	return(type);
}

static gint32 convert_proto(MonoProtocolType mono_proto)
{
	gint32 proto=-1;
	
	switch(mono_proto) {
	case ProtocolType_IP:
	case ProtocolType_IPv6:
	case ProtocolType_Icmp:
	case ProtocolType_Igmp:
	case ProtocolType_Ggp:
	case ProtocolType_Tcp:
	case ProtocolType_Pup:
	case ProtocolType_Udp:
	case ProtocolType_Idp:
		/* These protocols are known (on my system at least) */
		proto=mono_proto;
		break;
		
	case ProtocolType_ND:
	case ProtocolType_Raw:
	case ProtocolType_Ipx:
	case ProtocolType_Spx:
	case ProtocolType_SpxII:
	case ProtocolType_Unknown:
		/* These protocols arent */
		g_warning("System.Net.Sockets.ProtocolType has unsupported value 0x%x", mono_proto);
		break;
		
	default:
		break;
	}

	return(proto);
}

/* Convert MonoSocketFlags */
static gint32 convert_socketflags (gint32 sflags)
{
	gint32 flags = 0;

	if (!sflags)
		/* SocketFlags.None */
		return 0;

	if (sflags & ~(SocketFlags_OutOfBand | SocketFlags_MaxIOVectorLength | SocketFlags_Peek | 
			SocketFlags_DontRoute | SocketFlags_Partial))
		/* Contains invalid flag values */
		return -1;

	if (sflags & SocketFlags_OutOfBand)
		flags |= MSG_OOB;
	if (sflags & SocketFlags_Peek)
		flags |= MSG_PEEK;
	if (sflags & SocketFlags_DontRoute)
		flags |= MSG_DONTROUTE;

	/* Ignore Partial - see bug 349688.  Don't return -1, because
	 * according to the comment in that bug ms runtime doesn't for
	 * UDP sockets (this means we will silently ignore it for TCP
	 * too)
	 */
#ifdef MSG_MORE
	if (sflags & SocketFlags_Partial)
		flags |= MSG_MORE;
#endif
#if 0
	/* Don't do anything for MaxIOVectorLength */
	if (sflags & SocketFlags_MaxIOVectorLength)
		return -1;	
#endif
	return flags;
}

/*
 * Returns:
 *    0 on success (mapped mono_level and mono_name to system_level and system_name
 *   -1 on error
 *   -2 on non-fatal error (ie, must ignore)
 */
static gint32 convert_sockopt_level_and_name(MonoSocketOptionLevel mono_level,
					     MonoSocketOptionName mono_name,
					     int *system_level,
					     int *system_name)
{
	switch (mono_level) {
	case SocketOptionLevel_Socket:
		*system_level = SOL_SOCKET;
		
		switch(mono_name) {
		case SocketOptionName_DontLinger:
			/* This is SO_LINGER, because the setsockopt
			 * internal call maps DontLinger to SO_LINGER
			 * with l_onoff=0
			 */
			*system_name = SO_LINGER;
			break;
		case SocketOptionName_Debug:
			*system_name = SO_DEBUG;
			break;
#ifdef SO_ACCEPTCONN
		case SocketOptionName_AcceptConnection:
			*system_name = SO_ACCEPTCONN;
			break;
#endif
		case SocketOptionName_ReuseAddress:
			*system_name = SO_REUSEADDR;
			break;
		case SocketOptionName_KeepAlive:
			*system_name = SO_KEEPALIVE;
			break;
		case SocketOptionName_DontRoute:
			*system_name = SO_DONTROUTE;
			break;
		case SocketOptionName_Broadcast:
			*system_name = SO_BROADCAST;
			break;
		case SocketOptionName_Linger:
			*system_name = SO_LINGER;
			break;
		case SocketOptionName_OutOfBandInline:
			*system_name = SO_OOBINLINE;
			break;
		case SocketOptionName_SendBuffer:
			*system_name = SO_SNDBUF;
			break;
		case SocketOptionName_ReceiveBuffer:
			*system_name = SO_RCVBUF;
			break;
		case SocketOptionName_SendLowWater:
			*system_name = SO_SNDLOWAT;
			break;
		case SocketOptionName_ReceiveLowWater:
			*system_name = SO_RCVLOWAT;
			break;
		case SocketOptionName_SendTimeout:
			*system_name = SO_SNDTIMEO;
			break;
		case SocketOptionName_ReceiveTimeout:
			*system_name = SO_RCVTIMEO;
			break;
		case SocketOptionName_Error:
			*system_name = SO_ERROR;
			break;
		case SocketOptionName_Type:
			*system_name = SO_TYPE;
			break;
#ifdef SO_PEERCRED
		case SocketOptionName_PeerCred:
			*system_name = SO_PEERCRED;
			break;
#endif
		case SocketOptionName_ExclusiveAddressUse:
#ifdef SO_EXCLUSIVEADDRUSE
			*system_name = SO_EXCLUSIVEADDRUSE;
			break;
#endif
		case SocketOptionName_UseLoopback:
#ifdef SO_USELOOPBACK
			*system_name = SO_USELOOPBACK;
			break;
#endif
		case SocketOptionName_MaxConnections:
#ifdef SO_MAXCONN
			*system_name = SO_MAXCONN;
			break;
#elif defined(SOMAXCONN)
			*system_name = SOMAXCONN;
			break;
#endif
		default:
			g_warning("System.Net.Sockets.SocketOptionName 0x%x is not supported at Socket level", mono_name);
			return(-1);
		}
		break;
		
	case SocketOptionLevel_IP:
		*system_level = mono_networking_get_ip_protocol ();
		
		switch(mono_name) {
		case SocketOptionName_IPOptions:
			*system_name = IP_OPTIONS;
			break;
#ifdef IP_HDRINCL
		case SocketOptionName_HeaderIncluded:
			*system_name = IP_HDRINCL;
			break;
#endif
#ifdef IP_TOS
		case SocketOptionName_TypeOfService:
			*system_name = IP_TOS;
			break;
#endif
#ifdef IP_TTL
		case SocketOptionName_IpTimeToLive:
			*system_name = IP_TTL;
			break;
#endif
		case SocketOptionName_MulticastInterface:
			*system_name = IP_MULTICAST_IF;
			break;
		case SocketOptionName_MulticastTimeToLive:
			*system_name = IP_MULTICAST_TTL;
			break;
		case SocketOptionName_MulticastLoopback:
			*system_name = IP_MULTICAST_LOOP;
			break;
		case SocketOptionName_AddMembership:
			*system_name = IP_ADD_MEMBERSHIP;
			break;
		case SocketOptionName_DropMembership:
			*system_name = IP_DROP_MEMBERSHIP;
			break;
#ifdef HAVE_IP_PKTINFO
		case SocketOptionName_PacketInformation:
			*system_name = IP_PKTINFO;
			break;
#endif /* HAVE_IP_PKTINFO */

		case SocketOptionName_DontFragment:
#ifdef HAVE_IP_DONTFRAGMENT
			*system_name = IP_DONTFRAGMENT;
			break;
#elif defined HAVE_IP_MTU_DISCOVER
			/* Not quite the same */
			*system_name = IP_MTU_DISCOVER;
			break;
#else
			/* If the flag is not available on this system, we can ignore this error */
			return (-2);
#endif /* HAVE_IP_DONTFRAGMENT */
		case SocketOptionName_AddSourceMembership:
		case SocketOptionName_DropSourceMembership:
		case SocketOptionName_BlockSource:
		case SocketOptionName_UnblockSource:
			/* Can't figure out how to map these, so fall
			 * through
			 */
		default:
			g_warning("System.Net.Sockets.SocketOptionName 0x%x is not supported at IP level", mono_name);
			return(-1);
		}
		break;

	case SocketOptionLevel_IPv6:
		*system_level = mono_networking_get_ipv6_protocol ();

		switch(mono_name) {
		case SocketOptionName_IpTimeToLive:
		case SocketOptionName_HopLimit:
			*system_name = IPV6_UNICAST_HOPS;
			break;
		case SocketOptionName_MulticastInterface:
			*system_name = IPV6_MULTICAST_IF;
			break;
		case SocketOptionName_MulticastTimeToLive:
			*system_name = IPV6_MULTICAST_HOPS;
			break;
		case SocketOptionName_MulticastLoopback:
			*system_name = IPV6_MULTICAST_LOOP;
			break;
		case SocketOptionName_AddMembership:
			*system_name = IPV6_JOIN_GROUP;
			break;
		case SocketOptionName_DropMembership:
			*system_name = IPV6_LEAVE_GROUP;
			break;
		case SocketOptionName_PacketInformation:
#ifdef HAVE_IPV6_PKTINFO
			*system_name = IPV6_PKTINFO;
#endif
			break;
		case SocketOptionName_HeaderIncluded:
		case SocketOptionName_IPOptions:
		case SocketOptionName_TypeOfService:
		case SocketOptionName_DontFragment:
		case SocketOptionName_AddSourceMembership:
		case SocketOptionName_DropSourceMembership:
		case SocketOptionName_BlockSource:
		case SocketOptionName_UnblockSource:
			/* Can't figure out how to map these, so fall
			 * through
			 */
		default:
			g_warning("System.Net.Sockets.SocketOptionName 0x%x is not supported at IPv6 level", mono_name);
			return(-1);
		}

		break;	/* SocketOptionLevel_IPv6 */
		
	case SocketOptionLevel_Tcp:
	*system_level = mono_networking_get_tcp_protocol ();
		
		switch(mono_name) {
		case SocketOptionName_NoDelay:
			*system_name = TCP_NODELAY;
			break;
#if 0
			/* The documentation is talking complete
			 * bollocks here: rfc-1222 is titled
			 * 'Advancing the NSFNET Routing Architecture'
			 * and doesn't mention either of the words
			 * "expedite" or "urgent".
			 */
		case SocketOptionName_BsdUrgent:
		case SocketOptionName_Expedited:
#endif
		default:
			g_warning("System.Net.Sockets.SocketOptionName 0x%x is not supported at TCP level", mono_name);
			return(-1);
		}
		break;
		
	case SocketOptionLevel_Udp:
		g_warning("System.Net.Sockets.SocketOptionLevel has unsupported value 0x%x", mono_level);

		switch(mono_name) {
		case SocketOptionName_NoChecksum:
		case SocketOptionName_ChecksumCoverage:
		default:
			g_warning("System.Net.Sockets.SocketOptionName 0x%x is not supported at UDP level", mono_name);
			return(-1);
		}
		return(-1);
		break;

	default:
		g_warning("System.Net.Sockets.SocketOptionLevel has unknown value 0x%x", mono_level);
		return(-1);
	}

	return(0);
}

static MonoImage *get_socket_assembly (void)
{
	MonoDomain *domain = mono_domain_get ();
	
	if (domain->socket_assembly == NULL) {
		MonoImage *socket_assembly;

		socket_assembly = mono_image_loaded ("System");
		if (!socket_assembly) {
			MonoAssembly *sa = mono_assembly_open ("System.dll", NULL);
		
			if (!sa) {
				g_assert_not_reached ();
			} else {
				socket_assembly = mono_assembly_get_image (sa);
			}
		}
		mono_atomic_store_release (&domain->socket_assembly, socket_assembly);
	}
	
	return domain->socket_assembly;
}

static gint32 get_family_hint(void)
{
	MonoDomain *domain = mono_domain_get ();

	if (!domain->inet_family_hint) {
		MonoClass *socket_class;
		MonoClassField *ipv6_field, *ipv4_field;
		gint32 ipv6_enabled = -1, ipv4_enabled = -1;
		MonoVTable *vtable;

		socket_class = mono_class_from_name (get_socket_assembly (), "System.Net.Sockets", "Socket");
		ipv4_field = mono_class_get_field_from_name (socket_class, "ipv4_supported");
		ipv6_field = mono_class_get_field_from_name (socket_class, "ipv6_supported");
		vtable = mono_class_vtable (mono_domain_get (), socket_class);
		g_assert (vtable);
		mono_runtime_class_init (vtable);

		mono_field_static_get_value (vtable, ipv4_field, &ipv4_enabled);
		mono_field_static_get_value (vtable, ipv6_field, &ipv6_enabled);

		mono_domain_lock (domain);
		if (ipv4_enabled == 1 && ipv6_enabled == 1) {
			domain->inet_family_hint = 1;
		} else if (ipv4_enabled == 1) {
			domain->inet_family_hint = 2;
		} else {
			domain->inet_family_hint = 3;
		}
		mono_domain_unlock (domain);
	}
	switch (domain->inet_family_hint) {
	case 1: return PF_UNSPEC;
	case 2: return PF_INET;
	case 3: return PF_INET6;
	default:
		return PF_UNSPEC;
	}
}

gpointer ves_icall_System_Net_Sockets_Socket_Socket_internal(MonoObject *this, gint32 family, gint32 type, gint32 proto, gint32 *error)
{
	SOCKET sock;
	gint32 sock_family;
	gint32 sock_proto;
	gint32 sock_type;
	
	*error = 0;
	
	sock_family=convert_family(family);
	if(sock_family==-1) {
		*error = WSAEAFNOSUPPORT;
		return(NULL);
	}

	sock_proto=convert_proto(proto);
	if(sock_proto==-1) {
		*error = WSAEPROTONOSUPPORT;
		return(NULL);
	}
	
	sock_type=convert_type(type);
	if(sock_type==-1) {
		*error = WSAESOCKTNOSUPPORT;
		return(NULL);
	}
	
	sock = _wapi_socket (sock_family, sock_type, sock_proto,
			     NULL, 0, WSA_FLAG_OVERLAPPED);

	if(sock==INVALID_SOCKET) {
		*error = WSAGetLastError ();
		return(NULL);
	}

	return(GUINT_TO_POINTER (sock));
}

/* FIXME: the SOCKET parameter (here and in other functions in this
 * file) is really an IntPtr which needs to be converted to a guint32.
 */
void ves_icall_System_Net_Sockets_Socket_Close_internal(SOCKET sock,
							gint32 *error)
{
	LOGDEBUG (g_message ("%s: closing 0x%x", __func__, sock));

	*error = 0;

	/* Clear any pending work item from this socket if the underlying
	 * polling system does not notify when the socket is closed */
	mono_thread_pool_remove_socket (GPOINTER_TO_INT (sock));
	closesocket(sock);
}

gint32 ves_icall_System_Net_Sockets_SocketException_WSAGetLastError_internal(void)
{
	LOGDEBUG (g_message("%s: returning %d", __func__, WSAGetLastError()));

	return(WSAGetLastError());
}

gint32 ves_icall_System_Net_Sockets_Socket_Available_internal(SOCKET sock,
							      gint32 *error)
{
	int ret;
	int amount;
	
	*error = 0;

	/* FIXME: this might require amount to be unsigned long. */
	ret=ioctlsocket(sock, FIONREAD, &amount);
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
		return(0);
	}
	
	return(amount);
}

void ves_icall_System_Net_Sockets_Socket_Blocking_internal(SOCKET sock,
							   gboolean block,
							   gint32 *error)
{
	int ret;
	
	*error = 0;

	/*
	 * block == TRUE/FALSE means we will block/not block.
	 * But the ioctlsocket call takes TRUE/FALSE for non-block/block
	 */
	block = !block;
	
	ret = ioctlsocket (sock, FIONBIO, (gulong *) &block);
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}
}

gpointer ves_icall_System_Net_Sockets_Socket_Accept_internal(SOCKET sock,
							     gint32 *error,
							     gboolean blocking)
{
	SOCKET newsock;
	MonoInternalThread* curthread G_GNUC_UNUSED = mono_thread_internal_current ();
	MONO_PREPARE_BLOCKING
	
	*error = 0;
#ifdef HOST_WIN32
	{
		curthread->interrupt_on_stop = (gpointer)TRUE;
		newsock = _wapi_accept (sock, NULL, 0);
		curthread->interrupt_on_stop = (gpointer)FALSE;
	}
#else
	newsock = _wapi_accept (sock, NULL, 0);
#endif
	MONO_FINISH_BLOCKING

	if(newsock==INVALID_SOCKET) {
		*error = WSAGetLastError ();
		return(NULL);
	}
	
	return(GUINT_TO_POINTER (newsock));
}

void ves_icall_System_Net_Sockets_Socket_Listen_internal(SOCKET sock,
							 guint32 backlog,
							 gint32 *error)
{
	int ret;
	
	*error = 0;
	
	ret = _wapi_listen (sock, backlog);
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}
}

// Check whether it's ::ffff::0:0.
static gboolean
is_ipv4_mapped_any (const struct in6_addr *addr)
{
	int i;
	
	for (i = 0; i < 10; i++) {
		if (addr->s6_addr [i])
			return FALSE;
	}
	if ((addr->s6_addr [10] != 0xff) || (addr->s6_addr [11] != 0xff))
		return FALSE;
	for (i = 12; i < 16; i++) {
		if (addr->s6_addr [i])
			return FALSE;
	}
	return TRUE;
}

static MonoObject *create_object_from_sockaddr(struct sockaddr *saddr,
					       int sa_size, gint32 *error)
{
	MonoDomain *domain = mono_domain_get ();
	MonoObject *sockaddr_obj;
	MonoArray *data;
	MonoAddressFamily family;

	/* Build a System.Net.SocketAddress object instance */
	if (!domain->sockaddr_class) {
		domain->sockaddr_class=mono_class_from_name (get_socket_assembly (), "System.Net", "SocketAddress");
		g_assert (domain->sockaddr_class);
	}
	sockaddr_obj=mono_object_new(domain, domain->sockaddr_class);
	
	/* Locate the SocketAddress data buffer in the object */
	if (!domain->sockaddr_data_field) {
		domain->sockaddr_data_field=mono_class_get_field_from_name (domain->sockaddr_class, "data");
		g_assert (domain->sockaddr_data_field);
	}

	/* May be the +2 here is too conservative, as sa_len returns
	 * the length of the entire sockaddr_in/in6, including
	 * sizeof (unsigned short) of the family */
	/* We can't really avoid the +2 as all code below depends on this size - INCLUDING unix domain sockets.*/
	data=mono_array_new_cached(domain, mono_get_byte_class (), sa_size+2);

	/* The data buffer is laid out as follows:
	 * bytes 0 and 1 are the address family
	 * bytes 2 and 3 are the port info
	 * the rest is the address info
	 */
		
	family=convert_to_mono_family(saddr->sa_family);
	if(family==AddressFamily_Unknown) {
		*error = WSAEAFNOSUPPORT;
		return(NULL);
	}

	mono_array_set(data, guint8, 0, family & 0x0FF);
	mono_array_set(data, guint8, 1, (family >> 8) & 0x0FF);
	
	if(saddr->sa_family==AF_INET) {
		struct sockaddr_in *sa_in=(struct sockaddr_in *)saddr;
		guint16 port=ntohs(sa_in->sin_port);
		guint32 address=ntohl(sa_in->sin_addr.s_addr);
		
		if(sa_size<8) {
			mono_raise_exception((MonoException *)mono_exception_from_name(mono_get_corlib (), "System", "SystemException"));
		}
		
		mono_array_set(data, guint8, 2, (port>>8) & 0xff);
		mono_array_set(data, guint8, 3, (port) & 0xff);
		mono_array_set(data, guint8, 4, (address>>24) & 0xff);
		mono_array_set(data, guint8, 5, (address>>16) & 0xff);
		mono_array_set(data, guint8, 6, (address>>8) & 0xff);
		mono_array_set(data, guint8, 7, (address) & 0xff);
	
		mono_field_set_value (sockaddr_obj, domain->sockaddr_data_field, data);

		return(sockaddr_obj);
	} else if (saddr->sa_family == AF_INET6) {
		struct sockaddr_in6 *sa_in=(struct sockaddr_in6 *)saddr;
		int i;

		guint16 port=ntohs(sa_in->sin6_port);

		if(sa_size<28) {
			mono_raise_exception((MonoException *)mono_exception_from_name(mono_get_corlib (), "System", "SystemException"));
		}

		mono_array_set(data, guint8, 2, (port>>8) & 0xff);
		mono_array_set(data, guint8, 3, (port) & 0xff);
		
		if (is_ipv4_mapped_any (&sa_in->sin6_addr)) {
			// Map ::ffff:0:0 to :: (bug #5502)
			for(i=0; i<16; i++) {
				mono_array_set(data, guint8, 8+i, 0);
			}
		} else {
			for(i=0; i<16; i++) {
				mono_array_set(data, guint8, 8+i,
					       sa_in->sin6_addr.s6_addr[i]);
			}
		}

		mono_array_set(data, guint8, 24, sa_in->sin6_scope_id & 0xff);
		mono_array_set(data, guint8, 25,
			       (sa_in->sin6_scope_id >> 8) & 0xff);
		mono_array_set(data, guint8, 26,
			       (sa_in->sin6_scope_id >> 16) & 0xff);
		mono_array_set(data, guint8, 27,
			       (sa_in->sin6_scope_id >> 24) & 0xff);

		mono_field_set_value (sockaddr_obj, domain->sockaddr_data_field, data);

		return(sockaddr_obj);
#ifdef HAVE_SYS_UN_H
	} else if (saddr->sa_family == AF_UNIX) {
		int i;

		for (i = 0; i < sa_size; i++) {
			mono_array_set (data, guint8, i+2, saddr->sa_data[i]);
		}
		
		mono_field_set_value (sockaddr_obj, domain->sockaddr_data_field, data);

		return sockaddr_obj;
#endif
	} else {
		*error = WSAEAFNOSUPPORT;
		return(NULL);
	}
}

static int
get_sockaddr_size (int family)
{
	int size;

	size = 0;
	if (family == AF_INET) {
		size = sizeof (struct sockaddr_in);
	} else if (family == AF_INET6) {
		size = sizeof (struct sockaddr_in6);
#ifdef HAVE_SYS_UN_H
	} else if (family == AF_UNIX) {
		size = sizeof (struct sockaddr_un);
#endif
	}
	return size;
}

extern MonoObject *ves_icall_System_Net_Sockets_Socket_LocalEndPoint_internal(SOCKET sock, gint32 af, gint32 *error)
{
	gchar *sa;
	socklen_t salen;
	int ret;
	MonoObject *result;
	
	*error = 0;
	
	salen = get_sockaddr_size (convert_family (af));
	if (salen == 0) {
		*error = WSAEAFNOSUPPORT;
		return NULL;
	}
	sa = (salen <= 128) ? alloca (salen) : g_malloc0 (salen);
	MONO_PREPARE_BLOCKING
	ret = _wapi_getsockname (sock, (struct sockaddr *)sa, &salen);
	MONO_FINISH_BLOCKING
	
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
		if (salen > 128)
			g_free (sa);
		return(NULL);
	}
	
	LOGDEBUG (g_message("%s: bound to %s port %d", __func__, inet_ntoa(((struct sockaddr_in *)&sa)->sin_addr), ntohs(((struct sockaddr_in *)&sa)->sin_port)));

	result = create_object_from_sockaddr((struct sockaddr *)sa, salen, error);
	if (salen > 128)
		g_free (sa);
	return result;
}

extern MonoObject *ves_icall_System_Net_Sockets_Socket_RemoteEndPoint_internal(SOCKET sock, gint32 af, gint32 *error)
{
	gchar *sa;
	socklen_t salen;
	int ret;
	MonoObject *result;
	
	*error = 0;
	
	salen = get_sockaddr_size (convert_family (af));
	if (salen == 0) {
		*error = WSAEAFNOSUPPORT;
		return NULL;
	}
	sa = (salen <= 128) ? alloca (salen) : g_malloc0 (salen);
	/* Note: linux returns just 2 for AF_UNIX. Always. */
	MONO_PREPARE_BLOCKING
	ret = _wapi_getpeername (sock, (struct sockaddr *)sa, &salen);
	MONO_FINISH_BLOCKING
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
		if (salen > 128)
			g_free (sa);
		return(NULL);
	}
	
	LOGDEBUG (g_message("%s: connected to %s port %d", __func__, inet_ntoa(((struct sockaddr_in *)&sa)->sin_addr), ntohs(((struct sockaddr_in *)&sa)->sin_port)));

	result = create_object_from_sockaddr((struct sockaddr *)sa, salen, error);
	if (salen > 128)
		g_free (sa);
	return result;
}

static struct sockaddr *create_sockaddr_from_object(MonoObject *saddr_obj,
						    socklen_t *sa_size,
						    gint32 *error)
{
	MonoClassField *field;
	MonoArray *data;
	gint32 family;
	int len;

	/* Dig the SocketAddress data buffer out of the object */
	field=mono_class_get_field_from_name(saddr_obj->vtable->klass, "data");
	data=*(MonoArray **)(((char *)saddr_obj) + field->offset);

	/* The data buffer is laid out as follows:
	 * byte 0 is the address family low byte
	 * byte 1 is the address family high byte
	 * INET:
	 * 	bytes 2 and 3 are the port info
	 * 	the rest is the address info
	 * UNIX:
	 * 	the rest is the file name
	 */
	len = mono_array_length (data);
	if (len < 2) {
		mono_raise_exception (mono_exception_from_name(mono_get_corlib (), "System", "SystemException"));
	}
	
	family = convert_family (mono_array_get (data, guint8, 0) + (mono_array_get (data, guint8, 1) << 8));
	if (family == AF_INET) {
		struct sockaddr_in *sa;
		guint16 port;
		guint32 address;
		
		if (len < 8) {
			mono_raise_exception (mono_exception_from_name (mono_get_corlib (), "System", "SystemException"));
		}

		sa = g_new0 (struct sockaddr_in, 1);
		port = (mono_array_get (data, guint8, 2) << 8) +
			mono_array_get (data, guint8, 3);
		address = (mono_array_get (data, guint8, 4) << 24) +
			(mono_array_get (data, guint8, 5) << 16 ) +
			(mono_array_get (data, guint8, 6) << 8) +
			mono_array_get (data, guint8, 7);

		sa->sin_family = family;
		sa->sin_addr.s_addr = htonl (address);
		sa->sin_port = htons (port);

		*sa_size = sizeof(struct sockaddr_in);
		return((struct sockaddr *)sa);
	} else if (family == AF_INET6) {
		struct sockaddr_in6 *sa;
		int i;
		guint16 port;
		guint32 scopeid;
		
		if (len < 28) {
			mono_raise_exception (mono_exception_from_name (mono_get_corlib (), "System", "SystemException"));
		}

		sa = g_new0 (struct sockaddr_in6, 1);
		port = mono_array_get (data, guint8, 3) +
			(mono_array_get (data, guint8, 2) << 8);
		scopeid = mono_array_get (data, guint8, 24) + 
			(mono_array_get (data, guint8, 25) << 8) + 
			(mono_array_get (data, guint8, 26) << 16) + 
			(mono_array_get (data, guint8, 27) << 24);

		sa->sin6_family = family;
		sa->sin6_port = htons (port);
		sa->sin6_scope_id = scopeid;

		for(i=0; i<16; i++) {
			sa->sin6_addr.s6_addr[i] = mono_array_get (data, guint8, 8+i);
		}

		*sa_size = sizeof(struct sockaddr_in6);
		return((struct sockaddr *)sa);
#ifdef HAVE_SYS_UN_H
	} else if (family == AF_UNIX) {
		struct sockaddr_un *sock_un;
		int i;

		/* Need a byte for the '\0' terminator/prefix, and the first
		 * two bytes hold the SocketAddress family
		 */
		if (len - 2 >= sizeof(sock_un->sun_path)) {
			mono_raise_exception (mono_get_exception_index_out_of_range ());
		}
		
		sock_un = g_new0 (struct sockaddr_un, 1);

		sock_un->sun_family = family;
		for (i = 0; i < len - 2; i++) {
			sock_un->sun_path [i] = mono_array_get (data, guint8,
								i + 2);
		}
		
		*sa_size = len;

		return (struct sockaddr *)sock_un;
#endif
	} else {
		*error = WSAEAFNOSUPPORT;
		return(0);
	}
}

extern void ves_icall_System_Net_Sockets_Socket_Bind_internal(SOCKET sock, MonoObject *sockaddr, gint32 *error)
{
	struct sockaddr *sa;
	socklen_t sa_size;
	int ret;
	
	*error = 0;
	
	sa=create_sockaddr_from_object(sockaddr, &sa_size, error);
	if (*error != 0) {
		return;
	}

	LOGDEBUG (g_message("%s: binding to %s port %d", __func__, inet_ntoa(((struct sockaddr_in *)sa)->sin_addr), ntohs (((struct sockaddr_in *)sa)->sin_port)));

	ret = _wapi_bind (sock, sa, sa_size);
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}

	g_free(sa);
}

enum {
	SelectModeRead,
	SelectModeWrite,
	SelectModeError
};

MonoBoolean
ves_icall_System_Net_Sockets_Socket_Poll_internal (SOCKET sock, gint mode,
						   gint timeout, gint32 *error)
{
	MonoInternalThread *thread = NULL;
	mono_pollfd *pfds;
	int ret;
	time_t start;
	
	pfds = g_new0 (mono_pollfd, 1);
	pfds[0].fd = GPOINTER_TO_INT (sock);
	pfds[0].events = (mode == SelectModeRead) ? MONO_POLLIN :
		(mode == SelectModeWrite) ? MONO_POLLOUT :
		(MONO_POLLERR | MONO_POLLHUP | MONO_POLLNVAL);

	timeout = (timeout >= 0) ? (timeout / 1000) : -1;
	start = time (NULL);
	do {
		*error = 0;
		
		ret = mono_poll (pfds, 1, timeout);
		if (timeout > 0 && ret < 0) {
			int err = errno;
			int sec = time (NULL) - start;
			
			timeout -= sec * 1000;
			if (timeout < 0) {
				timeout = 0;
			}
			
			errno = err;
		}
		
		if (ret == -1 && errno == EINTR) {
			int leave = 0;

			if (thread == NULL) {
				thread = mono_thread_internal_current ();
			}
			
			leave = mono_thread_test_state (thread, ThreadState_AbortRequested | ThreadState_StopRequested);
			
			if (leave != 0) {
				g_free (pfds);
				return(FALSE);
			} else {
				/* Suspend requested? */
				mono_thread_interruption_checkpoint ();
			}
			errno = EINTR;
		}
	} while (ret == -1 && errno == EINTR);

	if (ret == -1) {
#ifdef HOST_WIN32
		*error = WSAGetLastError ();
#else
		*error = errno_to_WSA (errno, __func__);
#endif
		g_free (pfds);
		return(FALSE);
	}
	
	g_free (pfds);

	if (ret == 0) {
		return(FALSE);
	} else {
		return (TRUE);
	}
}

extern void ves_icall_System_Net_Sockets_Socket_Connect_internal(SOCKET sock, MonoObject *sockaddr, gint32 *error)
{
	struct sockaddr *sa;
	socklen_t sa_size;
	int ret;
	
	*error = 0;
	
	sa=create_sockaddr_from_object(sockaddr, &sa_size, error);
	if (*error != 0) {
		return;
	}
	
	LOGDEBUG (g_message("%s: connecting to %s port %d", __func__, inet_ntoa(((struct sockaddr_in *)sa)->sin_addr), ntohs (((struct sockaddr_in *)sa)->sin_port)));

	MONO_PREPARE_BLOCKING
	ret = _wapi_connect (sock, sa, sa_size);
	MONO_FINISH_BLOCKING

	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}

	g_free(sa);
}

/* These #defines from mswsock.h from wine.  Defining them here allows
 * us to build this file on a mingw box that doesn't know the magic
 * numbers, but still run on a newer windows box that does.
 */
#ifndef WSAID_DISCONNECTEX
#define WSAID_DISCONNECTEX {0x7fda2e11,0x8630,0x436f,{0xa0, 0x31, 0xf5, 0x36, 0xa6, 0xee, 0xc1, 0x57}}
typedef BOOL (WINAPI *LPFN_DISCONNECTEX)(SOCKET, LPOVERLAPPED, DWORD, DWORD);
#endif

#ifndef WSAID_TRANSMITFILE
#define WSAID_TRANSMITFILE {0xb5367df0,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
typedef BOOL (WINAPI *LPFN_TRANSMITFILE)(SOCKET, HANDLE, DWORD, DWORD, LPOVERLAPPED, LPTRANSMIT_FILE_BUFFERS, DWORD);
#endif

extern void ves_icall_System_Net_Sockets_Socket_Disconnect_internal(SOCKET sock, MonoBoolean reuse, gint32 *error)
{
	int ret;
	glong output_bytes = 0;
	GUID disco_guid = WSAID_DISCONNECTEX;
	GUID trans_guid = WSAID_TRANSMITFILE;
	LPFN_DISCONNECTEX _wapi_disconnectex = NULL;
	LPFN_TRANSMITFILE _wapi_transmitfile = NULL;
	gboolean bret;
	MONO_PREPARE_BLOCKING
	
	*error = 0;
	
	LOGDEBUG (g_message("%s: disconnecting from socket %p (reuse %d)", __func__, sock, reuse));

	/* I _think_ the extension function pointers need to be looked
	 * up for each socket.  FIXME: check the best way to store
	 * pointers to functions in managed objects that still works
	 * on 64bit platforms.
	 */
	ret = WSAIoctl (sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
			(void *)&disco_guid, sizeof(GUID),
			(void *)&_wapi_disconnectex, sizeof(void *),
			&output_bytes, NULL, NULL);
	if (ret != 0) {
		/* make sure that WSAIoctl didn't put crap in the
		 * output pointer
		 */
		_wapi_disconnectex = NULL;

		/*
		 * Use the SIO_GET_EXTENSION_FUNCTION_POINTER to
		 * determine the address of the disconnect method without
		 * taking a hard dependency on a single provider
		 * 
		 * For an explanation of why this is done, you can read
		 * the article at http://www.codeproject.com/internet/jbsocketserver3.asp
		 */
		ret = WSAIoctl (sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
				(void *)&trans_guid, sizeof(GUID),
				(void *)&_wapi_transmitfile, sizeof(void *),
				&output_bytes, NULL, NULL);
		if (ret != 0) {
			_wapi_transmitfile = NULL;
		}
	}

	if (_wapi_disconnectex != NULL) {
		bret = _wapi_disconnectex (sock, NULL, TF_REUSE_SOCKET, 0);
	} else if (_wapi_transmitfile != NULL) {
		bret = _wapi_transmitfile (sock, NULL, 0, 0, NULL, NULL,
					   TF_DISCONNECT | TF_REUSE_SOCKET);
	} else {
		*error = ERROR_NOT_SUPPORTED;
		bret = TRUE; //we don't want the following bret check to change *error
	}

	if (bret == FALSE) {
		*error = WSAGetLastError ();
	}

	MONO_FINISH_BLOCKING
}

gint32 ves_icall_System_Net_Sockets_Socket_Receive_internal(SOCKET sock, MonoArray *buffer, gint32 offset, gint32 count, gint32 flags, gint32 *error)
{
	int ret;
	guchar *buf;
	gint32 alen;
	int recvflags=0;
	MonoInternalThread* curthread G_GNUC_UNUSED = mono_thread_internal_current ();
	
	*error = 0;
	
	alen = mono_array_length (buffer);
	if (offset > alen - count) {
		return(0);
	}
	
	buf=mono_array_addr(buffer, guchar, offset);
	
	recvflags = convert_socketflags (flags);
	if (recvflags == -1) {
		*error = WSAEOPNOTSUPP;
		return (0);
	}

	MONO_PREPARE_BLOCKING
#ifdef HOST_WIN32
	{
		curthread->interrupt_on_stop = (gpointer)TRUE;
		ret = _wapi_recv (sock, buf, count, recvflags);
		curthread->interrupt_on_stop = (gpointer)FALSE;
	}
#else
	ret = _wapi_recv (sock, buf, count, recvflags);
#endif
	MONO_FINISH_BLOCKING

	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
		return(0);
	}

	return(ret);
}

gint32 ves_icall_System_Net_Sockets_Socket_Receive_array_internal(SOCKET sock, MonoArray *buffers, gint32 flags, gint32 *error)
{
	int ret, count;
	DWORD recv;
	WSABUF *wsabufs;
	DWORD recvflags = 0;
	
	*error = 0;
	
	wsabufs = mono_array_addr (buffers, WSABUF, 0);
	count = mono_array_length (buffers);
	
	recvflags = convert_socketflags (flags);
	if (recvflags == -1) {
		*error = WSAEOPNOTSUPP;
		return(0);
	}
	
	ret = WSARecv (sock, wsabufs, count, &recv, &recvflags, NULL, NULL);
	if (ret == SOCKET_ERROR) {
		*error = WSAGetLastError ();
		return(0);
	}
	
	return(recv);
}

gint32 ves_icall_System_Net_Sockets_Socket_ReceiveFrom_internal(SOCKET sock, MonoArray *buffer, gint32 offset, gint32 count, gint32 flags, MonoObject **sockaddr, gint32 *error)
{
	int ret;
	guchar *buf;
	gint32 alen;
	int recvflags=0;
	struct sockaddr *sa;
	socklen_t sa_size;
	
	*error = 0;
	
	alen = mono_array_length (buffer);
	if (offset > alen - count) {
		return(0);
	}

	sa=create_sockaddr_from_object(*sockaddr, &sa_size, error);
	if (*error != 0) {
		return(0);
	}
	
	buf=mono_array_addr(buffer, guchar, offset);
	
	recvflags = convert_socketflags (flags);
	if (recvflags == -1) {
		*error = WSAEOPNOTSUPP;
		return (0);
	}

	MONO_PREPARE_BLOCKING
	ret = _wapi_recvfrom (sock, buf, count, recvflags, sa, &sa_size);
	MONO_FINISH_BLOCKING

	if(ret==SOCKET_ERROR) {
		g_free(sa);
		*error = WSAGetLastError ();
		return(0);
	}

	/* If we didn't get a socket size, then we're probably a
	 * connected connection-oriented socket and the stack hasn't
	 * returned the remote address. All we can do is return null.
	 */
	if ( sa_size != 0 )
		*sockaddr=create_object_from_sockaddr(sa, sa_size, error);
	else
		*sockaddr=NULL;

	g_free(sa);
	
	return(ret);
}

gint32 ves_icall_System_Net_Sockets_Socket_Send_internal(SOCKET sock, MonoArray *buffer, gint32 offset, gint32 count, gint32 flags, gint32 *error)
{
	int ret;
	guchar *buf;
	gint32 alen;
	int sendflags=0;
	
	*error = 0;
	
	alen = mono_array_length (buffer);
	if (offset > alen - count) {
		return(0);
	}

	LOGDEBUG (g_message("%s: alen: %d", __func__, alen));
	
	buf=mono_array_addr(buffer, guchar, offset);

	LOGDEBUG (g_message("%s: Sending %d bytes", __func__, count));

	sendflags = convert_socketflags (flags);
	if (sendflags == -1) {
		*error = WSAEOPNOTSUPP;
		return (0);
	}

	MONO_PREPARE_BLOCKING
	ret = _wapi_send (sock, buf, count, sendflags);
	MONO_FINISH_BLOCKING
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
		return(0);
	}

	return(ret);
}

gint32 ves_icall_System_Net_Sockets_Socket_Send_array_internal(SOCKET sock, MonoArray *buffers, gint32 flags, gint32 *error)
{
	int ret, count;
	DWORD sent;
	WSABUF *wsabufs;
	DWORD sendflags = 0;
	
	*error = 0;
	
	wsabufs = mono_array_addr (buffers, WSABUF, 0);
	count = mono_array_length (buffers);
	
	sendflags = convert_socketflags (flags);
	if (sendflags == -1) {
		*error = WSAEOPNOTSUPP;
		return(0);
	}
	
	ret = WSASend (sock, wsabufs, count, &sent, sendflags, NULL, NULL);
	if (ret == SOCKET_ERROR) {
		*error = WSAGetLastError ();
		return(0);
	}
	
	return(sent);
}

gint32 ves_icall_System_Net_Sockets_Socket_SendTo_internal(SOCKET sock, MonoArray *buffer, gint32 offset, gint32 count, gint32 flags, MonoObject *sockaddr, gint32 *error)
{
	int ret;
	guchar *buf;
	gint32 alen;
	int sendflags=0;
	struct sockaddr *sa;
	socklen_t sa_size;
	
	*error = 0;
	
	alen = mono_array_length (buffer);
	if (offset > alen - count) {
		return(0);
	}

	sa=create_sockaddr_from_object(sockaddr, &sa_size, error);
	if(*error != 0) {
		return(0);
	}
	
	LOGDEBUG (g_message("%s: alen: %d", __func__, alen));
	
	buf=mono_array_addr(buffer, guchar, offset);

	LOGDEBUG (g_message("%s: Sending %d bytes", __func__, count));

	sendflags = convert_socketflags (flags);
	if (sendflags == -1) {
		*error = WSAEOPNOTSUPP;
		return (0);
	}

	MONO_PREPARE_BLOCKING
	ret = _wapi_sendto (sock, buf, count, sendflags, sa, sa_size);
	MONO_FINISH_BLOCKING
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}

	g_free(sa);
	
	return(ret);
}

static SOCKET Socket_to_SOCKET(MonoObject *sockobj)
{
	MonoSafeHandle *safe_handle;
	MonoClassField *field;
	
	field = mono_class_get_field_from_name (sockobj->vtable->klass, "safe_handle");
	safe_handle = ((MonoSafeHandle*) (*(gpointer *)(((char *)sockobj)+field->offset)));

	if (safe_handle == NULL)
		return -1;

	return (SOCKET) safe_handle->handle;
}

#define POLL_ERRORS (MONO_POLLERR | MONO_POLLHUP | MONO_POLLNVAL)
void ves_icall_System_Net_Sockets_Socket_Select_internal(MonoArray **sockets, gint32 timeout, gint32 *error)
{
	MonoInternalThread *thread = NULL;
	MonoObject *obj;
	mono_pollfd *pfds;
	int nfds, idx;
	int ret;
	int i, count;
	int mode;
	MonoClass *sock_arr_class;
	MonoArray *socks;
	time_t start;
	uintptr_t socks_size;
	
	/* *sockets -> READ, null, WRITE, null, ERROR, null */
	count = mono_array_length (*sockets);
	nfds = count - 3; /* NULL separators */
	pfds = g_new0 (mono_pollfd, nfds);
	mode = idx = 0;
	for (i = 0; i < count; i++) {
		obj = mono_array_get (*sockets, MonoObject *, i);
		if (obj == NULL) {
			mode++;
			continue;
		}

		if (idx >= nfds) {
			/* The socket array was bogus */
			g_free (pfds);
			*error = WSAEFAULT;
			return;
		}

		pfds [idx].fd = Socket_to_SOCKET (obj);
		pfds [idx].events = (mode == 0) ? MONO_POLLIN : (mode == 1) ? MONO_POLLOUT : POLL_ERRORS;
		idx++;
	}

	timeout = (timeout >= 0) ? (timeout / 1000) : -1;
	start = time (NULL);
	do {
		*error = 0;
		ret = mono_poll (pfds, nfds, timeout);
		if (timeout > 0 && ret < 0) {
			int err = errno;
			int sec = time (NULL) - start;

			timeout -= sec * 1000;
			if (timeout < 0)
				timeout = 0;
			errno = err;
		}

		if (ret == -1 && errno == EINTR) {
			int leave = 0;
			if (thread == NULL)
				thread = mono_thread_internal_current ();

			leave = mono_thread_test_state (thread, ThreadState_AbortRequested | ThreadState_StopRequested);
			
			if (leave != 0) {
				g_free (pfds);
				*sockets = NULL;
				return;
			} else {
				/* Suspend requested? */
				mono_thread_interruption_checkpoint ();
			}
			errno = EINTR;
		}
	} while (ret == -1 && errno == EINTR);
	
	if (ret == -1) {
#ifdef HOST_WIN32
		*error = WSAGetLastError ();
#else
		*error = errno_to_WSA (errno, __func__);
#endif
		g_free (pfds);
		return;
	}

	if (ret == 0) {
		g_free (pfds);
		*sockets = NULL;
		return;
	}

	sock_arr_class= ((MonoObject *)*sockets)->vtable->klass;
	socks_size = ((uintptr_t)ret) + 3; /* space for the NULL delimiters */
	socks = mono_array_new_full (mono_domain_get (), sock_arr_class, &socks_size, NULL);

	mode = idx = 0;
	for (i = 0; i < count && ret > 0; i++) {
		mono_pollfd *pfd;

		obj = mono_array_get (*sockets, MonoObject *, i);
		if (obj == NULL) {
			mode++;
			idx++;
			continue;
		}

		pfd = &pfds [i - mode];
		if (pfd->revents == 0)
			continue;

		ret--;
		if (mode == 0 && (pfd->revents & (MONO_POLLIN | POLL_ERRORS)) != 0) {
			mono_array_setref (socks, idx++, obj);
		} else if (mode == 1 && (pfd->revents & (MONO_POLLOUT | POLL_ERRORS)) != 0) {
			mono_array_setref (socks, idx++, obj);
		} else if ((pfd->revents & POLL_ERRORS) != 0) {
			mono_array_setref (socks, idx++, obj);
		}
	}

	*sockets = socks;
	g_free (pfds);
}

static MonoObject* int_to_object (MonoDomain *domain, int val)
{
	return mono_value_box (domain, mono_get_int32_class (), &val);
}


void ves_icall_System_Net_Sockets_Socket_GetSocketOption_obj_internal(SOCKET sock, gint32 level, gint32 name, MonoObject **obj_val, gint32 *error)
{
	int system_level = 0;
	int system_name = 0;
	int ret;
	int val;
	socklen_t valsize=sizeof(val);
	struct linger linger;
	socklen_t lingersize=sizeof(linger);
	int time_ms = 0;
	socklen_t time_ms_size = sizeof (time_ms);
#ifdef SO_PEERCRED
#  if defined(__OpenBSD__)
	struct sockpeercred cred;
#  else
	struct ucred cred;
#  endif
	socklen_t credsize = sizeof(cred);
#endif
	MonoDomain *domain=mono_domain_get();
	MonoObject *obj;
	MonoClass *obj_class;
	MonoClassField *field;
	
	*error = 0;
	
#if !defined(SO_EXCLUSIVEADDRUSE) && defined(SO_REUSEADDR)
	if (level == SocketOptionLevel_Socket && name == SocketOptionName_ExclusiveAddressUse) {
		system_level = SOL_SOCKET;
		system_name = SO_REUSEADDR;
		ret = 0;
	} else
#endif
	{

		ret = convert_sockopt_level_and_name (level, name, &system_level, &system_name);
	}

	if(ret==-1) {
		*error = WSAENOPROTOOPT;
		return;
	}
	if (ret == -2) {
		*obj_val = int_to_object (domain, 0);
		return;
	}
	
	/* No need to deal with MulticastOption names here, because
	 * you cant getsockopt AddMembership or DropMembership (the
	 * int getsockopt will error, causing an exception)
	 */
	switch(name) {
	case SocketOptionName_Linger:
	case SocketOptionName_DontLinger:
		ret = _wapi_getsockopt(sock, system_level, system_name, &linger,
			       &lingersize);
		break;
		
	case SocketOptionName_SendTimeout:
	case SocketOptionName_ReceiveTimeout:
		ret = _wapi_getsockopt (sock, system_level, system_name, (char *) &time_ms, &time_ms_size);
		break;

#ifdef SO_PEERCRED
	case SocketOptionName_PeerCred: 
		ret = _wapi_getsockopt (sock, system_level, system_name, &cred,
					&credsize);
		break;
#endif

	default:
		ret = _wapi_getsockopt (sock, system_level, system_name, &val,
			       &valsize);
	}
	
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
		return;
	}
	
	switch(name) {
	case SocketOptionName_Linger:
		/* build a System.Net.Sockets.LingerOption */
		obj_class=mono_class_from_name(get_socket_assembly (),
					       "System.Net.Sockets",
					       "LingerOption");
		obj=mono_object_new(domain, obj_class);
		
		/* Locate and set the fields "bool enabled" and "int
		 * seconds"
		 */
		field=mono_class_get_field_from_name(obj_class, "enabled");
		*(guint8 *)(((char *)obj)+field->offset)=linger.l_onoff;

		field=mono_class_get_field_from_name(obj_class, "seconds");
		*(guint32 *)(((char *)obj)+field->offset)=linger.l_linger;
		
		break;
		
	case SocketOptionName_DontLinger:
		/* construct a bool int in val - true if linger is off */
		obj = int_to_object (domain, !linger.l_onoff);
		break;
		
	case SocketOptionName_SendTimeout:
	case SocketOptionName_ReceiveTimeout:
		obj = int_to_object (domain, time_ms);
		break;

#ifdef SO_PEERCRED
	case SocketOptionName_PeerCred: 
	{
		/* build a Mono.Posix.PeerCred+PeerCredData if
		 * possible
		 */
		static MonoImage *mono_posix_image = NULL;
		MonoPeerCredData *cred_data;
		
		if (mono_posix_image == NULL) {
			mono_posix_image=mono_image_loaded ("Mono.Posix");
			if (!mono_posix_image) {
				MonoAssembly *sa = mono_assembly_open ("Mono.Posix.dll", NULL);
				if (!sa) {
					*error = WSAENOPROTOOPT;
					return;
				} else {
					mono_posix_image = mono_assembly_get_image (sa);
				}
			}
		}
		
		obj_class = mono_class_from_name(mono_posix_image,
						 "Mono.Posix",
						 "PeerCredData");
		obj = mono_object_new(domain, obj_class);
		cred_data = (MonoPeerCredData *)obj;
		cred_data->pid = cred.pid;
		cred_data->uid = cred.uid;
		cred_data->gid = cred.gid;
		break;
	}
#endif

	default:
#if !defined(SO_EXCLUSIVEADDRUSE) && defined(SO_REUSEADDR)
		if (level == SocketOptionLevel_Socket && name == SocketOptionName_ExclusiveAddressUse)
			val = val ? 0 : 1;
#endif
		obj = int_to_object (domain, val);
	}
	*obj_val=obj;
}

void ves_icall_System_Net_Sockets_Socket_GetSocketOption_arr_internal(SOCKET sock, gint32 level, gint32 name, MonoArray **byte_val, gint32 *error)
{
	int system_level = 0;
	int system_name = 0;
	int ret;
	guchar *buf;
	socklen_t valsize;
	
	*error = 0;
	
	ret=convert_sockopt_level_and_name(level, name, &system_level,
					   &system_name);
	if(ret==-1) {
		*error = WSAENOPROTOOPT;
		return;
	}
	if(ret==-2)
		return;

	valsize=mono_array_length(*byte_val);
	buf=mono_array_addr(*byte_val, guchar, 0);
	
	ret = _wapi_getsockopt (sock, system_level, system_name, buf, &valsize);
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}
}

#if defined(HAVE_STRUCT_IP_MREQN) || defined(HAVE_STRUCT_IP_MREQ)
static struct in_addr ipaddress_to_struct_in_addr(MonoObject *ipaddr)
{
	struct in_addr inaddr;
	MonoClassField *field;
	
	field=mono_class_get_field_from_name(ipaddr->vtable->klass, "m_Address");

	/* No idea why .net uses a 64bit type to hold a 32bit value...
	 *
	 * Internal value of IPAddess is in little-endian order
	 */
	inaddr.s_addr=GUINT_FROM_LE ((guint32)*(guint64 *)(((char *)ipaddr)+field->offset));
	
	return(inaddr);
}

static struct in6_addr ipaddress_to_struct_in6_addr(MonoObject *ipaddr)
{
	struct in6_addr in6addr;
	MonoClassField *field;
	MonoArray *data;
	int i;

	field=mono_class_get_field_from_name(ipaddr->vtable->klass, "m_Numbers");
	data=*(MonoArray **)(((char *)ipaddr) + field->offset);

/* Solaris has only the 8 bit version. */
#ifndef s6_addr16
	for(i=0; i<8; i++) {
		guint16 s = mono_array_get (data, guint16, i);
		in6addr.s6_addr[2 * i + 1] = (s >> 8) & 0xff;
		in6addr.s6_addr[2 * i] = s & 0xff;
	}
#else
	for(i=0; i<8; i++)
		in6addr.s6_addr16[i] = mono_array_get (data, guint16, i);
#endif
	return(in6addr);
}
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)

#if defined(HAVE_GETIFADDRS) && defined(HAVE_IF_NAMETOINDEX)
static int
get_local_interface_id (int family)
{
	struct ifaddrs *ifap = NULL, *ptr;
	int idx = 0;
	
	if (getifaddrs (&ifap)) {
		return 0;
	}
	
	for (ptr = ifap; ptr; ptr = ptr->ifa_next) {
		if (!ptr->ifa_addr || !ptr->ifa_name)
			continue;
		if (ptr->ifa_addr->sa_family != family)
			continue;
		if ((ptr->ifa_flags & IFF_LOOPBACK) != 0)
			continue;
		if ((ptr->ifa_flags & IFF_MULTICAST) == 0)
			continue;
			
		idx = if_nametoindex (ptr->ifa_name);
		break;
	}
	
	freeifaddrs (ifap);
	return idx;
}
#else
static int
get_local_interface_id (int family)
{
	return 0;
}
#endif

#endif /* defined(__APPLE__) || defined(__FreeBSD__) */

void ves_icall_System_Net_Sockets_Socket_SetSocketOption_internal(SOCKET sock, gint32 level, gint32 name, MonoObject *obj_val, MonoArray *byte_val, gint32 int_val, gint32 *error)
{
	struct linger linger;
	int system_level = 0;
	int system_name = 0;
	int ret;
	int sol_ip;
	int sol_ipv6;

	*error = 0;

	sol_ipv6 = mono_networking_get_ipv6_protocol ();
	sol_ip = mono_networking_get_ip_protocol ();

	ret=convert_sockopt_level_and_name(level, name, &system_level,
					   &system_name);

#if !defined(SO_EXCLUSIVEADDRUSE) && defined(SO_REUSEADDR)
	if (level == SocketOptionLevel_Socket && name == SocketOptionName_ExclusiveAddressUse) {
		system_name = SO_REUSEADDR;
		int_val = int_val ? 0 : 1;
		ret = 0;
	}
#endif

	if(ret==-1) {
		*error = WSAENOPROTOOPT;
		return;
	}
	if(ret==-2){
		return;
	}

	/* Only one of obj_val, byte_val or int_val has data */
	if(obj_val!=NULL) {
		MonoClassField *field;
		int valsize;
		
		switch(name) {
		case SocketOptionName_Linger:
			/* Dig out "bool enabled" and "int seconds"
			 * fields
			 */
			field=mono_class_get_field_from_name(obj_val->vtable->klass, "enabled");
			linger.l_onoff=*(guint8 *)(((char *)obj_val)+field->offset);
			field=mono_class_get_field_from_name(obj_val->vtable->klass, "seconds");
			linger.l_linger=*(guint32 *)(((char *)obj_val)+field->offset);
			
			valsize=sizeof(linger);
			ret = _wapi_setsockopt (sock, system_level,
						system_name, &linger, valsize);
			break;
		case SocketOptionName_AddMembership:
		case SocketOptionName_DropMembership:
#if defined(HAVE_STRUCT_IP_MREQN) || defined(HAVE_STRUCT_IP_MREQ)
		{
			MonoObject *address = NULL;

			if(system_level == sol_ipv6) {
				struct ipv6_mreq mreq6;

				/*
				 *	Get group address
				 */
				field = mono_class_get_field_from_name (obj_val->vtable->klass, "group");
				address = *(gpointer *)(((char *)obj_val) + field->offset);
				
				if(address) {
					mreq6.ipv6mr_multiaddr = ipaddress_to_struct_in6_addr (address);
				}

				field=mono_class_get_field_from_name(obj_val->vtable->klass, "ifIndex");
				mreq6.ipv6mr_interface =*(guint64 *)(((char *)obj_val)+field->offset);
				
#if defined(__APPLE__) || defined(__FreeBSD__)
				/*
				* Bug #5504:
				*
				* Mac OS Lion doesn't allow ipv6mr_interface = 0.
				*
				* Tests on Windows and Linux show that the multicast group is only
				* joined on one NIC when interface = 0, so we simply use the interface
				* id from the first non-loopback interface (this is also what
				* Dns.GetHostName (string.Empty) would return).
				*/
				if (!mreq6.ipv6mr_interface)
					mreq6.ipv6mr_interface = get_local_interface_id (AF_INET6);
#endif
					
				ret = _wapi_setsockopt (sock, system_level,
							system_name, &mreq6,
							sizeof (mreq6));
			} else if(system_level == sol_ip) {
#ifdef HAVE_STRUCT_IP_MREQN
				struct ip_mreqn mreq = {{0}};
#else
				struct ip_mreq mreq = {{0}};
#endif /* HAVE_STRUCT_IP_MREQN */
			
				/* pain! MulticastOption holds two IPAddress
				 * members, so I have to dig the value out of
				 * those :-(
				 */
				field = mono_class_get_field_from_name (obj_val->vtable->klass, "group");
				address = *(gpointer *)(((char *)obj_val) + field->offset);

				/* address might not be defined and if so, set the address to ADDR_ANY.
				 */
				if(address) {
					mreq.imr_multiaddr = ipaddress_to_struct_in_addr (address);
				}

				field = mono_class_get_field_from_name (obj_val->vtable->klass, "local");
				address = *(gpointer *)(((char *)obj_val) + field->offset);

#ifdef HAVE_STRUCT_IP_MREQN
				if(address) {
					mreq.imr_address = ipaddress_to_struct_in_addr (address);
				}

				field = mono_class_get_field_from_name(obj_val->vtable->klass, "iface_index");
				mreq.imr_ifindex = *(gint32 *)(((char *)obj_val)+field->offset);
#else
				if(address) {
					mreq.imr_interface = ipaddress_to_struct_in_addr (address);
				}
#endif /* HAVE_STRUCT_IP_MREQN */

				ret = _wapi_setsockopt (sock, system_level,
							system_name, &mreq,
							sizeof (mreq));
			}
			break;
		}
#endif /* HAVE_STRUCT_IP_MREQN || HAVE_STRUCT_IP_MREQ */
		default:
			/* Cause an exception to be thrown */
			*error = WSAEINVAL;
			return;
		}
	} else if (byte_val!=NULL) {
		int valsize = mono_array_length (byte_val);
		guchar *buf = mono_array_addr (byte_val, guchar, 0);
		
		switch(name) {
		case SocketOptionName_DontLinger:
			if (valsize == 1) {
				linger.l_onoff = (*buf) ? 0 : 1;
				linger.l_linger = 0;
				ret = _wapi_setsockopt (sock, system_level, system_name, &linger, sizeof (linger));
			} else {
				*error = WSAEINVAL;
			}
			break;
		default:
			ret = _wapi_setsockopt (sock, system_level, system_name, buf, valsize);
			break;
		}
	} else {
		/* ReceiveTimeout/SendTimeout get here */
		switch(name) {
		case SocketOptionName_DontLinger:
			linger.l_onoff = !int_val;
			linger.l_linger = 0;
			ret = _wapi_setsockopt (sock, system_level, system_name, &linger, sizeof (linger));
			break;
		case SocketOptionName_MulticastInterface:
#ifndef HOST_WIN32
#ifdef HAVE_STRUCT_IP_MREQN
			int_val = GUINT32_FROM_BE (int_val);
			if ((int_val & 0xff000000) == 0) {
				/* int_val is interface index */
				struct ip_mreqn mreq = {{0}};
				mreq.imr_ifindex = int_val;
				ret = _wapi_setsockopt (sock, system_level, system_name, (char *) &mreq, sizeof (mreq));
				break;
			}
			int_val = GUINT32_TO_BE (int_val);
#endif /* HAVE_STRUCT_IP_MREQN */
#endif /* HOST_WIN32 */
			/* int_val is in_addr */
			ret = _wapi_setsockopt (sock, system_level, system_name, (char *) &int_val, sizeof (int_val));
			break;
		case SocketOptionName_DontFragment:
#ifdef HAVE_IP_MTU_DISCOVER
			/* Fiddle with the value slightly if we're
			 * turning DF on
			 */
			if (int_val == 1) {
				int_val = IP_PMTUDISC_DO;
			}
			/* Fall through */
#endif
			
		default:
			ret = _wapi_setsockopt (sock, system_level, system_name, (char *) &int_val, sizeof (int_val));
		}
	}

	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}
}

void ves_icall_System_Net_Sockets_Socket_Shutdown_internal(SOCKET sock,
							   gint32 how,
							   gint32 *error)
{
	int ret;
	MONO_PREPARE_BLOCKING

	*error = 0;
	
	/* Currently, the values for how (recv=0, send=1, both=2) match
	 * the BSD API
	 */
	ret = _wapi_shutdown (sock, how);
	if(ret==SOCKET_ERROR) {
		*error = WSAGetLastError ();
	}

	MONO_FINISH_BLOCKING
}

gint
ves_icall_System_Net_Sockets_Socket_IOControl_internal (SOCKET sock, gint32 code,
					      MonoArray *input,
					      MonoArray *output, gint32 *error)
{
	glong output_bytes = 0;
	gchar *i_buffer, *o_buffer;
	gint i_len, o_len;
	gint ret;

	*error = 0;
	
	if ((guint32)code == FIONBIO) {
		/* Invalid command. Must use Socket.Blocking */
		return -1;
	}

	if (input == NULL) {
		i_buffer = NULL;
		i_len = 0;
	} else {
		i_buffer = mono_array_addr (input, gchar, 0);
		i_len = mono_array_length (input);
	}

	if (output == NULL) {
		o_buffer = NULL;
		o_len = 0;
	} else {
		o_buffer = mono_array_addr (output, gchar, 0);
		o_len = mono_array_length (output);
	}

	ret = WSAIoctl (sock, code, i_buffer, i_len, o_buffer, o_len, &output_bytes, NULL, NULL);
	if (ret == SOCKET_ERROR) {
		*error = WSAGetLastError ();
		return(-1);
	}

	return (gint) output_bytes;
}

static gboolean 
addrinfo_to_IPHostEntry(MonoAddressInfo *info, MonoString **h_name,
						MonoArray **h_aliases,
						MonoArray **h_addr_list,
						gboolean add_local_ips)
{
	gint32 count, i;
	MonoAddressEntry *ai = NULL;
	struct in_addr *local_in = NULL;
	int nlocal_in = 0;
	struct in6_addr *local_in6 = NULL;
	int nlocal_in6 = 0;
	int addr_index;

	MonoDomain *domain = mono_domain_get ();

	addr_index = 0;
	*h_aliases=mono_array_new(domain, mono_get_string_class (), 0);
	if (add_local_ips) {
		local_in = (struct in_addr *) mono_get_local_interfaces (AF_INET, &nlocal_in);
		local_in6 = (struct in6_addr *) mono_get_local_interfaces (AF_INET6, &nlocal_in6);
		if (nlocal_in || nlocal_in6) {
			char addr [INET6_ADDRSTRLEN];
			*h_addr_list=mono_array_new(domain, mono_get_string_class (), nlocal_in + nlocal_in6);
			if (nlocal_in) {
				MonoString *addr_string;
				int i;

				for (i = 0; i < nlocal_in; i++) {
					MonoAddress maddr;
					mono_address_init (&maddr, AF_INET, &local_in [i]);
					if (mono_networking_addr_to_str (&maddr, addr, sizeof (addr))) {
						addr_string = mono_string_new (domain, addr);
						mono_array_setref (*h_addr_list, addr_index, addr_string);
						addr_index++;
					}
				}
			}

			if (nlocal_in6) {
				MonoString *addr_string;
				int i;

				for (i = 0; i < nlocal_in6; i++) {
					MonoAddress maddr;
					mono_address_init (&maddr, AF_INET6, &local_in6 [i]);
					if (mono_networking_addr_to_str (&maddr, addr, sizeof (addr))) {
						addr_string = mono_string_new (domain, addr);
						mono_array_setref (*h_addr_list, addr_index, addr_string);
						addr_index++;
					}
				}
			}

			g_free (local_in);
			g_free (local_in6);
			if (info) {
				mono_free_address_info (info);
			}
			return TRUE;
		}

		g_free (local_in);
		g_free (local_in6);
	}

	for (count = 0, ai = info->entries; ai != NULL; ai = ai->next) {
		if (ai->family != AF_INET && ai->family != AF_INET6)
			continue;

		count++;
	}

	*h_addr_list=mono_array_new(domain, mono_get_string_class (), count);

	for (ai = info->entries, i = 0; ai != NULL; ai = ai->next) {
		MonoAddress maddr;
		MonoString *addr_string;
		char buffer [INET6_ADDRSTRLEN]; /* Max. size for IPv6 */

		if((ai->family != PF_INET) && (ai->family != PF_INET6)) {
			continue;
		}

		mono_address_init (&maddr, ai->family, &ai->address);
		if(mono_networking_addr_to_str (&maddr, buffer, sizeof (buffer))) {
			addr_string=mono_string_new(domain, buffer);
		} else {
			addr_string=mono_string_new(domain, "");
		}

		mono_array_setref (*h_addr_list, addr_index, addr_string);

		if(!i) {
			i++;
			if (ai->canonical_name != NULL) {
				*h_name=mono_string_new(domain, ai->canonical_name);
			} else {
				*h_name=mono_string_new(domain, buffer);
			}
		}

		addr_index++;
	}

	if(info) {
		mono_free_address_info (info);
	}

	return(TRUE);
}

static int
get_addrinfo_family_hint (void)
{
	switch (get_family_hint ()) {
	case PF_UNSPEC: return MONO_HINT_UNSPECIFIED;
	case PF_INET: return MONO_HINT_IPV4;
#ifdef PF_INET6
	case PF_INET6: return MONO_HINT_IPV6;
#endif
	default:
		g_error ("invalid hint");
		return 0;
	}
}

MonoBoolean ves_icall_System_Net_Dns_GetHostByName_internal(MonoString *host, MonoString **h_name, MonoArray **h_aliases, MonoArray **h_addr_list)
{
	gboolean add_local_ips = FALSE, add_info_ok = TRUE;
	gchar this_hostname [256];
	MonoAddressInfo *info = NULL;
	char *hostname = mono_string_to_utf8 (host);
	MONO_PREPARE_BLOCKING

	if (*hostname == '\0') {
		add_local_ips = TRUE;
		*h_name = host;
	}
	if (!add_local_ips && gethostname (this_hostname, sizeof (this_hostname)) != -1) {
		if (!strcmp (hostname, this_hostname)) {
			add_local_ips = TRUE;
			*h_name = host;
		}
	}

	if (*hostname && mono_get_address_info (hostname, 0, MONO_HINT_CANONICAL_NAME | get_addrinfo_family_hint (), &info))
		add_info_ok = FALSE;

	g_free(hostname);
	MONO_FINISH_BLOCKING

	if (add_info_ok)
		return addrinfo_to_IPHostEntry(info, h_name, h_aliases, h_addr_list, add_local_ips);
	return FALSE;
}

extern MonoBoolean ves_icall_System_Net_Dns_GetHostByAddr_internal(MonoString *addr, MonoString **h_name, MonoArray **h_aliases, MonoArray **h_addr_list)
{
	char *address;
	struct sockaddr_in saddr;
	struct sockaddr_in6 saddr6;
	MonoAddressInfo *info = NULL;
	gint32 family;
	char hostname[NI_MAXHOST] = {0};
	int flags = 0;

	address = mono_string_to_utf8 (addr);

	if (inet_pton (AF_INET, address, &saddr.sin_addr ) <= 0) {
		/* Maybe an ipv6 address */
		if (inet_pton (AF_INET6, address, &saddr6.sin6_addr) <= 0) {
			g_free (address);
			return FALSE;
		}
		else {
			family = AF_INET6;
			saddr6.sin6_family = AF_INET6;
		}
	}
	else {
		family = AF_INET;
		saddr.sin_family = AF_INET;
	}
	g_free(address);

	if(family == AF_INET) {
#if HAVE_SOCKADDR_IN_SIN_LEN
		saddr.sin_len = sizeof (saddr);
#endif
		if(getnameinfo ((struct sockaddr*)&saddr, sizeof(saddr),
				hostname, sizeof(hostname), NULL, 0,
				flags) != 0) {
			return(FALSE);
		}
	} else if(family == AF_INET6) {
#if HAVE_SOCKADDR_IN6_SIN_LEN
		saddr6.sin6_len = sizeof (saddr6);
#endif
		if(getnameinfo ((struct sockaddr*)&saddr6, sizeof(saddr6),
				hostname, sizeof(hostname), NULL, 0,
				flags) != 0) {
			return(FALSE);
		}
	}

	if (mono_get_address_info (hostname, 0, get_addrinfo_family_hint () | MONO_HINT_CANONICAL_NAME | MONO_HINT_CONFIGURED_ONLY, &info))
		return FALSE;

	return(addrinfo_to_IPHostEntry (info, h_name, h_aliases, h_addr_list, FALSE));
}

extern MonoBoolean ves_icall_System_Net_Dns_GetHostName_internal(MonoString **h_name)
{
	gchar hostname[256];
	int ret;
	
	ret = gethostname (hostname, sizeof (hostname));
	if(ret==-1) {
		return(FALSE);
	}
	
	*h_name=mono_string_new(mono_domain_get (), hostname);

	return(TRUE);
}

gboolean
ves_icall_System_Net_Sockets_Socket_SendFile_internal (SOCKET sock, MonoString *filename, MonoArray *pre_buffer, MonoArray *post_buffer, gint flags)
{
	HANDLE file;
	gint32 error;
	TRANSMIT_FILE_BUFFERS buffers;

	if (filename == NULL)
		return FALSE;

	file = ves_icall_System_IO_MonoIO_Open (filename, FileMode_Open, FileAccess_Read, FileShare_Read, 0, &error);
	if (file == INVALID_HANDLE_VALUE) {
		SetLastError (error);
		return FALSE;
	}

	memset (&buffers, 0, sizeof (buffers));
	if (pre_buffer != NULL) {
		buffers.Head = mono_array_addr (pre_buffer, guchar, 0);
		buffers.HeadLength = mono_array_length (pre_buffer);
	}
	if (post_buffer != NULL) {
		buffers.Tail = mono_array_addr (post_buffer, guchar, 0);
		buffers.TailLength = mono_array_length (post_buffer);
	}

	if (!TransmitFile (sock, file, 0, 0, NULL, &buffers, flags)) {
		CloseHandle (file);
		return FALSE;
	}

	CloseHandle (file);
	return TRUE;
}

void mono_network_init(void)
{
	mono_networking_init ();
}

void mono_network_cleanup(void)
{
	_wapi_cleanup_networking ();
	mono_networking_shutdown ();
}

void
icall_cancel_blocking_socket_operation (MonoThread *thread)
{
	MonoInternalThread *internal = thread->internal_thread;
	
	if (mono_thread_info_new_interrupt_enabled ()) {
		mono_thread_info_abort_socket_syscall_for_close ((MonoNativeThreadId)(gsize)internal->tid);
	} else {
#ifndef HOST_WIN32
		internal->ignore_next_signal = TRUE;
		mono_thread_kill (internal, mono_thread_get_abort_signal ());		
#endif
	}
}

#endif /* #ifndef DISABLE_SOCKETS */
