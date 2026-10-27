// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * swmirror - konfiguracja port mirroringu w switchu BCM5325E
 *            (D-Link DSL-2542B, SoC BCM6338)
 *
 * Dostęp do rejestrów switcha:
 *
 *   swmirror --ioctl(SIOCxMIIREG)--> kernel: bcm63xx_enet --MDIO--> switch
 *
 * Kernel udostępnia zapis/odczyt dowolnego rejestru MII na magistrali MDIO
 * kontrolera ethernetu przez ioctl SIOCGMIIREG/SIOCSMIIREG na interfejsie
 * eth0. Sterownik bcm63xx_enet dla płytki 96338W nie ma podpiętego PHY
 * (has_phy = 0), więc używa generic_mii_ioctl() i przyjmuje dowolny adres
 * PHY (maska 0x3f), także 30 = pseudo-PHY switcha.
 *
 * Pseudo-PHY (adres 0x1E) to "okno" do całej przestrzeni rejestrów switcha:
 *   rej. 16 (0x10): strona << 8 | 1 (włącz dostęp)
 *   rej. 17 (0x11): adres << 8 | op (op: 1 = zapis, 2 = odczyt);
 *                   switch zeruje bity op po wykonaniu operacji
 *   rej. 24 (0x18): dane (16 bitów; rej. 25-27 dla szerszych rejestrów)
 * Ta sama procedura co w linuksowym b53_mdio.c.
 *
 * Rejestry mirroringu: datasheet BCM5325E (5325E-DS14-R), strona 0x02:
 *   0x10 Mirror Capture Control (Table 64)
 *        bit 15     MIRROR_ENABLE
 *        bity 10:0  CAPTURE_PORT - MASKA BITOWA (jeden bit = jeden port!)
 *   0x12 Ingress Mirror Control (Table 65)
 *        bit 13     IN_DIV_EN (próbkowanie co n-tą ramkę, tu wyłączone)
 *        bity 10:0  IN_MIRROR_MASK - porty, których ruch ODBIERANY kopiujemy
 *   0x1C Egress Mirror Control (Table 67)
 *        bity 10:0  OUT_MIRROR_MASK - porty, których ruch WYSYŁANY kopiujemy
 * Bity portów: 0-4 = porty 10/100, 8 = MII/IMP (CPU routera).
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

/* Strona 0x01: status */
#define PAGE_STATUS		0x01
#define REG_LINK_STATUS		0x00	/* 1 bit na port: link up */

/* Strona 0x02: zarządzanie i mirroring */
#define PAGE_MGMT		0x02
#define REG_MIRROR_CAPTURE	0x10
#define REG_MIRROR_INGRESS	0x12
#define REG_MIRROR_EGRESS	0x1c
#define MIRROR_ENABLE		0x8000
#define MIRROR_PORT_MASK	0x07ff

/* Numery portów switcha. Mapowanie gniazd na obudowie sprawdzone
 * empirycznie (Link Status) i zgodne z OpenWrt (02_network dla bcm96338w). */
