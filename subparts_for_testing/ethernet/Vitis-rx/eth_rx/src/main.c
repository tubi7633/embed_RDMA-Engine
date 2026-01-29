#include "xparameters.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "sleep.h"

#include "xaxiethernet.h"
#include "xaxiethernet_hw.h"

/* -------------------------------------------------------------------------
 * Hardware IDs
 * ------------------------------------------------------------------------- */
#define ETH_DEV_ID          0

/* PHY address that works on your board */
#define PHY_ADDR_CONFIG     2

/* MAC address (pattern generator uses the same MAC) */
static u8 BoardMac[6] = {0x00, 0x0A, 0x35, 0x01, 0x02, 0x03};

/* PHY register addresses */
#define PHY_REG_BMCR   0
#define PHY_REG_BMSR   1

/* BMCR bits */
#define BMCR_AUTONEG_EN  0x1000
#define BMCR_RESTART_AN  0x0200

/* BMSR bits */
#define BMSR_LINK_STATUS  0x0004

/* ------------------------------------------------------------------------- */
/* BRAM side: AXI BRAM Controller base address
 * From Address Editor + xparameters.h:
 *   XPAR_AXI_BRAM_CTRL_0_BASEADDR = 0x82000000
 */
#define BRAM_BASEADDR   XPAR_AXI_BRAM_CTRL_0_BASEADDR
#define BRAM_WORDS      2048   /* 8 KB / 4 bytes = 2048 words */

static XAxiEthernet EthInst;

/* ------------------------------------------------------------------------- */
/* Simple BRAM dump (first N 32-bit words) */
static void DumpBram(int max_words)
{
    if (max_words > BRAM_WORDS)
        max_words = BRAM_WORDS;

    xil_printf("---- BRAM DUMP (first %d words) ----\r\n", max_words);

    for (int i = 0; i < max_words; i++) {
        u32 addr = BRAM_BASEADDR + 4U * (u32)i;
        u32 w    = Xil_In32(addr);

        char c0 = (char)((w      ) & 0xFF);
        char c1 = (char)((w >>  8) & 0xFF);
        char c2 = (char)((w >> 16) & 0xFF);
        char c3 = (char)((w >> 24) & 0xFF);

        /* Replace non-printable bytes with '.' */
        if (c0 < 32 || c0 > 126) c0 = '.';
        if (c1 < 32 || c1 > 126) c1 = '.';
        if (c2 < 32 || c2 > 126) c2 = '.';
        if (c3 < 32 || c3 > 126) c3 = '.';

        xil_printf("%03d: [0x%08lx] = 0x%08lx  | %c%c%c%c\r\n",
                   i,
                   (unsigned long)addr,
                   (unsigned long)w,
                   c0, c1, c2, c3);
    }

    xil_printf("---- END ----\r\n");
}

