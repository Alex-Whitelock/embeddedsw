/******************************************************************************
*
* Copyright (C) 2015 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* XILINX CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
*******************************************************************************/
/*****************************************************************************/
/**
*
* @file xfsbl_authentication.c
*
* Contains the function definitions for RSA Signature verification.
*
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xfsbl_hw.h"

#ifdef XFSBL_RSA

#include "xfsbl_authentication.h"

/*****************************************************************************/

const u8 XFsbl_TPadSha3[] = {0x30, 0x41, 0x30, 0x0D, 0x06, 0x09, 0x60,
	0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
	0x02, 0x05, 0x00, 0x04, 0x30 };

const u8 XFsbl_TPadSha2[] = {0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60,
	0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
	0x01, 0x05, 0x00, 0x04, 0x20 };

u32 CheckSum = 0U;
static XSecure_Rsa SecureRsa;

/*****************************************************************************/
/**
 * Configure the RSA and SHA for the SPK
 * Signature verification.
 * If SPK Signature verification fails
 * then return unique error code saying
 * XFSBL_STAGE3_SPK_SIGN_VERIF_ERROR.
 *
 * @param
 *
 * @return
 *
 ******************************************************************************/
u32 XFsbl_SpkVer(u64 AcOffset, u32 HashLen)
{
	u8 SpkHash[XFSBL_HASH_TYPE_SHA3] __attribute__ ((aligned (4)));
	u8 * PpkModular = (u8 *)NULL;
	u8 * PpkModularEx = (u8 *)NULL;
	u32 PpkExp = 0;
	u8 * AcPtr = (u8*) (PTRSIZE) AcOffset;
	u32 Status = XFSBL_SUCCESS;
	void * ShaCtx = (void * )NULL;
	u8 XFsbl_RsaSha3Array[512];

#ifdef XFSBL_SHA2
	sha2_context ShaCtxObj;
	ShaCtx = &ShaCtxObj;
#endif

	(void)XFsbl_ShaStart(ShaCtx, HashLen);

	/* Hash the PPK + SPK choice */
	XFsbl_ShaUpdate(ShaCtx, AcPtr, 8, HashLen);

	/* Set PPK pointer */
	AcPtr += XFSBL_RSA_AC_ALIGN;
	PpkModular = (u8 *)AcPtr;
	AcPtr += XFSBL_SPK_SIG_SIZE;
	PpkModularEx = (u8 *)AcPtr;
	AcPtr += XFSBL_SPK_SIG_SIZE;
	PpkExp = *((u32 *)AcPtr);
	AcPtr += XFSBL_RSA_AC_ALIGN;

	XFsbl_Printf(DEBUG_DETAILED,
		"XFsbl_SpkVer: Ppk Mod %0x, Ppk Mod Ex %0x, Ppk Exp %0x\r\n",
		PpkModular, PpkModularEx, PpkExp);

	/* Calculate SPK + Auth header(PPK and SPK Selectoin both) Hash */
	XFsbl_ShaUpdate(ShaCtx, (u8 *)AcPtr, XFSBL_PPK_SIZE, HashLen);
	XFsbl_ShaFinish(ShaCtx, (u8 *)SpkHash, HashLen);

	/* Set SPK Signature pointer */
	AcPtr += XFSBL_SPK_SIZE;

	XSecure_RsaInitialize(&SecureRsa, AcPtr, PpkModular,
			              PpkModularEx, (u8 *)&PpkExp);

	/* Decrypt SPK Signature */

	if(XFSBL_SUCCESS !=
		XSecure_RsaDecrypt(&SecureRsa, XFsbl_RsaSha3Array))
	{
		XFsbl_Printf(DEBUG_GENERAL,
			"XFsbl_SpkVer: XFSBL_ERROR_SPK_RSA_DECRYPT\r\n");
		Status = XFSBL_ERROR_SPK_RSA_DECRYPT;
		goto END;
	}

	/* Authenticate SPK Signature */
	if(XFSBL_SUCCESS != XSecure_RsaCheckPadding(XFsbl_RsaSha3Array,
					                            SpkHash, HashLen))
	{
		XFsbl_PrintArray(DEBUG_INFO, SpkHash,
				HashLen, "Calculated Partition Hash");
		XFsbl_PrintArray(DEBUG_INFO, XFsbl_RsaSha3Array,
				512, "RSA decrypted Hash");
		XFsbl_Printf(DEBUG_GENERAL,
			"XFsbl_SpkVer: XFSBL_ERROR_SPK_SIGNATURE\r\n");
		Status = XFSBL_ERROR_SPK_SIGNATURE;
	}

END:
	return Status;
}

/*****************************************************************************/
/**
 * Configure the RSA and SHA for the
 * Boot Header Signature verification.
 * If Boot Header Signature verification
 * fails then return unique error code saying
 * XFSBL_STAGE3_BOOT_HDR_SIGN_VERIF_ERROR.
 *
 * @param
 *
 * @return
 *
 ******************************************************************************/
