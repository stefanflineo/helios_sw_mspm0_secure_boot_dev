/*
 * Copyright (c) 2015-2023, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ti/driverlib/dl_flashctl.h>

#include "aes_cmac.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil/security_cnt.h"
#include "customer_secure_config.h"
#include "lockable_storage_private.h"
#include "mcuboot_config/mcuboot_logging.h"
#include "rollback.h"
#include "secret.h"
#include "ti_msp_dl_config.h"

/* Uncomment this define in order to have the device perform both phases
 * (privileged and unprivileged) without programming nonmain and without
 * requiring a BOOTRST. Useful if non-main not configured for debugging, or if
 * not programming non-main
 */
//#define DEBUG_NO_RESET_PATH

/* sets the range over the lockable storage (0x4000-0x4800). Includes secret */
#define LOCKABLE_FLASH_FIREWALL 0x00030000

//#define CSC_ENABLE_KEYSTORE

#define BOOT_PRIMARY_SLOT 0
#define BOOT_SECONDARY_SLOT 1

/* current debug image settings. Image could obviously be much larger */
#define PRIMARY_SLOT_OFFSET (CSC_PRIMARY_SLOT_OFFSET)
#define SECONDARY_SLOT_OFFSET (CSC_BANK_SIZE + CSC_PRIMARY_SLOT_OFFSET)
#define IMG_SLOT_SIZE (CSC_APPLICATION_IMAGE_SIZE)
/* TEMPORARY DEBUG INSTRUMENTATION (2026-09-08): the CSC's own state can't be
 * observed with a debugger attached (no breakpoints reachable - the device
 * just sits with the I2C lines pulled high, which is ambiguous: that's ALSO
 * what a healthy CSC looks like before the app takes over and starts
 * driving I2C, so "high and idle" alone doesn't say whether the CSC is
 * stuck, looping in mcubootFail(), or genuinely still running normally).
 * Traces control flow through main() by pulsing the I2C0_SDA pin
 * (GPIOA.0, IOMUX_PINCM1 - the only pin externally observable right now) a
 * distinct number of times at each decision point, with a long gap between
 * checkpoints so groups are countable on a logic analyzer/scope. This pin
 * is never used by the CSC itself for anything else, so repurposing it
 * here is safe; remove this whole block once the real branch taken is
 * known.
 */
void debugPulse(uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; i++) {
        DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_0);
        delay_cycles(20000);
        DL_GPIO_clearPins(GPIOA, DL_GPIO_PIN_0);
        delay_cycles(20000);
    }
    delay_cycles(150000); /* gap so the next checkpoint's pulses are distinguishable - shortened 2026-09-08, flash_area_read() checkpoints fire on every chunk read during hashing, not just header reads, so the old delays made a full pass take minutes */
}

static void start_app(uint32_t *vector_table)
{
    /* The following code resets the SP to the value specified in the
     * provided vector table, and then the Reset Handler is invoked.
     *
     * Per ARM Cortex specification:
     *
     *           ARM Cortex VTOR
     *
     *
     *   Offset             Vector
     *
     * 0x00000000  ++++++++++++++++++++++++++
     *             |    Initial SP value    |
     * 0x00000004  ++++++++++++++++++++++++++
     *             |         Reset          |
     * 0x00000008  ++++++++++++++++++++++++++
     *             |          NMI           |
     *             ++++++++++++++++++++++++++
     *             |           .            |
     *             |           .            |
     *             |           .            |
     *
     * */

    /* Reset the SP with the value stored at vector_table[0] */
    __asm volatile(
        "LDR R3,[%[vectab],#0x0] \n"
        "MOV SP, R3       \n" ::[vectab] "r"(vector_table));

    /* Set the Reset Vector to the new vector table (Will be reset to 0x000) */
    SCB->VTOR = (uint32_t) vector_table;

    /* Jump to the Reset Handler address at vector_table[1] */
    debugPulse(6);
    ((void (*)(void))(*(vector_table + 1)))();
}

