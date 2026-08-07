/*
 * Copyright (c) 2024, Texas Instruments Incorporated
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

#ifndef __CUSTOMER_SECURE_CONFIG_H__
#define __CUSTOMER_SECURE_CONFIG_H__

/* Top-Level configuration file for the customer secure code and customer secure
 * sample image.
 *
 * This will define functionality that is present in the customer
 * secure code such that a user can pick-and-choose components that will be
 * useful in their application.
 *
 * This file and its configuration must be shared between both projects
 */

/*******************************************************************************
 * VERIFICATION MECHANISMS / SECURITY FEATURES                                 *
 ******************************************************************************/
/* CSC_ENABLE_CMAC_ACCELERATION - used exclusively by the CSC during privileged
 * mode on power up. If an image has been previously verified using ECDSA, the
 * CMAC key and tag can be computed and stored in a read-protected region of
 * flash. During power up, if the image has not been updated (and is the same
 * verison), the tag can be re-computed for the image using the key and ECDSA
 * can be bypassed.
 *
 * This greatly speeds up power-on-resets/bootrsts where updates are not
 * being performed without compromising a thorough, 128-bit secure verification
 * (as key and tag are not exposed).
 *
 * This also is separate from a standard symmetric encryption as the CMAC key
 * is unique per device, thus the leaking of a CMAC key does not represent a
 * breach in security to all devices, or that a user can begin signing arbitrary
 * images that will run in all devices.
 */
//#define CSC_ENABLE_CMAC_ACCELERATION

/* CSC_ENABLE_ROLLBACK_PROTECTION - if the customer uploads a newer version of
 * the application, rollback protection will disallow previous versions from
 * being loaded onto the device.
 */
//#define CSC_ENABLE_ROLLBACK_PROTECTION

/*******************************************************************************
 *   KEY CONFIGURATION                                                         *
 ******************************************************************************/
/* CSC_STATIC_SHARED_SECRET_KEY - this option adds a 256-bit shared secret
 * exclusively used in the privileged mode of execution. This shared secret
 * may be used in order to decrypt incoming keys/messages, or to use symmetric
 * authentication of incoming messages.
 *
 * NOTE: Currently Unsupported
 */
//#define CSC_STATIC_SHARED_SECRET_KEY

/* CSC_STATIC_SHARED_SECRET_KEY_INTERNAL - stores the secret inside the program
 * at compile time, rather than provisioned separately.
 *
 * NOTE: Currently Unsupported
 */
//#define CSC_STATIC_SHARED_SECRET_KEY_INTERNAL

/* ENABLE_KEYSTORE - enables the CSC to write keys into the KEYSTORE - an IP
 * that allows the privileged mode of execution to utilize but not read certain
 * keys used by the AES engine. The KEYSTORE is configured every boot routine
 * (as it is in volatile memory), so keys used are stored persistently in the
 * secret memory.
 *
 * Note: Keys used exclusively during the privileged execution should not be
 *       placed in the keystore.
 * Note: if the application does not use the AES specifically, it is not
 *       necessary to enable this option.
 *
 * Must be enabled if the application is attempting to read from the keystore.
 *
 */
#define CSC_ENABLE_KEYSTORE

/* CSC_ENABLE_KEYSTORE_STATIC_KEY - include an additional secret key at compile
 * time into the secret region of memory that will be added to the keystore for
 * use by the application
 *
 * requires: CSC_ENABLE_KEYSTORE
 *
 */
#define CSC_ENABLE_KEYSTORE_STATIC_KEY

/*******************************************************************************
 *  MISCELLANEOUS                                                           *
 ******************************************************************************/

/* CSC_ENABLE_IP_PROTECTION - the customer will provide an IP Protection range
 * in the header, such that the CSC can enable the IP Protect firewall over a
 * region of memory. The IP Protect firewall will prevent reading of that
 * segment of memory but will still allow the device to execute the code
 *
 * NOTE: Currently Unsupported
 */
//#define CSC_ENABLE_IP_PROTECTION

/* CSC_EXPOSE_SECURITY_PRIMITIVES - the CSC will provide external symbols
 * such that the application can perform hashing and ECDSA by way of a
 * function table in the output.
 *
 * NOTE: Currently unsupported
 */
//#define CSC_EXPOSE_SECURITY_PRIMITIVES

/*******************************************************************************
 *  MEMORY MAPS                                                                *
 ******************************************************************************/
/* CSC_DISABLE_BANKSWAP - if the device supports multiple banks, it is
 * recommended to leave bankswap enabled to allow the CSC to handle images at
 * the bank level (and use write^execute exclusion permissions specific to the
 * bank). Banks also allow for logical address space changes, meaning images can
 * always be compiled with respect to the image offset.
 *
 * If the device is only equipped with one bank, or this functionality is not
 * desired, then it is possible to use eXecute-In-Place (XIP), where two sets
 * of images are used and swapped between. The CSC will write-protect the image
 * in use (at 8kB granularity) and not write-protect the other image. Two sets
 * of images, primary and secondary, must be kept.
 *
 * NOTE: Only Bankswap is enabled currently
 */
//#define CSC_DISABLE_BANKSWAP

/* clang-format off */

#define CSC_BANK_SIZE                                                 (0x10000)

#define CSC_SECRET_ADDR                                                (0x4000)
#define CSC_SECRET_SIZE                                                 (0x400)
#define CSC_SECRET_END                  (CSC_SECRET_ADDR + CSC_SECRET_SIZE - 1)

#define CSC_LOCK_STORAGE_ADDR               (CSC_SECRET_ADDR + CSC_SECRET_SIZE)
#define CSC_LOCK_STORAGE_SIZE                                           (0x400)

#define CSC_APPLICATION_IMAGE_BASE_ADDR                                (0x4800)
#define CSC_APPLICATION_IMAGE_SIZE                                     (0xB800)

#define CSC_PRIMARY_SLOT_OFFSET               (CSC_APPLICATION_IMAGE_BASE_ADDR)

/* clang-format on */

/*******************************************************************************
 * CONFIGURATION CHECKS - DO NOT EDIT                                          *
 ******************************************************************************/
#if (((defined(CSC_ENABLE_KEYSTORE_STATIC_KEY) ||      \
          defined(CSC_ENABLE_KEYSTORE_DYNAMIC_KEY)) && \
         !defined(CSC_ENABLE_KEYSTORE)) ||             \
     (defined(CSC_ENABLE_KEYSTORE) &&                  \
         (!defined(CSC_ENABLE_KEYSTORE_DYNAMIC_KEY) && \
             !defined(CSC_ENABLE_KEYSTORE_STATIC_KEY))))
#error "Keystore must support a static or dynamic key, and be enabled"
#endif

#if (defined(CSC_SYMMETRIC_ENCRYPTION_ONLY_REMOVE_ECDSA_SUPPORT) && \
     !defined(CSC_SYMMETRIC_ENCRYPTION_ONLY))
#error \
    "ECDSA Support can only be removed if Symmetric Encryption only is enabled"
#endif

#endif  //__CUSTOMER_SECURE_CONFIG_H__
