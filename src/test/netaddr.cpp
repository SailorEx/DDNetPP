#include <gtest/gtest.h>

#include <base/system.h>

TEST(NetAddr, FromStr)
{
	NETADDR Addr;
	char aBuf1[NETADDR_MAXSTRSIZE];
	char aBuf2[NETADDR_MAXSTRSIZE];

	EXPECT_FALSE(net_addr_from_str(&Addr, "127.0.0.1"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "127.0.0.1:0");
	EXPECT_STREQ(aBuf2, "127.0.0.1");

	EXPECT_FALSE(net_addr_from_str(&Addr, "1.2.3.4:5678"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "1.2.3.4:5678");
	EXPECT_STREQ(aBuf2, "1.2.3.4");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[::1]"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[::1]:0");
	EXPECT_STREQ(aBuf2, "[::1]");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[0123:4567:89ab:cdef:1:2:3:4]:5678"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[123:4567:89ab:cdef:1:2:3:4]:5678");
	EXPECT_STREQ(aBuf2, "[123:4567:89ab:cdef:1:2:3:4]");
}

TEST(NetAddr, StrV6)
{
	NETADDR Addr;
	char aBuf1[NETADDR_MAXSTRSIZE];
	char aBuf2[NETADDR_MAXSTRSIZE];

	// Test vectors from RFC 5952 section 4:
	// https://tools.ietf.org/html/rfc5952#section-4
	// 4.1 Handling Leading Zeros in a 16-Bit Field
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:0db8::0001]:1"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[2001:db8::1]:1");
	EXPECT_STREQ(aBuf2, "[2001:db8::1]");

	// 4.2.1 Shorten as Much as Possible
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8:0:0:0:0:2:1]"));
	net_addr_str(&Addr, aBuf1, sizeof(aBuf1), true);
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf1, "[2001:db8::2:1]:0");
	EXPECT_STREQ(aBuf2, "[2001:db8::2:1]");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8::0:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:db8::1]");

	// 4.2.2 Handling One 16-Bit 0 Field
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8::1:1:1:1:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:db8:0:1:1:1:1:1]");

	// 4.2.3 Choice in Placement of "::"
	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:0:0:1:0:0:0:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:0:0:1::1]");

	EXPECT_FALSE(net_addr_from_str(&Addr, "[2001:db8:0:0:1:0:0:1]"));
	net_addr_str(&Addr, aBuf2, sizeof(aBuf2), false);
	EXPECT_STREQ(aBuf2, "[2001:db8::1:0:0:1]");
}

TEST(NetAddr, FromStrInvalid)
{
	NETADDR Addr;
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0."));
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0.1a"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "1.1"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::1"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0.1:"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::]:"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "127.0.0.1:1a"));
	EXPECT_TRUE(net_addr_from_str(&Addr, "[::]:c"));
}