/* do_boot is called in the event of a successful verification of an image */
static void do_boot(struct boot_rsp *rsp)
{
#ifndef EXCLUDE_GPIOS

    /* Clears the red LED after successfully completing the boot */
    DL_GPIO_clearPins(GPIO_LEDS_PORT, GPIO_LEDS_USER_LED_RED_PIN);

#endif

    MCUBOOT_LOG_INF("Starting Main Application");
    MCUBOOT_LOG_INF("  Image Start Offset: 0x%x", (int) rsp->br_image_off);

    /* BUG FIX (2026-09-08): replaced + rsp->br_image_off with the fixed
     * PRIMARY_SLOT_OFFSET. A controlled real-hardware test (flash
     * bootloader + v1.6.0 alone at 0x4800, confirm, then OTA in a v2.6.0
     * build ALSO linked for plain 0x4800) showed that after
     * DL_SYSCTL_issueINITDONE() locks bank-swap in, address 0x4800 always
     * aliases to wherever the CURRENTLY EXECUTING image is (v2.6.0's bytes
     * were readable AT 0x4800 via a real memory dump post-swap, despite
     * being physically written to 0x14800) - i.e. bank-swap genuinely does
     * remap addresses, just not yet during the bootloader's own
     * pre-INITDONE validation phase (where br_image_off's raw physical
     * value is what correctly locates each slot for reading/hashing).
     * Using the physical br_image_off here - which could be 0x4800 or
     * 0x14800 depending on which slot won validation - ignores that the
     * post-remap address is ALWAYS PRIMARY_SLOT_OFFSET regardless of which
     * bank that turns out to be; using br_image_off when it's 0x14800
     * jumps into the wrong bank's memory - confirmed as the cause of a
     * hard lockup on the actual jump. See FwUpdate_dev.h's matching fix on
     * the application/OTA side. */
    uint32_t vector_table = PRIMARY_SLOT_OFFSET + rsp->br_hdr->ih_hdr_size;

    MCUBOOT_LOG_INF("  Vector Table Start Address: 0x%x", (int) vector_table);

    start_app((uint32_t *) vector_table);
}

/* Fail procedure for mcuboot if no valid image found. Currently just toggles
 * the red LED.
 */
void mcubootFail(void)
{
    while (1) {
        DL_GPIO_togglePins(GPIO_LEDS_PORT, GPIO_LEDS_USER_LED_RED_PIN);
        delay_cycles(10000000);
    }
}
/* Checkpoint numbering (pulse count = which one fired):
 *  1 - main() entered, GPIO debug pin initialized
 *  2 - DL_SYSCTL_isINITDONEIssued() TRUE  -> took the UNPRIVILEGED/fast path
 *  3 - DL_SYSCTL_isINITDONEIssued() FALSE -> took the PRIVILEGED/validation path
 *  4 - (unprivileged path) lock storage says SUCCESS -> about to start_app() at fixed PRIMARY_SLOT_OFFSET
 *  5 - (unprivileged path) lock storage says FAILURE -> about to mcubootFail()
 *  6 - (privileged path) boot_go() validation SUCCEEDED
 *  7 - (privileged path) boot_go() validation FAILED (no valid image in either slot)
 *  8 - (privileged path) about to call DL_SYSCTL_issueINITDONE()
 *  9 - (privileged path) fell through to the shared mcubootFail() at the
 *      bottom of main() AFTER issueINITDONE() returned - should be
 *      UNREACHABLE on a successful validation if issueINITDONE() really
 *      does trigger its own internal reset as assumed; seeing this after a
 *      6 means that assumption is wrong.
 * An extra single short pulse right after a 6 means the upper bank was
 * selected (bootRsp.br_image_off != PRIMARY_SLOT_OFFSET); none means lower.
 * Deeper checkpoints 10-12, 20-24, 30-33, 40-43 live further down this same
 * call chain (loader.c, swap_scratch.c, flash_map_backend.c) - see their
 * own comments. NOTE (2026-09-08): the standalone bank1-only pinpoint test
 * that briefly replaced this function has been reverted - this CSC build
 * is flashed identically to BOTH physical banks, so that version never
 * called start_app() for EITHER slot, meaning bank0 could never boot
 * either - which also meant the app could never come up to service the
 * OTA transfer that writes bank1 in the first place. This real flow (which
 * does boot bank0 normally) is what must stay flashed for actual OTA
 * testing; only swap in a standalone pinpoint test temporarily, on its own,
 * never as a replacement for this. */
