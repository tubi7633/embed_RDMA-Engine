#include "xparameters.h"
#include "xil_printf.h"
#include "xil_cache.h"
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

static XAxiEthernet EthInst;

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
    if (Status != XST_SUCCESS) {
        xil_printf("  [WARN] SetOperatingSpeed FAILED (%d)\r\n", Status);
    }

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

    /* --- PHY init --- */
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
            /* Read twice: many PHYs latch/link-status bits and require
               two reads to get the current value (first read may clear
               latched bits). */
            XAxiEthernet_PhyRead(&EthInst, PhyAddr, PHY_REG_BMSR, &Bmsr);
            XAxiEthernet_PhyRead(&EthInst, PhyAddr, PHY_REG_BMSR, &Bmsr);

            if (Bmsr & BMSR_LINK_STATUS) {
                xil_printf("  LINK UP, BMSR = 0x%04x\r\n", Bmsr);
                break;
            }
            usleep(100000); // 100 ms
        }
        if (!(Bmsr & BMSR_LINK_STATUS)) {
            xil_printf("  [WARN] Link DOWN, last BMSR = 0x%04x\r\n", Bmsr);
        }
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

    /* For a clean test, disable DCache (prevents confusing DMA/BRAM reads).
       Instruction cache can stay enabled. */
    Xil_DCacheDisable();
    Xil_ICacheEnable();

    xil_printf("\r\n========================================\r\n");
    xil_printf("==  PL Ethernet + PatternGen TEST     ==\r\n");
    xil_printf("========================================\r\n");

    xil_printf("STEP 1: InitEth()\r\n");
    Status = InitEth(&PhyAddr);
    if (Status != XST_SUCCESS) {
        xil_printf("[FATAL] InitEth FAILED (%d)\r\n", Status);
        return XST_FAILURE;
    }

    xil_printf("MAIN: PHY addr = %d\r\n", PhyAddr);
    xil_printf("MAIN: Pattern generator is active in hardware.\r\n");
    xil_printf("      You can look for broadcast frames in Wireshark.\r\n");

    while (1) {
        sleep(1);
    }

    return 0;
}
