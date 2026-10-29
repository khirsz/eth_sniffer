// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * swmirror - port mirroring configuration for the BCM5325E switch
 *            (D-Link DSL-2542B, BCM6338 SoC)
 *
 * Access to the switch registers:
 *
 *   swmirror --ioctl(SIOCxMIIREG)--> kernel: bcm63xx_enet --MDIO--> switch
 *
 * The kernel lets us read/write any MII register on the Ethernet
 * controller's MDIO bus through the SIOCGMIIREG/SIOCSMIIREG ioctls on eth0.
 * The bcm63xx_enet driver has no PHY attached on the 96338W board
 * (has_phy = 0), so it uses generic_mii_ioctl() and accepts any PHY address
 * (mask 0x3f), including 30 = the switch pseudo-PHY.
 *
 * The pseudo-PHY (address 0x1E) is a "window" into the whole switch register
 * space:
 *   reg 16 (0x10): page << 8 | 1 (enable access)
 *   reg 17 (0x11): address << 8 | op (op: 1 = write, 2 = read);
 *                  the switch clears the op bits when the operation is done
 *   reg 24 (0x18): data (16 bits; regs 25-27 for wider registers)
 * Same procedure as in Linux b53_mdio.c.
 *
 * Mirroring registers: BCM5325E datasheet (5325E-DS14-R), page 0x02:
 *   0x10 Mirror Capture Control (Table 64)
 *        bit 15     MIRROR_ENABLE
 *        bits 10:0  CAPTURE_PORT - a BITMASK (one bit = one port!)
 *   0x12 Ingress Mirror Control (Table 65)
 *        bit 13     IN_DIV_EN (sample every n-th frame, disabled here)
 *        bits 10:0  IN_MIRROR_MASK - ports whose RECEIVED traffic is copied
 *   0x1C Egress Mirror Control (Table 67)
 *        bits 10:0  OUT_MIRROR_MASK - ports whose TRANSMITTED traffic is copied
 * Port bits: 0-4 = 10/100 ports, 8 = MII/IMP (the router CPU).
 */

#include <errno.h>
#include <linux/mii.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define PSEUDO_PHY		0x1e
#define MII_PSEUDO_PAGE		16
#define MII_PSEUDO_ADDR		17
#define MII_PSEUDO_DATA0	24
#define PSEUDO_PAGE_ENABLE	0x0001
#define PSEUDO_OP_WRITE		0x0001
#define PSEUDO_OP_READ		0x0002
#define PSEUDO_OP_MASK		0x0003

/* Page 0x01: status */
#define PAGE_STATUS		0x01
#define REG_LINK_STATUS		0x00	/* 1 bit per port: link up */

/* Page 0x02: management and mirroring */
#define PAGE_MGMT		0x02
#define REG_MIRROR_CAPTURE	0x10
#define REG_MIRROR_INGRESS	0x12
#define REG_MIRROR_EGRESS	0x1c
#define MIRROR_ENABLE		0x8000
#define MIRROR_PORT_MASK	0x07ff

/* Switch port numbers. The mapping of the case sockets was verified
 * empirically (Link Status) and matches OpenWrt (02_network for bcm96338w). */
static const struct {
	const char *name;
	int port;
} port_names[] = {
	{ "lan1", 3 },
	{ "lan2", 2 },
	{ "lan3", 1 },
	{ "lan4", 0 },
	{ "cpu",  8 },	/* MII/IMP port: the switch <-> SoC link */
};
#define N_PORTS (sizeof(port_names) / sizeof(port_names[0]))

static int sock = -1;
static struct ifreq ifr;

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("swmirror: ", stderr);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

/* --- Layer 1: a single MDIO transaction through the kernel ------------- */

static struct mii_ioctl_data *mii_data(void)
{
	/* struct mii_ioctl_data is passed "inside" the ifreq,
	 * the kernel reads it via if_mii(ifr) = &ifr->ifr_ifru */
	return (struct mii_ioctl_data *)&ifr.ifr_ifru;
}

static uint16_t mdio_read(int phy, int reg)
{
	struct mii_ioctl_data *mii = mii_data();

	mii->phy_id = phy;
	mii->reg_num = reg;
	if (ioctl(sock, SIOCGMIIREG, &ifr) < 0)
		die("SIOCGMIIREG phy %d reg %d: %s", phy, reg, strerror(errno));
	return mii->val_out;
}