int main(void)
{
    fih_int bootStatus;
    /* BUG FIX: bootRsp must be zero-initialized. Left uninitialized,
     * bootRsp.br_hdr is stack garbage until something actually assigns it -
     * several early-exit paths inside boot_go()/context_boot_go() (e.g.
     * failing to read image headers, or finding zero valid images) return
     * without ever touching rsp->br_hdr. The very next line dereferences
     * bootRsp.br_hdr->ih_magic; if that garbage pointer isn't a valid
     * address, that's an immediate HardFault -> Default_Handler ->
     * DL_SYSCTL_resetDevice(0) -> straight back to the top of main() - a
     * silent, fast reset loop that never gets far enough to look like
     * anything but "stuck". */
    struct boot_rsp bootRsp = {0};
    SYSCFG_DL_init();

    /* EXPERIMENT (2026-09-08): reading bank1's header via a normal CPU
     * memcpy() locks up the core hard enough that even Default_Handler's
     * own DL_SYSCTL_resetDevice(0) never runs (confirmed via GPIO trace:
     * completely silent afterward, no reset loop) - despite those same
     * bytes being perfectly readable via SWD. Bank swap policy defaults to
     * enabled per driverlib's docs, but call it explicitly up front, before
     * any header reads, in case that default doesn't actually hold this
     * early after a real BOOTRST on this hardware. */
    DL_SYSCTL_enableFlashBankSwap();

    DL_GPIO_initDigitalOutput(IOMUX_PINCM1);
    DL_GPIO_setPins(GPIOA, DL_GPIO_PIN_0);
    DL_GPIO_enableOutput(GPIOA, DL_GPIO_PIN_0);
    DL_GPIO_clearPins(GPIOA, DL_GPIO_PIN_0);
    debugPulse(1);

#ifndef EXCLUDE_GPIOS
    /* Sets the red LED solid during the validation phase. Will turn off
     * after the validation phase is complete.
     */

#endif /* EXCLUDE_GPIOS */

#ifdef DEBUG_NO_RESET_PATH
    goto noInitdoneLabel;
#endif

    if (DL_SYSCTL_isINITDONEIssued()) { // POSSIBLE IMPORTANT BUG, THIS FLAG SEEMS TO BE SET BY DEFAULT
        debugPulse(2);

        /* Execution flow for the unprivileged state. Authentication should
         * already be accomplished and the verified image CMAC tag should live
         * in the locked storage. All firewalls should be active.
         */

#ifdef DEBUG_NO_RESET_PATH
    initdoneLabel:
        __asm("nop");
#endif
        /* TODO: Check that the swap, if desired, was successfully executed */

        /* Check Write Protect Firewall in place */
        if (gLockStgInFlash.bootStatus == LOCKSTG_BOOT_STATUS_SUCCESS) {
            debugPulse(4);
            if (!((DL_SYSCTL_getWriteProtectFirewallAddrRange() &
                      LOCKABLE_FLASH_FIREWALL) == LOCKABLE_FLASH_FIREWALL)) {
                mcubootFail();
            }
            /* Jump to the application */
            DL_GPIO_clearPins(GPIO_LEDS_PORT, GPIO_LEDS_USER_LED_RED_PIN);
            
            start_app((uint32_t *) (PRIMARY_SLOT_OFFSET + 0x100));
        } else {
            debugPulse(5);
            mcubootFail();
        }

    } else {
        debugPulse(3);
        /* Execution flow for the privileged state. Device has no features but
         * static write protect enabled, and images are unverified, and
         * therefore must be authenticated.
         */
        DL_GPIO_setPins(GPIO_LEDS_PORT, GPIO_LEDS_USER_LED_RED_PIN);
#ifdef DEBUG_NO_RESET_PATH
    noInitdoneLabel:
        __asm("nop");
#endif

        trace_init();
        MCUBOOT_LOG_INF("mcuboot_app");

        debugPulse(10); /* trace_init()/first log line completed */

        boot_return_highest_version(&bootRsp);
        debugPulse(11); /* boot_return_highest_version() returned (its result is unused below unless CMAC accel is on) */

#ifdef CSC_ENABLE_ROLLBACK_PROTECTION
        /* Check that the current version is at least as recent, if not, fail.
         * TODO: this is a preliminary version of rollback protection, more secure monotonic
         * variants will be explored in future releases of the Customer Secure Code, and
         * being integreated into MCUBoot.
         */
        Rollback_status status;
        status = Rollback_compareCount(bootRsp.br_hdr->ih_ver.iv_major);
        if (status != ROLLBACK_OK) {
            mcubootFail();
        }
#endif

#ifdef CSC_ENABLE_CMAC_ACCELERATION

        CMAC_status CMACstatus;

        CMACstatus = CMAC_init(0x0000);
        if (CMACstatus == CMAC_OK) {
            CMACstatus = CMAC_compareTag(
                &bootRsp.br_image_off, &bootRsp.br_hdr->ih_ver);
        }

        if (CMACstatus == CMAC_OK) {
            bootStatus = 0;
        } else {
            bootStatus = boot_go(&bootRsp);
        }
#else
        bootStatus = boot_go(&bootRsp);
#endif
        debugPulse(12); /* boot_go() returned - reaching this rules out a hang inside boot_go() itself */
        debugPulse((bootRsp.br_hdr != NULL) ? 1 : 0); /* extra single pulse if br_hdr got set at all */

        if ((0 == bootStatus) && (IMAGE_MAGIC == bootRsp.br_hdr->ih_magic)) {
            debugPulse(6);
            debugPulse((bootRsp.br_image_off != PRIMARY_SLOT_OFFSET) ? 1 : 0);
            MCUBOOT_LOG_INF("bootRsp: slot = %x, offset = %x, ver=%d.%d.%d.%d",
                bootStatus, bootRsp.br_image_off,
                bootRsp.br_hdr->ih_ver.iv_major,
                bootRsp.br_hdr->ih_ver.iv_minor,
                bootRsp.br_hdr->ih_ver.iv_revision,
                bootRsp.br_hdr->ih_ver.iv_build_num);

#ifdef CSC_ENABLE_ROLLBACK_PROTECTION
            Rollback_storeCount(bootRsp.br_hdr->ih_ver.iv_major);
#endif

#ifdef CSC_ENABLE_CMAC_ACCELERATION
            /* Calculate the CMAC value of the image and store it in lockable storage */
            if (CMACstatus != CMAC_OK) {
                CMACstatus =
                    CMAC_generateTag((uint32_t *) bootRsp.br_image_off,
                        IMG_SLOT_SIZE, &bootRsp.br_hdr->ih_ver);
            }
#endif
            /* Stores new secret and writes Keys into the KeyStore*/
            //Secret_writeOut(); // AVOID LOCKING ANY SECTION

            /* stores keys based on secret information. Must be done after
             * the secret write out function. */
#ifdef CSC_ENABLE_KEYSTORE
           // Keystore_storeKeys(); // AVOID LOCKING ANY SECTION
#endif

            Lock_writeStatus(LOCKSTG_BOOT_STATUS_SUCCESS);
            Lock_writeOut();

            /* Set bank from which we wish to execute */
            if (bootRsp.br_image_off != PRIMARY_SLOT_OFFSET) {
                DL_SYSCTL_executeFromUpperFlashBank();
            } else {
                DL_SYSCTL_executeFromLowerFlashBank();
            }

#ifdef DEBUG_NO_RESET_PATH
            goto initdoneLabel;
#endif

        } else {
            debugPulse(7);
            Lock_writeStatus(LOCKSTG_BOOT_STATUS_FAILURE);
            Lock_writeOut();
        }

        // /* Set Firewalls (to be enabled upon INITDONE) */
        DL_SYSCTL_setWriteProtectFirewallAddrRange( // AVOID LOCKING ANY SECTION
            (uint32_t) LOCKABLE_FLASH_FIREWALL);// AVOID LOCKING ANY SECTION

        // DL_SYSCTL_setReadExecuteProtectFirewallAddrStart(CSC_SECRET_ADDR);// AVOID LOCKING ANY SECTION
        // DL_SYSCTL_setReadExecuteProtectFirewallAddrEnd(CSC_SECRET_END);// AVOID LOCKING ANY SECTION

        // DL_SYSCTL_enableReadExecuteProtectFirewall();// AVOID LOCKING ANY SECTION

        //start_app((uint32_t *) (bootRsp.br_image_off + 0x100));//FORCE APP START HERE DUE TO DL_SYSCTL_isINITDONEIssued POSSIBLE BUG

        debugPulse(8);
        DL_SYSCTL_issueINITDONE();
        debugPulse(9); /* should be unreachable after a successful (6) validation if issueINITDONE() really self-resets */
    }
    mcubootFail();
}