u32 XFsbl_PartitionSignVer(u64 PartitionOffset, u32 PartitionLen,
							    u64 AcOffset, u32 HashLen)
{

	u8 PartitionHash[XFSBL_HASH_TYPE_SHA3] __attribute__ ((aligned (4)));
	u8 * SpkModular = (u8 *)NULL;
	u8 * SpkModularEx = (u8 *)NULL;
	u32 SpkExp = 0;
	u8 * AcPtr = (u8*)(PTRSIZE) AcOffset;
	u32 Status = XFSBL_SUCCESS;
	u32 HashDataLen=0U;
	u8 XFsbl_RsaSha3Array[512];

	XFsbl_Printf(DEBUG_INFO,"Doing Partition Sign verification\r\n");

	/* Reset CSU DMA. This is a workaround and need to be removed */
	XCsuDma_Reset();

	/**
	 * hash to be calculated will be total length with AC minus
	 * signature size
	 */
	HashDataLen = PartitionLen - XFSBL_FSBL_SIG_SIZE;

	/* Calculate Partition Hash */
	XFsbl_ShaDigest((const u8 *)(PTRSIZE)PartitionOffset,
			HashDataLen, PartitionHash, HashLen);

	/* Set SPK pointer */
	AcPtr += (XFSBL_RSA_AC_ALIGN + XFSBL_PPK_SIZE);
	SpkModular = (u8 *)AcPtr;
	AcPtr += XFSBL_BHDR_SIG_SIZE;
	SpkModularEx = (u8 *)AcPtr;
	AcPtr += XFSBL_BHDR_SIG_SIZE;
	SpkExp = *((u32 *)AcPtr);
	AcPtr += XFSBL_RSA_AC_ALIGN;

	/* Increment by  SPK Signature pointer */
	AcPtr += XFSBL_SPK_SIG_SIZE;
	/* Increment by  BHDR Signature pointer */
	AcPtr += XFSBL_BHDR_SIG_SIZE;

	XFsbl_Printf(DEBUG_DETAILED,
		"XFsbl_PartVer: Spk Mod %0x, Spk Mod Ex %0x, Spk Exp %0x\r\n",
		SpkModular, SpkModularEx, SpkExp);

	XFsbl_Printf(DEBUG_INFO,
			"Partition Verification done \r\n");

	XSecure_RsaInitialize(&SecureRsa, AcPtr, SpkModular,
			              SpkModularEx, (u8 *)&SpkExp);
	/* Decrypt Partition Signature. */
	if(XFSBL_SUCCESS !=
		XSecure_RsaDecrypt(&SecureRsa, XFsbl_RsaSha3Array))
	{
		XFsbl_Printf(DEBUG_GENERAL,
                        "XFsbl_SpkVer: XFSBL_ERROR_PART_RSA_DECRYPT\r\n");
		Status = XFSBL_ERROR_PART_RSA_DECRYPT;
		goto END;
	}

	/* Authenticate Partition Signature */
	if(XFSBL_SUCCESS != XSecure_RsaCheckPadding(XFsbl_RsaSha3Array,
				PartitionHash, HashLen))
	{
		XFsbl_PrintArray(DEBUG_INFO, PartitionHash,
				HashLen, "Calculated Partition Hash");
		XFsbl_PrintArray(DEBUG_INFO, XFsbl_RsaSha3Array,
				512, "RSA decrypted Hash");
		XFsbl_Printf(DEBUG_GENERAL,
                        "XFsbl_PartVer: XFSBL_ERROR_PART_SIGNATURE\r\n");
		Status = XFSBL_ERROR_PART_SIGNATURE;
		goto END;
	}

END:
	return Status;
}

/*****************************************************************************/
/**
 *
 * @param       None
 *
 * @return      None
 *
 ******************************************************************************/
u32 XFsbl_Authentication(XFsblPs * FsblInstancePtr, u64 PartitionOffset,
						 u32 PartitionLen, u64 AcOffset, u32 HashLen)
{
        u32 Status = XFSBL_SUCCESS;


	XFsbl_Printf(DEBUG_INFO,
		"Auth: Partition Offset %0x, PartitionLen %0x,"
		" AcOffset %0x, HashLen %0x\r\n",
		(PTRSIZE )PartitionOffset, PartitionLen,
		(PTRSIZE )AcOffset, HashLen);

        /* Do SPK Signature verification using PPK */
        Status = XFsbl_SpkVer(AcOffset, HashLen);

        if(XFSBL_SUCCESS != Status)
        {
                goto END;
        }

        /* Do Partition Signature verification using SPK */
        Status = XFsbl_PartitionSignVer(PartitionOffset, PartitionLen,
			                        AcOffset, HashLen);

        if(XFSBL_SUCCESS != Status)
        {
                goto END;
        }

END:
        return Status;
}

#endif /* end of XFSBL_RSA */