static void mdio_write(int phy, int reg, uint16_t val)
{
	struct mii_ioctl_data *mii = mii_data();

	mii->phy_id = phy;
	mii->reg_num = reg;
	mii->val_in = val;
	if (ioctl(sock, SIOCSMIIREG, &ifr) < 0)
		die("SIOCSMIIREG phy %d reg %d: %s", phy, reg, strerror(errno));
}

/* --- Layer 2: switch registers through the pseudo-PHY ------------------ */

static void sw_op(int page, int reg, int op)
{
	int i;

	mdio_write(PSEUDO_PHY, MII_PSEUDO_PAGE, (page << 8) | PSEUDO_PAGE_ENABLE);
	mdio_write(PSEUDO_PHY, MII_PSEUDO_ADDR, (reg << 8) | op);

	/* Wait for the switch to clear the operation bits. */
	for (i = 0; i < 100; i++) {
		if (!(mdio_read(PSEUDO_PHY, MII_PSEUDO_ADDR) & PSEUDO_OP_MASK))
			return;
		usleep(100);
	}
	die("switch did not complete the operation (page 0x%02x, reg 0x%02x)", page, reg);
}

static uint16_t sw_read16(int page, int reg)
{
	sw_op(page, reg, PSEUDO_OP_READ);
	return mdio_read(PSEUDO_PHY, MII_PSEUDO_DATA0);
}

static void sw_write16(int page, int reg, uint16_t val)
{
	mdio_write(PSEUDO_PHY, MII_PSEUDO_DATA0, val);
	sw_op(page, reg, PSEUDO_OP_WRITE);
}

/* --- Layer 3: mirroring logic ------------------------------------------- */

static const char *port_name(int port)
{
	size_t i;

	for (i = 0; i < N_PORTS; i++)
		if (port_names[i].port == port)
			return port_names[i].name;
	return "?";
}

static int parse_port(const char *s)
{
	size_t i;
	char *end;
	long v;

	for (i = 0; i < N_PORTS; i++)
		if (!strcasecmp(s, port_names[i].name))
			return port_names[i].port;

	/* or a raw switch port number: 0-4 or 8 */
	v = strtol(s, &end, 0);
	if (*s && !*end && ((v >= 0 && v <= 4) || v == 8))
		return (int)v;

	die("unknown port '%s' (use lan1..lan4, cpu or a number 0-4/8)", s);
	return -1;
}

/* "lan1,lan2" -> bitmask */
static uint16_t parse_port_list(const char *list)
{
	char buf[128], *tok, *save = NULL;
	uint16_t mask = 0;

	if (strlen(list) >= sizeof(buf))
		die("port list too long");
	strcpy(buf, list);
	for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
		mask |= 1u << parse_port(tok);
	if (!mask)
		die("empty port list");
	return mask;
}

static void print_mask(uint16_t mask)
{
	int p, first = 1;

	for (p = 0; p <= 10; p++) {
		if (!(mask & (1u << p)))
			continue;
		printf("%s%s", first ? "" : ",", port_name(p));
		first = 0;
	}
	if (first)
		printf("-");
}

static void cmd_status(void)
{
	uint16_t link = sw_read16(PAGE_STATUS, REG_LINK_STATUS);
	uint16_t cap = sw_read16(PAGE_MGMT, REG_MIRROR_CAPTURE);
	uint16_t in = sw_read16(PAGE_MGMT, REG_MIRROR_INGRESS);
	uint16_t eg = sw_read16(PAGE_MGMT, REG_MIRROR_EGRESS);
	size_t i;

	printf("Link status (0x%04x):", link);
	for (i = 0; i < N_PORTS; i++)
		printf(" %s=%s", port_names[i].name,
		       link & (1u << port_names[i].port) ? "up" : "down");
	printf("\n");

	printf("Mirroring:  %s\n", cap & MIRROR_ENABLE ? "ENABLED" : "disabled");
	printf("  capture  (0x%04x): ", cap);
	print_mask(cap & MIRROR_PORT_MASK);
	printf("\n  ingress  (0x%04x): ", in);
	print_mask(in & MIRROR_PORT_MASK);
	printf("\n  egress   (0x%04x): ", eg);
	print_mask(eg & MIRROR_PORT_MASK);
	printf("\n");
}