static const struct {
	const char *name;
	int port;
} port_names[] = {
	{ "lan1", 3 },
	{ "lan2", 2 },
	{ "lan3", 1 },
	{ "lan4", 0 },
	{ "cpu",  8 },	/* port MII/IMP: łącze switch <-> SoC */
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

/* --- Warstwa 1: pojedyncza transakcja MDIO przez kernel ------------------ */

static struct mii_ioctl_data *mii_data(void)
{
	/* struct mii_ioctl_data jest przekazywana "w środku" ifreq,
	 * kernel czyta ją przez if_mii(ifr) = &ifr->ifr_ifru */
	return (struct mii_ioctl_data *)&ifr.ifr_ifru;
}

static uint16_t mdio_read(int phy, int reg)
{
	struct mii_ioctl_data *mii = mii_data();

	mii->phy_id = phy;
	mii->reg_num = reg;
	if (ioctl(sock, SIOCGMIIREG, &ifr) < 0)
		die("SIOCGMIIREG phy %d rej. %d: %s", phy, reg, strerror(errno));
	return mii->val_out;
}

static void mdio_write(int phy, int reg, uint16_t val)
{
	struct mii_ioctl_data *mii = mii_data();

	mii->phy_id = phy;
	mii->reg_num = reg;
	mii->val_in = val;
	if (ioctl(sock, SIOCSMIIREG, &ifr) < 0)
		die("SIOCSMIIREG phy %d rej. %d: %s", phy, reg, strerror(errno));
}

/* --- Warstwa 2: rejestry switcha przez pseudo-PHY ------------------------ */

static void sw_op(int page, int reg, int op)
{
	int i;

	mdio_write(PSEUDO_PHY, MII_PSEUDO_PAGE, (page << 8) | PSEUDO_PAGE_ENABLE);
	mdio_write(PSEUDO_PHY, MII_PSEUDO_ADDR, (reg << 8) | op);

	/* Czekamy, aż switch wyzeruje bity operacji. */
	for (i = 0; i < 100; i++) {
		if (!(mdio_read(PSEUDO_PHY, MII_PSEUDO_ADDR) & PSEUDO_OP_MASK))
			return;
		usleep(100);
	}
	die("switch nie zakończył operacji (strona 0x%02x, rej. 0x%02x)", page, reg);
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

/* --- Warstwa 3: logika mirroringu ---------------------------------------- */

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

	/* albo surowy numer portu switcha: 0-4 lub 8 */
	v = strtol(s, &end, 0);
	if (*s && !*end && ((v >= 0 && v <= 4) || v == 8))
		return (int)v;

	die("nieznany port '%s' (użyj lan1..lan4, cpu albo numeru 0-4/8)", s);
	return -1;
}

/* "lan1,lan2" -> maska bitowa */
static uint16_t parse_port_list(const char *list)
{
	char buf[128], *tok, *save = NULL;
	uint16_t mask = 0;

	if (strlen(list) >= sizeof(buf))
		die("za długa lista portów");
	strcpy(buf, list);
	for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
		mask |= 1u << parse_port(tok);
	if (!mask)
		die("pusta lista portów");
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

	printf("Mirroring:  %s\n", cap & MIRROR_ENABLE ? "WŁĄCZONY" : "wyłączony");
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
		die("port monitorujący (%s) nie może być jednocześnie źródłem", to_s);

	if (!strcmp(dir, "rx") || !strcmp(dir, "both"))
		in = from;
	if (!strcmp(dir, "tx") || !strcmp(dir, "both"))
		eg = from;
	if (!in && !eg)
		die("kierunek musi być rx, tx albo both (jest '%s')", dir);

	/* Najpierw maski, na końcu globalne włączenie. */
	sw_write16(PAGE_MGMT, REG_MIRROR_INGRESS, in);
	sw_write16(PAGE_MGMT, REG_MIRROR_EGRESS, eg);
	sw_write16(PAGE_MGMT, REG_MIRROR_CAPTURE, MIRROR_ENABLE | (1u << to));

	/* Odczyt kontrolny: czy switch przyjął wartości? */
	if (sw_read16(PAGE_MGMT, REG_MIRROR_CAPTURE) != (MIRROR_ENABLE | (1u << to)) ||
	    sw_read16(PAGE_MGMT, REG_MIRROR_INGRESS) != in ||
	    sw_read16(PAGE_MGMT, REG_MIRROR_EGRESS) != eg)
		die("odczyt kontrolny nie zgadza się z zapisem");
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
		die("niepoprawna liczba '%s' (max 0x%lx)", s, max);
	return v;
}

static void usage(void)
{
	fputs(
"Użycie: swmirror [-i IFACE] POLECENIE\n"
"\n"
"  status                         link portów i stan mirroringu\n"
"  on DO Z[,Z...] [rx|tx|both]    kopiuj ruch z portów Z na port DO\n"
"  off                            wyłącz mirroring\n"
"  read STRONA REJ                odczyt 16-bitowego rejestru switcha\n"
"  write STRONA REJ WARTOŚĆ       zapis 16-bitowego rejestru switcha\n"
"\n"
"Porty: lan1..lan4 (napisy na obudowie), cpu (procesor routera)\n"
"Przykład: swmirror on lan4 lan1 both\n"
"Domyślny interfejs: eth0\n", stderr);
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
		die("za długa nazwa interfejsu");
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