/* ------------------------------------------------------------------------- */
static int InitEth(int *FoundPhyAddr)
{
    int Status;
    XAxiEthernet_Config *EthCfg;
    u32 Options;
    int PhyAddr = -1;

    xil_printf("\r\n=== InitEth() started ===\r\n");

    xil_printf("ETH-1: XAxiEthernet_LookupConfig (ID=%d)\r\n", ETH_DEV_ID);
    EthCfg = XAxiEthernet_LookupConfig(ETH_DEV_ID);
    if (!EthCfg) {
        xil_printf("  [ERR] LookupConfig FAILED\r\n");
        return XST_FAILURE;
    }
    xil_printf("  BaseAddress = 0x%08lx\r\n",
               (unsigned long)EthCfg->BaseAddress);

    xil_printf("ETH-2: CfgInitialize\r\n");
    Status = XAxiEthernet_CfgInitialize(&EthInst, EthCfg,
                                        EthCfg->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("  [ERR] CfgInitialize FAILED (%d)\r\n", Status);
        return Status;
    }

    xil_printf("ETH-2.5: SetOperatingSpeed 1000 Mbps\r\n");
    Status = XAxiEthernet_SetOperatingSpeed(&EthInst, XAE_SPEED_1000_MBPS);
    if (Status != XST_SUCCESS)
        xil_printf("  [WARN] SetOperatingSpeed FAILED (%d)\r\n", Status);

    xil_printf("ETH-3: SetMacAddress\r\n");
    Status = XAxiEthernet_SetMacAddress(&EthInst, BoardMac);
    if (Status != XST_SUCCESS) {
        xil_printf("  [ERR] SetMacAddress FAILED (%d)\r\n", Status);
        return Status;
    }

    xil_printf("ETH-4: Configure options\r\n");
    Options = XAxiEthernet_GetOptions(&EthInst);
    xil_printf("  Default Options = 0x%08lx\r\n", (unsigned long)Options);

    Options |=  XAE_FLOW_CONTROL_OPTION
             |  XAE_TRANSMITTER_ENABLE_OPTION
             |  XAE_RECEIVER_ENABLE_OPTION
             |  XAE_FCS_STRIP_OPTION;

    XAxiEthernet_SetOptions(&EthInst, Options);
    XAxiEthernet_ClearOptions(&EthInst, ~Options);

    xil_printf("  New Options = 0x%08lx\r\n", (unsigned long)Options);

    /* PHY init */
    xil_printf("ETH-5: PHY init\r\n");

    PhyAddr = PHY_ADDR_CONFIG;
    {
        u16 Bmsr;
        XAxiEthernet_PhyRead(&EthInst, PhyAddr, PHY_REG_BMSR, &Bmsr);
        xil_printf("  PHY addr %d: BMSR = 0x%04x\r\n",
                   PhyAddr, (unsigned)Bmsr);
    }

    xil_printf("ETH-6: Enable/restart PHY autonegotiation\r\n");
    {
        u16 Bmcr;
        XAxiEthernet_PhyRead(&EthInst, PhyAddr, PHY_REG_BMCR, &Bmcr);
        xil_printf("  BMCR old = 0x%04x\r\n", Bmcr);
        Bmcr |= BMCR_AUTONEG_EN | BMCR_RESTART_AN;
        XAxiEthernet_PhyWrite(&EthInst, PhyAddr, PHY_REG_BMCR, Bmcr);
        xil_printf("  BMCR new written = 0x%04x\r\n", Bmcr);
    }

    xil_printf("ETH-7: XAxiEthernet_Start\r\n");
    XAxiEthernet_Start(&EthInst);

    xil_printf("ETH-8: Waiting for LINK...\r\n");
    {
        int i;
        u16 Bmsr = 0;
        for (i = 0; i < 100; i++) {
            /* Read twice: many PHYs have latched status bits. */
            XAxiEthernet_PhyRead(&EthInst, PhyAddr, PHY_REG_BMSR, &Bmsr);
            XAxiEthernet_PhyRead(&EthInst, PhyAddr, PHY_REG_BMSR, &Bmsr);

            if (Bmsr & BMSR_LINK_STATUS) {
                xil_printf("  LINK UP, BMSR = 0x%04x\r\n", Bmsr);
                break;
            }
            usleep(100000);
        }
        if (!(Bmsr & BMSR_LINK_STATUS))
            xil_printf("  [WARN] Link DOWN, last BMSR = 0x%04x\r\n", Bmsr);
    }

    *FoundPhyAddr = PhyAddr;
    xil_printf("=== InitEth() OK ===\r\n");
    return XST_SUCCESS;
}

/* ------------------------------------------------------------------------- */
int main(void)
{
    int Status;
    int PhyAddr;

    /* Disable DCache for clean BRAM / DMA visibility */
    Xil_DCacheDisable();
    Xil_ICacheEnable();

    xil_printf("\r\n========================================\r\n");
    xil_printf("==  PL Ethernet RX -> BRAM TEST       ==\r\n");
    xil_printf("========================================\r\n");

    xil_printf("STEP 1: InitEth()\r\n");
    Status = InitEth(&PhyAddr);
    if (Status != XST_SUCCESS) {
        xil_printf("[FATAL] InitEth FAILED (%d)\r\n", Status);
        return XST_FAILURE;
    }

    xil_printf("MAIN: PHY addr = %d\r\n", PhyAddr);
    xil_printf("MAIN: Send packets from the PC using Scapy and watch the BRAM.\r\n");

    while (1) {
        sleep(1);
        xil_printf("\r\n[INFO] BRAM content snapshot:\r\n");
        DumpBram(32);
    }

    return 0;
}