static void cmd_on(const char *to_s, const char *from_s, const char *dir)
{
	int to = parse_port(to_s);
	uint16_t from = parse_port_list(from_s);
	uint16_t in = 0, eg = 0;

	if (from & (1u << to))
		die("the monitor port (%s) cannot also be a source", to_s);

	if (!strcmp(dir, "rx") || !strcmp(dir, "both"))
		in = from;
	if (!strcmp(dir, "tx") || !strcmp(dir, "both"))
		eg = from;
	if (!in && !eg)
		die("direction must be rx, tx or both (got '%s')", dir);

	/* Masks first, the global enable last. */
	sw_write16(PAGE_MGMT, REG_MIRROR_INGRESS, in);
	sw_write16(PAGE_MGMT, REG_MIRROR_EGRESS, eg);
	sw_write16(PAGE_MGMT, REG_MIRROR_CAPTURE, MIRROR_ENABLE | (1u << to));

	/* Read back: did the switch accept the values? */
	if (sw_read16(PAGE_MGMT, REG_MIRROR_CAPTURE) != (MIRROR_ENABLE | (1u << to)) ||
	    sw_read16(PAGE_MGMT, REG_MIRROR_INGRESS) != in ||
	    sw_read16(PAGE_MGMT, REG_MIRROR_EGRESS) != eg)
		die("read-back does not match the written values");
}

static void cmd_off(void)
{
	sw_write16(PAGE_MGMT, REG_MIRROR_CAPTURE, 0);
	sw_write16(PAGE_MGMT, REG_MIRROR_INGRESS, 0);
	sw_write16(PAGE_MGMT, REG_MIRROR_EGRESS, 0);
}

static unsigned long parse_num(const char *s, unsigned long max)
{
	char *end;
	unsigned long v = strtoul(s, &end, 0);

	if (!*s || *end || v > max)
		die("invalid number '%s' (max 0x%lx)", s, max);
	return v;
}

static void usage(void)
{
	fputs(
"Usage: swmirror [-i IFACE] COMMAND\n"
"\n"
"  status                          port link and mirroring state\n"
"  on TO FROM[,FROM...] [rx|tx|both]\n"
"                                  copy traffic from ports FROM to port TO\n"
"  off                             disable mirroring\n"
"  read PAGE REG                   read a 16-bit switch register\n"
"  write PAGE REG VALUE            write a 16-bit switch register\n"
"\n"
"Ports: lan1..lan4 (labels on the case), cpu (the router CPU)\n"
"Example: swmirror on lan4 lan1 both\n"
"Default interface: eth0\n", stderr);
	exit(2);
}

int main(int argc, char **argv)
{
	const char *iface = "eth0";
	int argi = 1;

	if (argc > 2 && !strcmp(argv[1], "-i")) {
		iface = argv[2];
		argi = 3;
	}
	if (argi >= argc)
		usage();

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		die("socket: %s", strerror(errno));
	memset(&ifr, 0, sizeof(ifr));
	if (strlen(iface) >= sizeof(ifr.ifr_name))
		die("interface name too long");
	strcpy(ifr.ifr_name, iface);

	argv += argi;
	argc -= argi;

	if (!strcmp(argv[0], "status") && argc == 1) {
		cmd_status();
	} else if (!strcmp(argv[0], "on") && (argc == 3 || argc == 4)) {
		cmd_on(argv[1], argv[2], argc == 4 ? argv[3] : "both");
		cmd_status();
	} else if (!strcmp(argv[0], "off") && argc == 1) {
		cmd_off();
	} else if (!strcmp(argv[0], "read") && argc == 3) {
		printf("0x%04x\n", sw_read16(parse_num(argv[1], 0xff),
					     parse_num(argv[2], 0xff)));
	} else if (!strcmp(argv[0], "write") && argc == 4) {
		sw_write16(parse_num(argv[1], 0xff), parse_num(argv[2], 0xff),
			   parse_num(argv[3], 0xffff));
	} else {
		usage();
	}

	close(sock);
	return 0;
}
