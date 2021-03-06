/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#define _USB_HALINIT_C_

#include <rtl8723b_hal.h>
#ifdef CONFIG_WOWLAN
	#include "hal_com_h2c.h"
#endif

static void
_ConfigChipOutEP_8723(
		PADAPTER	pAdapter,
		u8		NumOutPipe
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);


	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber	= 0;

	switch (NumOutPipe) {
	case	4:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 4;
		break;
	case	3:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 3;
		break;
	case	2:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 2;
		break;
	case	1:
		pHalData->OutEpQueueSel = TX_SELE_HQ;
		pHalData->OutEpNumber = 1;
		break;
	default:
		break;

	}
	RTW_INFO("%s OutEpQueueSel(0x%02x), OutEpNumber(%d)\n", __FUNCTION__, pHalData->OutEpQueueSel, pHalData->OutEpNumber);

}

static BOOLEAN HalUsbSetQueuePipeMapping8723BUsb(
		PADAPTER	pAdapter,
		u8		NumInPipe,
		u8		NumOutPipe
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	BOOLEAN			result		= _FALSE;

	_ConfigChipOutEP_8723(pAdapter, NumOutPipe);

	/* Normal chip with one IN and one OUT doesn't have interrupt IN EP. */
	if (1 == pHalData->OutEpNumber) {
		if (1 != NumInPipe)
			return result;
	}

	/* All config other than above support one Bulk IN and one Interrupt IN. */
	/* if(2 != NumInPipe){ */
	/*	return result; */
	/* } */

	result = Hal_MappingOutPipe(pAdapter, NumOutPipe);

	return result;

}

void rtl8723bu_interface_configure(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	if (IS_HIGH_SPEED_USB(padapter)) {
		pHalData->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;/* 512 bytes */
	} else {
		pHalData->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;/* 64 bytes */
	}

#ifdef CONFIG_USB_TX_AGGREGATION
	pHalData->UsbTxAggMode		= 1;
	pHalData->UsbTxAggDescNum	= 0x6;	/* only 4 bits */
#endif

#ifdef CONFIG_USB_RX_AGGREGATION
	pHalData->rxagg_mode		= RX_AGG_USB;
	pHalData->rxagg_usb_size	= 0x5; /* unit: 4KB, for USB mode */
	pHalData->rxagg_usb_timeout	= 0x20; /* unit: 32us, for USB mode */
	pHalData->rxagg_dma_size	= 0xF; /* uint: 1KB, for DMA mode */
	pHalData->rxagg_dma_timeout	= 0x20; /* unit: 32us, for DMA mode */
#endif

	HalUsbSetQueuePipeMapping8723BUsb(padapter,
			  pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

}

static u32 _InitPowerOn_8723BU(PADAPTER padapter)
{
	u8		status = _SUCCESS;
	u16			value16 = 0;
	u8			value8 = 0;
	u32 value32;

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &value8);
	if (value8 == _TRUE)
		return _SUCCESS;

	/* HW Power on sequence */
	if (!HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8723B_card_enable_flow))
		return _FAIL;

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	/* Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */
	rtw_write8(padapter, REG_CR_8723B, 0x00);  /* suggseted by zhouzhou, by page, 20111230 */
	value16 = rtw_read16(padapter, REG_CR_8723B);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
		    | PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	rtw_write16(padapter, REG_CR_8723B, value16);

	value8 = _TRUE;
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &value8);

	return status;
}


/* ---------------------------------------------------------------
 *
 *	MAC init functions
 *
 * --------------------------------------------------------------- */

/*
 * USB has no hardware interrupt,
 * so no need to initialize HIMR.
 */
static void _InitInterrupt(PADAPTER padapter)
{
#ifdef CONFIG_SUPPORT_USB_INT
	/* clear interrupt, write 1 clear */
	rtw_write32(padapter, REG_HISR0_8723B, 0xFFFFFFFF);
	rtw_write32(padapter, REG_HISR1_8723B, 0xFFFFFFFF);
#endif /* CONFIG_SUPPORT_USB_INT */
}

static void _InitQueueReservedPage(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ;
	u32			value32;
	u8			value8;
	BOOLEAN		bWiFiConfig	= pregistrypriv->wifi_spec;

	if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		numHQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_HPQ_8723B : NORMAL_PAGE_NUM_HPQ_8723B;

	if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		numLQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_LPQ_8723B : NORMAL_PAGE_NUM_LPQ_8723B;

	/* NOTE: This step shall be proceed before writting REG_RQPN. */
	if (pHalData->OutEpQueueSel & TX_SELE_NQ)
		numNQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_NPQ_8723B : NORMAL_PAGE_NUM_NPQ_8723B;
	value8 = (u8)_NPQ(numNQ);
	rtw_write8(padapter, REG_RQPN_NPQ, value8);

	numPubQ = TX_TOTAL_PAGE_NUMBER_8723B - numHQ - numLQ - numNQ;

	/* TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(padapter, REG_RQPN, value32);

}

static void _InitTxBufferBoundary(PADAPTER padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_CONCURRENT_MODE
	u8 val8;
#endif /* CONFIG_CONCURRENT_MODE */

	/* u16	txdmactrl; */
	u8	txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec)
		txpktbuf_bndy = TX_PAGE_BOUNDARY_8723B;
	else {
		/* for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_8723B;
	}

	rtw_write8(padapter, REG_TXPKTBUF_BCNQ_BDNY_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_MGQ_BDNY_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);

#ifdef CONFIG_CONCURRENT_MODE
	val8 = txpktbuf_bndy + 8;
	rtw_write8(padapter, REG_BCNQ1_BDNY, val8);
	rtw_write8(padapter, REG_DWBCN1_CTRL_8723B + 1, val8); /* BCN1_HEAD */

	val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723B + 2);
	val8 |= BIT(1); /* BIT1- BIT_SW_BCN_SEL_EN */
	rtw_write8(padapter, REG_DWBCN1_CTRL_8723B + 2, val8);
#endif /* CONFIG_CONCURRENT_MODE */
}


void
_InitTransferPageSize_8723bu(
	PADAPTER Adapter
)
{

	u8	value8;
	value8 = _PSRX(PBP_256) | _PSTX(PBP_256);

	rtw_write8(Adapter, REG_PBP_8723B, value8);
}


static void
_InitNormalChipRegPriority(
		PADAPTER	Adapter,
		u16		beQ,
		u16		bkQ,
		u16		viQ,
		u16		voQ,
		u16		mgtQ,
		u16		hiQ
)
{
	u16 value16		= (rtw_read16(Adapter, REG_TRXDMA_CTRL_8723B) & 0x7);

	value16 |=	_TXDMA_BEQ_MAP(beQ)	| _TXDMA_BKQ_MAP(bkQ) |
			_TXDMA_VIQ_MAP(viQ)	| _TXDMA_VOQ_MAP(voQ) |
			_TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL_8723B, value16);
}


static void
_InitNormalChipTwoOutEpPriority(
		PADAPTER Adapter
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16			beQ, bkQ, viQ, voQ, mgtQ, hiQ;


	u16	valueHi = 0;
	u16	valueLow = 0;

	switch (pHalData->OutEpQueueSel) {
	case (TX_SELE_HQ | TX_SELE_LQ):
		valueHi = QUEUE_HIGH;
		valueLow = QUEUE_LOW;
		break;
	case (TX_SELE_NQ | TX_SELE_LQ):
		valueHi = QUEUE_NORMAL;
		valueLow = QUEUE_LOW;
		break;
	case (TX_SELE_HQ | TX_SELE_NQ):
		valueHi = QUEUE_HIGH;
		valueLow = QUEUE_NORMAL;
		break;
	default:
		/* RT_ASSERT(FALSE,("Shall not reach here!\n")); */
		break;
	}

	if (!pregistrypriv->wifi_spec) {
		beQ		= valueLow;
		bkQ		= valueLow;
		viQ		= valueHi;
		voQ		= valueHi;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	} else { /* for WMM ,CONFIG_OUT_EP_WIFI_MODE */
		beQ		= valueLow;
		bkQ		= valueHi;
		viQ		= valueHi;
		voQ		= valueLow;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}

	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);

}

static void
_InitNormalChipThreeOutEpPriority(
		PADAPTER padapter
)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	u16			beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec) { /* typical setting */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_LOW;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	} else { /* for WMM */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_NORMAL;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(padapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitQueuePriority(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	switch (pHalData->OutEpNumber) {
	case 2:
		_InitNormalChipTwoOutEpPriority(padapter);
		break;
	case 3:
	case 4:
		_InitNormalChipThreeOutEpPriority(padapter);
		break;
	default:
		/* RT_ASSERT(FALSE,("Shall not reach here!\n")); */
		break;
	}

}


static void _InitPageBoundary(PADAPTER padapter)
{
	/* RX FIFO(RXFF0) Boundary, unit is byte */
	rtw_write16(padapter, REG_TRXFF_BNDY + 2, RX_DMA_BOUNDARY_8723B);
}

static void
_InitHardwareDropIncorrectBulkOut(
		PADAPTER Adapter
)
{
	u32	value32 = rtw_read32(Adapter, REG_TXDMA_OFFSET_CHK);
	value32 |= DROP_DATA_EN;
	rtw_write32(Adapter, REG_TXDMA_OFFSET_CHK, value32);
}

static void
_InitNetworkType(
		PADAPTER Adapter
)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_CR);

	/* TODO: use the other function to set network type */
#if 0/* RTL8191C_FPGA_NETWORKTYPE_ADHOC */
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC);
#else
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);
#endif
	rtw_write32(Adapter, REG_CR, value32);
	/*	RASSERT(pIoBase->rtw_read8(REG_CR + 2) == 0x2); */
}


static void
_InitDriverInfoSize(
		PADAPTER	Adapter,
		u8		drvInfoSize
)
{
	rtw_write8(Adapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

static void
_InitWMACSetting(
		PADAPTER Adapter
)
{
	/* u32			value32; */
	u16			value16;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32 rcr;

	/*	rcr = AAP | APM | AM |AB  |ADD3|APWRMGT| APP_ICV | APP_MIC |APP_FCS|ADF |ACF|AMF|HTC_LOC_CTRL|APP_PHYSTS; */
	rcr = 0
		| APM | AM | AB
		| CBSSID_DATA | CBSSID_BCN | AMF
		| HTC_LOC_CTRL
		| APP_PHYSTS | APP_ICV | APP_MIC
		;
	rtw_hal_set_hwreg(Adapter, HW_VAR_RCR, (u8 *)&rcr);

	/* Accept all data frames */
	value16 = 0xFFFF;
	rtw_write16(Adapter, REG_RXFLTMAP2_8723B, value16);

	/* 2010.09.08 hpfan */
	/* Since ADF is removed from RCR, ps-poll will not be indicate to driver, */
	/* RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll. */

	value16 = 0x400;
	rtw_write16(Adapter, REG_RXFLTMAP1_8723B, value16);

	/* Accept all management frames */
	value16 = 0xFFFF;
	rtw_write16(Adapter, REG_RXFLTMAP0_8723B, value16);

}

static void
_InitAdaptiveCtrl(
		PADAPTER Adapter
)
{
	u16	value16;
	u32	value32;

	/* Response Rate Set */
	value32 = rtw_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;

	rtw_phydm_set_rrsr(Adapter, value32, TRUE);


	/* CF-END Threshold */
	/* m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1); */

	/* SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(Adapter, REG_SPEC_SIFS, value16);

	/* Retry Limit */
	value16 = BIT_LRL(RL_VAL_STA) | BIT_SRL(RL_VAL_STA);
	rtw_write16(Adapter, REG_RETRY_LIMIT, value16);

}

static void
_InitEDCA(
		PADAPTER Adapter
)
{
	/* Set Spec SIFS (used in NAV) */
	rtw_write16(Adapter, REG_SPEC_SIFS, 0x100a);
	rtw_write16(Adapter, REG_MAC_SPEC_SIFS, 0x100a);

	/* Set SIFS for CCK */
	rtw_write16(Adapter, REG_SIFS_CTX, 0x100a);

	/* Set SIFS for OFDM */
	rtw_write16(Adapter, REG_SIFS_TRX, 0x100a);

	/* TXOP */
	rtw_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

#ifdef CONFIG_RTW_LED
static void _InitHWLed(PADAPTER Adapter)
{
	struct led_priv *pledpriv = adapter_to_led(Adapter);

	if (pledpriv->LedStrategy != HW_LED)
		return;

	/* HW led control
	 * to do ....
	 * must consider cases of antenna diversity/ commbo card/solo card/mini card */

}
#endif /* CONFIG_RTW_LED */

static void
_InitRDGSetting_8723bu(
		PADAPTER Adapter
)
{
	rtw_write8(Adapter, REG_RD_CTRL_8723B, 0xFF);
	rtw_write16(Adapter, REG_RD_NAV_NXT_8723B, 0x200);
	rtw_write8(Adapter, REG_RD_RESP_PKT_TH_8723B, 0x05);
}

static void
_InitRetryFunction(
		PADAPTER Adapter
)
{
	u8	value8;

	value8 = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL, value8);

	/* Set ACK timeout */
	rtw_write8(Adapter, REG_ACKTO, 0x40);
}

static void _InitBurstPktLen(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 tmp8;


	tmp8 = rtw_read8(padapter, REG_RXDMA_PRO_8723B);
	tmp8 &= ~(BIT(4) | BIT(5));
	switch (pHalData->UsbBulkOutSize) {
	case USB_HIGH_SPEED_BULK_SIZE:
		tmp8 |= BIT(4); /* set burst pkt len=512B */
		break;
	case USB_FULL_SPEED_BULK_SIZE:
	default:
		tmp8 |= BIT(5); /* set burst pkt len=64B */
		break;
	}
	tmp8 |= BIT(1) | BIT(2) | BIT(3);
	rtw_write8(padapter, REG_RXDMA_PRO_8723B, tmp8);

	pHalData->bSupportUSB3 = _FALSE;

	tmp8 = rtw_read8(padapter, REG_HT_SINGLE_AMPDU_8723B);
	tmp8 |= BIT(7); /* enable single pkt ampdu */
	rtw_write8(padapter, REG_HT_SINGLE_AMPDU_8723B, tmp8);
	rtw_write16(padapter, REG_MAX_AGGR_NUM, 0x0C14);
	rtw_write8(padapter, REG_AMPDU_MAX_TIME_8723B, 0x5E);
	rtw_write32(padapter, REG_AMPDU_MAX_LENGTH_8723B, 0xffffffff);
	if (pHalData->AMPDUBurstMode)
		rtw_write8(padapter, REG_AMPDU_BURST_MODE_8723B, 0x5F);

	/* for VHT packet length 11K */
	rtw_write8(padapter, REG_RX_PKT_LIMIT, 0x18);

	rtw_write8(padapter, REG_PIFS, 0x00);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL, 0x80);
	rtw_write32(padapter, REG_FAST_EDCA_CTRL, 0x03086666);
	rtw_write8(padapter, REG_USTIME_TSF_8723B, 0x50);
	rtw_write8(padapter, REG_USTIME_EDCA_8723B, 0x50);

	/* to prevent mac is reseted by bus. 20111208, by Page */
	tmp8 = rtw_read8(padapter, REG_RSV_CTRL);
	tmp8 |= BIT(5) | BIT(6);
	rtw_write8(padapter, REG_RSV_CTRL, tmp8);
}

/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingTxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Seperate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static void
usb_AggSettingTxUpdate(
		PADAPTER			Adapter
)
{
#ifdef CONFIG_USB_TX_AGGREGATION
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	/* PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo); */
	u32			value32;

	if (Adapter->registrypriv.wifi_spec)
		pHalData->UsbTxAggMode = _FALSE;

	if (pHalData->UsbTxAggMode) {
		value32 = rtw_read32(Adapter, REG_DWBCN0_CTRL_8723B);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((pHalData->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);

		rtw_write32(Adapter, REG_DWBCN0_CTRL_8723B, value32);
		rtw_write8(Adapter, REG_DWBCN1_CTRL_8723B, pHalData->UsbTxAggDescNum << 1);
	}

#endif
}	/* usb_AggSettingTxUpdate */


/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingRxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 *---------------------------------------------------------------------------*/
static void usb_AggSettingRxUpdate(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 aggctrl;
	u32 aggrx;
	u32 agg_size;


	pHalData = GET_HAL_DATA(padapter);

	aggctrl = rtw_read8(padapter, REG_TRXDMA_CTRL);
	aggctrl &= ~RXDMA_AGG_EN;

	aggrx = rtw_read32(padapter, REG_RXDMA_AGG_PG_TH);
	aggrx &= ~BIT_USB_RXDMA_AGG_EN;
	aggrx &= ~0xFF0F; /* reset agg size and timeout */

#ifdef CONFIG_USB_RX_AGGREGATION
	switch (pHalData->rxagg_mode) {
	case RX_AGG_DMA:
		agg_size = pHalData->rxagg_dma_size << 10;
		if (agg_size > RX_DMA_BOUNDARY_8723B)
			agg_size = RX_DMA_BOUNDARY_8723B >> 1;
		if ((agg_size + 2048) > MAX_RECVBUF_SZ)
			agg_size = MAX_RECVBUF_SZ - 2048;
		agg_size >>= 10; /* unit: 1K */
		if (agg_size > 0xF)
			agg_size = 0xF;

		aggctrl |= RXDMA_AGG_EN;
		aggrx |= BIT_USB_RXDMA_AGG_EN;
		aggrx |= agg_size;
		aggrx |= (pHalData->rxagg_dma_timeout << 8);
		RTW_INFO("%s: RX Agg-DMA mode, size=%dKB, timeout=%dus\n",
			 __func__, agg_size, pHalData->rxagg_dma_timeout * 32);
		break;

	case RX_AGG_USB:
	case RX_AGG_MIX:
		agg_size = pHalData->rxagg_usb_size << 12;
		if ((agg_size + 2048) > MAX_RECVBUF_SZ)
			agg_size = MAX_RECVBUF_SZ - 2048;
		agg_size >>= 12; /* unit: 4K */
		if (agg_size > 0xF)
			agg_size = 0xF;

		aggctrl |= RXDMA_AGG_EN;
		aggrx &= ~BIT_USB_RXDMA_AGG_EN;
		aggrx |= agg_size;
		aggrx |= (pHalData->rxagg_usb_timeout << 8);
		RTW_INFO("%s: RX Agg-USB mode, size=%dKB, timeout=%dus\n",
			__func__, agg_size * 4, pHalData->rxagg_usb_timeout * 32);
		break;

	case RX_AGG_DISABLE:
	default:
		RTW_INFO("%s: RX Aggregation Disable!\n", __FUNCTION__);
		break;
	}
#endif /* CONFIG_USB_RX_AGGREGATION */

	rtw_write8(padapter, REG_TRXDMA_CTRL, aggctrl);
	rtw_write32(padapter, REG_RXDMA_AGG_PG_TH, aggrx);
}

static void
_initUsbAggregationSetting(
		PADAPTER Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	/* Tx aggregation setting */
	usb_AggSettingTxUpdate(Adapter);

	/* Rx aggregation setting */
	usb_AggSettingRxUpdate(Adapter);

	/* 201/12/10 MH Add for USB agg mode dynamic switch. */
	pHalData->UsbRxHighSpeedMode = _FALSE;
}

static void
PHY_InitAntennaSelection8723B(
	PADAPTER Adapter
)
{
	/* TODO: <20130114, Kordan> The following setting is only for DPDT and Fixed board type. */
	/* TODO:  A better solution is configure it according EFUSE during the run-time. */
	phy_set_mac_reg(Adapter, 0x64, BIT20, 0x0);		   /* 0x66[4]=0		 */
	phy_set_mac_reg(Adapter, 0x64, BIT24, 0x0);		   /* 0x66[8]=0 */
	phy_set_mac_reg(Adapter, 0x40, BIT4, 0x0);		   /* 0x40[4]=0		 */
	phy_set_mac_reg(Adapter, 0x40, BIT3, 0x1);		   /* 0x40[3]=1		 */
	phy_set_mac_reg(Adapter, 0x4C, BIT24, 0x1);      /* 0x4C[24:23]=10 */
	phy_set_mac_reg(Adapter, 0x4C, BIT23, 0x0);     /* 0x4C[24:23]=10	 */
	phy_set_bb_reg(Adapter, 0x944, BIT1 | BIT0, 0x3);   /* 0x944[1:0]=11	 */
	phy_set_bb_reg(Adapter, 0x930, bMaskByte0, 0x77);   /* 0x930[7:0]=77	  */
	phy_set_mac_reg(Adapter, 0x38, BIT11, 0x1);        /* 0x38[11]=1	 */
}


/* Set CCK and OFDM Block "ON" */
static void _BBTurnOnBlock(
		PADAPTER		Adapter
)
{
#if (DISABLE_BB_RF)
	return;
#endif

	phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

/*
 * 2010/08/09 MH Add for power down check.
 *   */
 #if 0
static BOOLEAN
HalDetectPwrDownMode(
		PADAPTER				Adapter
)
{
	u8	tmpvalue;
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(Adapter);

	EFUSE_ShadowRead(Adapter, 1, EEPROM_FEATURE_OPTION_8723B, (u32 *)&tmpvalue);

	/* 2010/08/25 MH INF priority > PDN Efuse value. */
	if (tmpvalue & BIT4 && pwrctrlpriv->reg_pdnmode)
		pHalData->pwrdown = _TRUE;
	else
		pHalData->pwrdown = _FALSE;

	RTW_INFO("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);
	return pHalData->pwrdown;

}	/* HalDetectPwrDownMode */
#endif

/*
 * 2010/08/26 MH Add for selective suspend mode check.
 * If Efuse 0x0e bit1 is not enabled, we can not support selective suspend for Minicard and
 * slim card.
 *   */
#if 0   /* amyma */
static void
HalDetectSelectiveSuspendMode(
		PADAPTER				Adapter
)
{
	u8	tmpvalue;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);

	/* If support HW radio detect, we need to enable WOL ability, otherwise, we */
	/* can not use FW to notify host the power state switch. */

	EFUSE_ShadowRead(Adapter, 1, EEPROM_USB_OPTIONAL1, (u32 *)&tmpvalue);

	RTW_INFO("HalDetectSelectiveSuspendMode(): SS ");
	if (tmpvalue & BIT1)
		RTW_INFO("Enable\n");
	else {
		RTW_INFO("Disable\n");
		pdvobjpriv->RegUsbSS = _FALSE;
	}

	/* 2010/09/01 MH According to Dongle Selective Suspend INF. We can switch SS mode. */
	if (pdvobjpriv->RegUsbSS && !SUPPORT_HW_RADIO_DETECT(pHalData)) {
		/* PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo); */

		/* if (!pMgntInfo->bRegDongleSS)	 */
		/* { */
		pdvobjpriv->RegUsbSS = _FALSE;
		/* } */
	}
}	/* HalDetectSelectiveSuspendMode*/
#endif

rt_rf_power_state RfOnOffDetect(PADAPTER pAdapter)
{
	/* HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter); */
	u8	val8;
	rt_rf_power_state rfpowerstate = rf_off;

	if (adapter_to_pwrctl(pAdapter)->bHWPowerdown) {
		val8 = rtw_read8(pAdapter, REG_HSISR);
		RTW_INFO("pwrdown, 0x5c(BIT7)=%02x\n", val8);
		rfpowerstate = (val8 & BIT7) ? rf_off : rf_on;
	} else { /* rf on/off */
		rtw_write8(pAdapter, REG_MAC_PINMUX_CFG, rtw_read8(pAdapter, REG_MAC_PINMUX_CFG) & ~(BIT3));
		val8 = rtw_read8(pAdapter, REG_GPIO_IO_SEL);
		RTW_INFO("GPIO_IN=%02x\n", val8);
		rfpowerstate = (val8 & BIT3) ? rf_on : rf_off;
	}
	return rfpowerstate;
}	/* HalDetectPwrDownMode */

void _ps_open_RF(_adapter *padapter);

u32 rtl8723bu_hal_init(PADAPTER padapter)
{
	u8	value8 = 0, u1bRegCR;
	u32	boundary, status = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	rt_rf_power_state		eRfPowerStateToSet;
	u32 NavUpper = WiFiNavUpperUs;
	u32 value32;
	systime init_start_time = rtw_get_current_time();


#ifdef DBG_HAL_INIT_PROFILING

	enum HAL_INIT_STAGES {
		HAL_INIT_STAGES_BEGIN = 0,
		HAL_INIT_STAGES_INIT_PW_ON,
		HAL_INIT_STAGES_INIT_LLTT,
		HAL_INIT_STAGES_MISC01,
		HAL_INIT_STAGES_DOWNLOAD_FW,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,
		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		/* HAL_INIT_STAGES_RF_PS, */
		HAL_INIT_STAGES_INIT_HAL_DM,
		/*		HAL_INIT_STAGES_IQK,
		 *		HAL_INIT_STAGES_PW_TRACK,
		 *		HAL_INIT_STAGES_LCK, */
		HAL_INIT_STAGES_MISC21,
		/* HAL_INIT_STAGES_INIT_PABIAS, */
		HAL_INIT_STAGES_BT_COEXIST,
		/* HAL_INIT_STAGES_ANTENNA_SEL, */
		HAL_INIT_STAGES_MISC31,
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	char *hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_DOWNLOAD_FW",
		"HAL_INIT_STAGES_MAC",
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_MISC02",
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		/* "HAL_INIT_STAGES_RF_PS", */
		"HAL_INIT_STAGES_INIT_HAL_DM",
		/* 		"HAL_INIT_STAGES_IQK",
		 * 		"HAL_INIT_STAGES_PW_TRACK",
		 * 		"HAL_INIT_STAGES_LCK", */
		"HAL_INIT_STAGES_MISC21",
		/* "HAL_INIT_STAGES_INIT_PABIAS", */
		"HAL_INIT_STAGES_BT_COEXIST",
		/* "HAL_INIT_STAGES_ANTENNA_SEL", */
		"HAL_INIT_STAGES_MISC31",
		"HAL_INIT_STAGES_END",
	};

	int hal_init_profiling_i;
	systime hal_init_stages_timestamp[HAL_INIT_STAGES_NUM]; /* used to record the time of each stage's starting point */

	for (hal_init_profiling_i = 0; hal_init_profiling_i < HAL_INIT_STAGES_NUM; hal_init_profiling_i++)
		hal_init_stages_timestamp[hal_init_profiling_i] = 0;

#define HAL_INIT_PROFILE_TAG(stage) do { hal_init_stages_timestamp[(stage)] = rtw_get_current_time(); } while (0)
#else
#define HAL_INIT_PROFILE_TAG(stage) do {} while (0)
#endif /* DBG_HAL_INIT_PROFILING */




	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BEGIN);

	/*	if(rtw_is_surprise_removed(padapter))
			return _FAIL;*/

	/* Check if MAC has already power on.	 */
	value8 = rtw_read8(padapter, REG_SYS_CLKR_8723B + 1);
	u1bRegCR = rtw_read8(padapter, REG_CR_8723B);
	RTW_INFO(" power-on :REG_SYS_CLKR 0x09=0x%02x. REG_CR 0x100=0x%02x.\n", value8, u1bRegCR);
	if ((value8 & BIT3) && (u1bRegCR != 0 && u1bRegCR != 0xEA))
		RTW_INFO(" MAC has already power on.\n");
	else {
		/* Set FwPSState to ALL_ON mode to prevent from the I/O be return because of 32k */
		/* state which is set before sleep under wowlan mode. 2012.01.04. by tynli. */
		/* pHalData->FwPSState = FW_PS_STATE_ALL_ON_88E; */
		RTW_INFO(" MAC has not been powered on yet.\n");
	}


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	status = rtw_hal_power_on(padapter);
	if (status == _FAIL) {
		goto exit;
	}


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	if (!pregistrypriv->wifi_spec)
		boundary = TX_PAGE_BOUNDARY_8723B;
	else {
		/* for WMM */
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY_8723B;
	}
	status =  rtl8723b_InitLLTTable(padapter);
	if (status == _FAIL) {
		goto exit;
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	if (pHalData->bRDGEnable)
		_InitRDGSetting_8723bu(padapter);


	/* Enable TX Report */
	/* Enable Tx Report Timer   */
	value8 = rtw_read8(padapter, REG_TX_RPT_CTRL);
	rtw_write8(padapter, REG_TX_RPT_CTRL, value8 | BIT1);
	/* Set MAX RPT MACID */
	rtw_write8(padapter, REG_TX_RPT_CTRL + 1, 2);
	/* Tx RPT Timer. Unit: 32us */
	rtw_write16(padapter, REG_TX_RPT_TIME, 0xCdf0);

#ifdef CONFIG_TX_EARLY_MODE
	if (pHalData->AMPDUBurstMode) {

		value8 = rtw_read8(padapter, REG_EARLY_MODE_CONTROL_8723B);
#if RTL8723B_EARLY_MODE_PKT_NUM_10 == 1
		value8 = value8 | 0x1f;
#else
		value8 = value8 | 0xf;
#endif
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL_8723B, value8);

		rtw_write8(padapter, REG_EARLY_MODE_CONTROL_8723B + 3, 0x80);

		value8 = rtw_read8(padapter, REG_TCR_8723B + 1);
		value8 = value8 | 0x40;
		rtw_write8(padapter, REG_TCR_8723B + 1, value8);
	} else
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL_8723B, 0);
#endif

	/* <Kordan> InitHalDm should be put ahead of FirmwareDownload. (HWConfig flow: FW->MAC->-BB->RF) */
	/* InitHalDm(Adapter); */

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
	/* if (padapter->registrypriv.mp_mode == 0) */
	{
		status = rtl8723b_FirmwareDownload(padapter, _FALSE);
		if (status != _SUCCESS) {
			pHalData->bFWReady = _FALSE;
			pHalData->fw_ractrl = _FALSE;
			RTW_INFO("fw download fail!\n");
			goto exit;
		} else {
			pHalData->bFWReady = _TRUE;
			pHalData->fw_ractrl = _TRUE;
			RTW_INFO("fw download ok!\n");
		}
	}

	if (pwrctrlpriv->reg_rfoff == _TRUE)
		pwrctrlpriv->rf_pwrstate = rf_off;

	/* Set RF type for BB/RF configuration */
	/* _InitRFType(Adapter); */

	/* We should call the function before MAC/BB configuration. */
	PHY_InitAntennaSelection8723B(padapter);



	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8723B(padapter);
	if (status == _FAIL) {
		RTW_INFO("PHY_MACConfig8723B fault !!\n");
		goto exit;
	}
#endif


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8723B(padapter);
	if (status == _FAIL) {
		RTW_INFO("PHY_BBConfig8723B fault !!\n");
		goto exit;
	}
#endif




	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8723B(padapter);

	if (status == _FAIL) {
		RTW_INFO("PHY_RFConfig8723B fault !!\n");
		goto exit;
	}



#endif



	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);
	_InitQueuePriority(padapter);
	_InitPageBoundary(padapter);
	_InitTransferPageSize_8723bu(padapter);


	/* Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(padapter, DRVINFO_SZ);

	_InitInterrupt(padapter);
	_InitNetworkType(padapter);/* set msr */
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRetryFunction(padapter);
	/* _InitOperationMode(Adapter); */ /* todo */
	rtl8723b_InitBeaconParameters(padapter);
	rtl8723b_InitBeaconMaxError(padapter, _TRUE);

	_InitBurstPktLen(padapter);
	_initUsbAggregationSetting(padapter);

#ifdef ENABLE_USB_DROP_INCORRECT_OUT
	_InitHardwareDropIncorrectBulkOut(padapter);
#endif

	/* Enable MACTXEN/MACRXEN block */
	u1bRegCR = rtw_read8(padapter, REG_CR);
	u1bRegCR |= (MACTXEN | MACRXEN);
	rtw_write8(padapter, REG_CR, u1bRegCR);

#ifdef CONFIG_RTW_LED
	_InitHWLed(padapter);
#endif /* CONFIG_RTW_LED */

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(padapter);
	/* NicIFSetMacAddress(padapter, padapter->PermanentAddress); */


	rtw_hal_set_chnl_bw(padapter, padapter->registrypriv.channel,
		CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HAL_PRIME_CHNL_OFFSET_DONT_CARE);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
	invalidate_cam_all(padapter);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	/* 2010/12/17 MH We need to set TX power according to EFUSE content at first. */
	/* rtw_hal_set_tx_power_level(padapter, pHalData->current_channel); */
	rtl8723b_InitAntenna_Selection(padapter);

	/* HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);

	/*  */
	/* Disable BAR, suggested by Scott */
	/* 2010.04.09 add by hpfan */
	/*  */
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	if (pregistrypriv->wifi_spec)
		rtw_write16(padapter, REG_FAST_EDCA_CTRL , 0);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
	rtl8723b_InitHalDm(padapter);

#if (MP_DRIVER == 1)
	if (padapter->registrypriv.mp_mode == 1) {
		padapter->mppriv.channel = pHalData->current_channel;
		MPT_InitializeAdapter(padapter, padapter->mppriv.channel);
	} else
#endif
	{
		pwrctrlpriv->rf_pwrstate = rf_on;

		/*phy_lc_calibrate_8723b(&pHalData->odmpriv);*/
		halrf_lck_trigger(&pHalData->odmpriv);

		pHalData->neediqk_24g = _TRUE;

		odm_txpowertracking_check(&pHalData->odmpriv);
	}


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC21);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BT_COEXIST);
#ifdef CONFIG_BT_COEXIST
	/* Init BT hw config. */
	rtw_btcoex_HAL_Initialize(padapter, _FALSE);
#else
	rtw_btcoex_HAL_Initialize(padapter, _TRUE);
#endif

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC31);
	rtw_hal_set_hwreg(padapter, HW_VAR_NAV_UPPER, (u8 *)&NavUpper);

#ifdef CONFIG_XMIT_ACK
	/* ack for xmit mgmt frames. */
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, rtw_read32(padapter, REG_FWHW_TXQ_CTRL) | BIT(12));
#endif /* CONFIG_XMIT_ACK */

exit:
	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_END);

	RTW_INFO("%s in %dms\n", __FUNCTION__, rtw_get_passing_time_ms(init_start_time));

#ifdef DBG_HAL_INIT_PROFILING
	hal_init_stages_timestamp[HAL_INIT_STAGES_END] = rtw_get_current_time();

	for (hal_init_profiling_i = 0; hal_init_profiling_i < HAL_INIT_STAGES_NUM - 1; hal_init_profiling_i++) {
		RTW_INFO("DBG_HAL_INIT_PROFILING: %35s, %u, %5u, %5u\n"
			 , hal_init_stages_str[hal_init_profiling_i]
			 , hal_init_stages_timestamp[hal_init_profiling_i]
			, (hal_init_stages_timestamp[hal_init_profiling_i + 1] - hal_init_stages_timestamp[hal_init_profiling_i])
			, rtw_get_time_interval_ms(hal_init_stages_timestamp[hal_init_profiling_i], hal_init_stages_timestamp[hal_init_profiling_i + 1])
			);
	}
#endif


	return status;
}

#if 0
static void
_ResetFWDownloadRegister(
		PADAPTER			Adapter
)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_MCUFWDL);
	value32 &= ~(MCUFWDL_EN | MCUFWDL_RDY);
	rtw_write32(Adapter, REG_MCUFWDL, value32);
}

static void
_ResetBB(
		PADAPTER			Adapter
)
{
	u16	value16;

	/* reset BB */
	value16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	value16 &= ~(FEN_BBRSTB | FEN_BB_GLB_RSTn);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, value16);
}

static void
_ResetMCU(
		PADAPTER			Adapter
)
{
	u16	value16;

	/* reset MCU */
	value16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	value16 &= ~FEN_CPUEN;
	rtw_write16(Adapter, REG_SYS_FUNC_EN, value16);
}

static void
_DisableMAC_AFE_PLL(
		PADAPTER			Adapter
)
{
	u32	value32;

	/* disable MAC/ AFE PLL */
	value32 = rtw_read32(Adapter, REG_APS_FSMCO);
	value32 |= APDM_MAC;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

	value32 |= APFM_OFF;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);
}

static void
_AutoPowerDownToHostOff(
		PADAPTER		Adapter
)
{
	u32			value32;
	rtw_write8(Adapter, REG_SPS0_CTRL, 0x22);

	value32 = rtw_read32(Adapter, REG_APS_FSMCO);

	value32 |= APDM_HOST;/* card disable */
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

	/* set USB suspend */
	value32 = rtw_read32(Adapter, REG_APS_FSMCO);
	value32 &= ~AFSM_PCIE;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

}

static void
_SetUsbSuspend(
		PADAPTER			Adapter
)
{
	u32			value32;

	value32 = rtw_read32(Adapter, REG_APS_FSMCO);

	/* set USB suspend */
	value32 |= AFSM_HSUS;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

	/* RT_ASSERT(0 == (rtw_read32(Adapter, REG_APS_FSMCO) & BIT(12)),("")); */

}

static void
_DisableRFAFEAndResetBB(
		PADAPTER			Adapter
)
{
#if 0
	/* *************************************
	 * a.	TXPAUSE 0x522[7:0] = 0xFF              Pause MAC TX queue
	 * b.	RF path 0 offset 0x00 = 0x00              disable RF
	 * c.	APSD_CTRL 0x600[7:0] = 0x40
	 * d.	SYS_FUNC_EN 0x02[7:0] = 0x16		 reset BB state machine
	 * e.	SYS_FUNC_EN 0x02[7:0] = 0x14		 reset BB state machine
	 * ************************************** */
#endif
	enum rf_path eRFPath = RF_PATH_A, value8 = 0;
	rtw_write8(Adapter, REG_TXPAUSE, 0xFF);
	phy_set_rf_reg(Adapter, eRFPath, 0x0, bMaskByte0, 0x0);

	value8 |= APSDOFF;
	rtw_write8(Adapter, REG_APSD_CTRL, value8);/* 0x40 */

	value8 = 0 ;
	value8 |= (FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtw_write8(Adapter, REG_SYS_FUNC_EN, value8);/* 0x16		 */

	value8 &= (~FEN_BB_GLB_RSTn);
	rtw_write8(Adapter, REG_SYS_FUNC_EN, value8); /* 0x14		 */

}

static void
_ResetDigitalProcedure1(
		PADAPTER			Adapter,
		BOOLEAN				bWithoutHWSM
)
{

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if (pHalData->firmware_version <=  0x20) {
#if 0
#if 0
		/* **************************** */
		/* f.	SYS_FUNC_EN 0x03[7:0]=0x54		  reset MAC register, DCORE */
		/* g.	MCUFWDL 0x80[7:0]=0				  reset MCU ready status */
		/* ***************************** */
#endif
		u32	value32 = 0;
		PlatformIOWrite1Byte(Adapter, REG_SYS_FUNC_EN + 1, 0x54);
		PlatformIOWrite1Byte(Adapter, REG_MCUFWDL, 0);
#else
#if 0
		/* **************************** */
		/* f.	MCUFWDL 0x80[7:0]=0				  reset MCU ready status */
		/* g.	SYS_FUNC_EN 0x02[10]= 0			  reset MCU register, (8051 reset) */
		/* h.	SYS_FUNC_EN 0x02[15-12]= 5		  reset MAC register, DCORE */
		/* i.     SYS_FUNC_EN 0x02[10]= 1			  enable MCU register, (8051 enable) */
		/* ***************************** */
#endif
		u16 valu16 = 0;
		rtw_write8(Adapter, REG_MCUFWDL, 0);

		valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
		rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 & (~FEN_CPUEN)));/* reset MCU ,8051 */

		valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN) & 0x0FFF;
		rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 | (FEN_HWPDN | FEN_ELDR))); /* reset MAC */

#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
		{
			u8 val;
			val = rtw_read8(Adapter, REG_MCUFWDL);
			if (val)
				RTW_INFO("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __FUNCTION__, __LINE__, val);
		}
#endif


		valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
		rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 | FEN_CPUEN));/* enable MCU ,8051	 */


#endif
	} else {
		u8 retry_cnts = 0;

		if (rtw_read8(Adapter, REG_MCUFWDL) & BIT1) {
			/* IF fw in RAM code, do reset */

			rtw_write8(Adapter, REG_MCUFWDL, 0);
			if (GET_HAL_DATA(Adapter)->bFWReady) {
				/* 2010/08/25 MH Accordign to RD alfred's suggestion, we need to disable other */
				/* HRCV INT to influence 8051 reset. */
				rtw_write8(Adapter, REG_FWIMR, 0x20);

				rtw_write8(Adapter, REG_HMETFR + 3, 0x20); /* 8051 reset by self */

				while ((retry_cnts++ < 100) && (FEN_CPUEN & rtw_read16(Adapter, REG_SYS_FUNC_EN))) {
					rtw_udelay_os(50);/* PlatformStallExecution(50); */ /* us */
				}

				if (retry_cnts >= 100) {
					RTW_INFO("%s #####=> 8051 reset failed!.........................\n", __FUNCTION__);
					/* if 8051 reset fail we trigger GPIO 0 for LA */
					/* PlatformEFIOWrite4Byte(	Adapter, */
					/*						REG_GPIO_PIN_CTRL,  */
					/*						0x00010100); */
					/* 2010/08/31 MH According to Filen's info, if 8051 reset fail, reset MAC directly. */
					rtw_write8(Adapter, REG_SYS_FUNC_EN + 1, 0x50);	/* Reset MAC and Enable 8051 */
					rtw_mdelay_os(10);
				} else {
					/* RTW_INFO("%s =====> 8051 reset success (%d) .\n", __FUNCTION__, retry_cnts); */
				}
			} else
				RTW_INFO("%s =====> 8051 in RAM but !hal_data->bFWReady\n", __FUNCTION__);
		} else {
			/* RTW_INFO("%s =====> 8051 in ROM.\n", __FUNCTION__); */
		}

#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
		{
			u8 val;
			val = rtw_read8(Adapter, REG_MCUFWDL);
			if (val)
				RTW_INFO("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __FUNCTION__, __LINE__, val);
		}
#endif

		rtw_write8(Adapter, REG_SYS_FUNC_EN + 1, 0x54);	/* Reset MAC and Enable 8051 */
	}

	/* Clear rpwm value for initial toggle bit trigger. */
	rtw_write8(Adapter, REG_USB_HRPWM, 0x00);

	if (bWithoutHWSM) {
#if 0
		/* **************************** */
		/* Without HW auto state machine */
		/* g.	SYS_CLKR 0x08[15:0] = 0x30A3			 disable MAC clock */
		/* h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			 disable AFE PLL */
		/* i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		 gated AFE DIG_CLOCK */
		/* j.	SYS_ISO_CTRL 0x00[7:0] = 0xF9			  isolated digital to PON */
		/* ***************************** */
#endif

		/* rtw_write16(Adapter, REG_SYS_CLKR, 0x30A3); */
		rtw_write16(Adapter, REG_SYS_CLKR, 0x70A3);/* modify to 0x70A3 by Scott. */
		rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x80);
		rtw_write16(Adapter, REG_AFE_XTAL_CTRL, 0x880F);
		rtw_write8(Adapter, REG_SYS_ISO_CTRL, 0xF9);
	} else {
		/* Disable all RF/BB power */
		rtw_write8(Adapter, REG_RF_CTRL, 0x00);
	}

}

static void
_ResetDigitalProcedure2(
		PADAPTER			Adapter
)
{
#if 0
	/* ****************************
	 * k.	SYS_FUNC_EN 0x03[7:0] = 0x44			  disable ELDR runction
	 * l.	SYS_CLKR 0x08[15:0] = 0x3083			  disable ELDR clock
	 * m.	SYS_ISO_CTRL 0x01[7:0] = 0x83			  isolated ELDR to PON
	 * ***************************** */
#endif
	/* rtw_write8(Adapter, REG_SYS_FUNC_EN+1, 0x44); */ /* marked by Scott. */
	/* rtw_write16(Adapter, REG_SYS_CLKR, 0x3083); */
	/* rtw_write8(Adapter, REG_SYS_ISO_CTRL+1, 0x83); */

	rtw_write16(Adapter, REG_SYS_CLKR, 0x70a3); /* modify to 0x70a3 by Scott. */
	rtw_write8(Adapter, REG_SYS_ISO_CTRL + 1, 0x82); /* modify to 0x82 by Scott. */
}

static void
_DisableAnalog(
		PADAPTER			Adapter,
		BOOLEAN			bWithoutHWSM
)
{
	u16 value16 = 0;
	u8 value8 = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if (bWithoutHWSM) {
#if 0
		/* **************************** */
		/* n.	LDOA15_CTRL 0x20[7:0] = 0x04		  disable A15 power */
		/* o.	LDOV12D_CTRL 0x21[7:0] = 0x54		  disable digital core power */
		/* r.	When driver call disable, the ASIC will turn off remaining clock automatically */
		/* ***************************** */
#endif

		rtw_write8(Adapter, REG_LDOA15_CTRL, 0x04);
		/* PlatformIOWrite1Byte(Adapter, REG_LDOV12D_CTRL, 0x54);		 */

		value8 = rtw_read8(Adapter, REG_LDOV12D_CTRL);
		value8 &= (~LDV12_EN);
		rtw_write8(Adapter, REG_LDOV12D_CTRL, value8);
	}

#if 0
	/* ****************************
	 * h.	SPS0_CTRL 0x11[7:0] = 0x23			 enter PFM mode
	 * i.	APS_FSMCO 0x04[15:0] = 0x4802		  set USB suspend
	 * ***************************** */
#endif



	value8 = 0x23;

	rtw_write8(Adapter, REG_SPS0_CTRL, value8);


	if (bWithoutHWSM) {
		/* value16 |= (APDM_HOST | AFSM_HSUS |PFM_ALDN); */
		/* 2010/08/31 According to Filen description, we need to use HW to shut down 8051 automatically. */
		/* Becasue suspend operatione need the asistance of 8051 to wait for 3ms. */
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);
	} else
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);

	rtw_write16(Adapter, REG_APS_FSMCO, value16);/* 0x4802 */

	rtw_write8(Adapter, REG_RSV_CTRL, 0x0e);

#if 0
	/* tynli_test for suspend mode. */
	if (!bWithoutHWSM)
		rtw_write8(Adapter, 0xfe10, 0x19);
#endif

}

static void rtl8723bu_hw_power_down(_adapter *padapter)
{
	u8	u1bTmp;

	RTW_INFO("PowerDownRTL8723U\n");


	/* 1. Run Card Disable Flow */
	/* Done before this function call. */

	/* 2. 0x04[16] = 0			 */ /* reset WLON */
	u1bTmp = rtw_read8(padapter, REG_APS_FSMCO + 2);
	rtw_write8(padapter, REG_APS_FSMCO + 2, (u1bTmp & (~BIT0)));

	/* 3. 0x04[12:11] = 2b'11  */ /* enable suspend */
	/* Done before this function call. */

	/* 4. 0x04[15] = 1			 */ /* enable PDN */
	u1bTmp = rtw_read8(padapter, REG_APS_FSMCO + 1);
	rtw_write8(padapter, REG_APS_FSMCO + 1, (u1bTmp | BIT7));
}
#endif

/*
 * Description: RTL8723e card disable power sequence v003 which suggested by Scott.
 * First created by tynli. 2011.01.28.
 *   */
void
CardDisableRTL8723U(
	PADAPTER			Adapter
)
{
	u8		u1bTmp;

	rtw_hal_get_hwreg(Adapter, HW_VAR_APFM_ON_MAC, &u1bTmp);
	RTW_INFO(FUNC_ADPT_FMT ": bMacPwrCtrlOn=%d\n", FUNC_ADPT_ARG(Adapter), u1bTmp);
	if (u1bTmp == _FALSE)
		return;
	u1bTmp = _FALSE;
	rtw_hal_set_hwreg(Adapter, HW_VAR_APFM_ON_MAC, &u1bTmp);

	/* Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	u1bTmp = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter, REG_TX_RPT_CTRL, u1bTmp & (~BIT1));

	/* stop rx */
	rtw_write8(Adapter, REG_CR_8723B, 0x0);

	/* 1. Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8723B_enter_lps_flow);

	if ((rtw_read8(Adapter, REG_MCUFWDL_8723B) & BIT7) &&
	    GET_HAL_DATA(Adapter)->bFWReady) /* 8051 RAM code */
		rtl8723b_FirmwareSelfReset(Adapter);

	/* Reset MCU. Suggested by Filen. 2011.01.26. by tynli. */
	u1bTmp = rtw_read8(Adapter, REG_SYS_FUNC_EN_8723B + 1);
	rtw_write8(Adapter, REG_SYS_FUNC_EN_8723B + 1, (u1bTmp & (~BIT2)));

	/* MCUFWDL 0x80[1:0]=0				 */ /* reset MCU ready status */
	rtw_write8(Adapter, REG_MCUFWDL_8723B, 0x00);

	/* Card disable power action flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8723B_card_disable_flow);

	GET_HAL_DATA(Adapter)->bFWReady = _FALSE;
}


u32 rtl8723bu_hal_deinit(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);


	RTW_INFO("==> %s\n", __FUNCTION__);


	/* 2011/02/18 To Fix RU LNA  power leakage problem. We need to execute below below in */
	/* Adapter init and halt sequence. Accordingto EEchou's opinion, we can enable the ability for all */
	/* IC. Accord to johnny's opinion, only RU need the support. */
	rtw_hal_power_off(padapter);

	return _SUCCESS;
}


unsigned int rtl8723bu_inirp_init(PADAPTER Adapter)
{
	u8 i;
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev = adapter_to_dvobj(Adapter);
	struct intf_hdl *pintfhdl = &Adapter->iopriv.intf;
	struct recv_priv *precvpriv = &(Adapter->recvpriv);
	u32(*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	u32(*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#endif /* CONFIG_USB_INTERRUPT_IN_PIPE */


	_read_port = pintfhdl->io_ops._read_port;

	status = _SUCCESS;


	precvpriv->ff_hwaddr = RECV_BULK_IN_ADDR;

	/* issue Rx irp to receive data	 */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (_read_port(pintfhdl, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf) == _FALSE) {
			status = _FAIL;
			goto exit;
		}

		precvbuf++;
		precvpriv->free_recv_buf_queue_cnt--;
	}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	_read_interrupt = pintfhdl->io_ops._read_interrupt;
	if (_read_interrupt(pintfhdl, RECV_INT_IN_ADDR) == _FALSE) {
		status = _FAIL;
	}
	pHalData->IntrMask[0] = rtw_read32(Adapter, REG_USB_HIMR);
	RTW_INFO("pHalData->IntrMask = 0x%04x\n", pHalData->IntrMask[0]);
	pHalData->IntrMask[0] |= UHIMR_C2HCMD | UHIMR_CPWM;
	rtw_write32(Adapter, REG_USB_HIMR, pHalData->IntrMask[0]);
#endif /* CONFIG_USB_INTERRUPT_IN_PIPE */

exit:



	return status;

}

unsigned int rtl8723bu_inirp_deinit(PADAPTER Adapter)
{
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	u32(*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#endif /* CONFIG_USB_INTERRUPT_IN_PIPE */

	rtw_read_port_cancel(Adapter);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	pHalData->IntrMask[0] = rtw_read32(Adapter, REG_USB_HIMR);
	RTW_INFO("%s pHalData->IntrMask = 0x%04x\n", __FUNCTION__, pHalData->IntrMask[0]);
	pHalData->IntrMask[0] = 0x0;
	rtw_write32(Adapter, REG_USB_HIMR, pHalData->IntrMask[0]);
#endif /* CONFIG_USB_INTERRUPT_IN_PIPE */
	return _SUCCESS;
}

/* -------------------------------------------------------------------
 *
 *	EEPROM/EFUSE Content Parsing
 *
 * ------------------------------------------------------------------- */
#if 0
static void
_ReadLEDSetting(
		PADAPTER	Adapter,
		u8		*PROMContent,
		BOOLEAN		AutoloadFail
)
{
#ifdef CONFIG_RTW_LED
	struct led_priv *pledpriv = adapter_to_led(Adapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#ifdef CONFIG_RTW_SW_LED
	pledpriv->bRegUseLed = _TRUE;

	/*  */
	/* Led mode */
	/*  */
	switch (pHalData->CustomerID) {
	case RT_CID_DEFAULT:
		pledpriv->LedStrategy = SW_LED_MODE1;
		pledpriv->bRegUseLed = _TRUE;
		break;

	case RT_CID_819x_HP:
		pledpriv->LedStrategy = SW_LED_MODE6;
		break;

	default:
		pledpriv->LedStrategy = SW_LED_MODE1;
		break;
	}

	pHalData->bLedOpenDrain = _TRUE;/* Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16. */
#else /* HW LED */
	pledpriv->LedStrategy = HW_LED;
#endif /* CONFIG_RTW_SW_LED */
#endif
}
#endif
/* Read HW power down mode selection */
#if 0
static void _ReadPSSetting(PADAPTER Adapter, u8 *PROMContent, u8	AutoloadFail)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(Adapter);

	if (AutoloadFail) {
		pwrctl->bHWPowerdown = _FALSE;
		pwrctl->bSupportRemoteWakeup = _FALSE;
	} else	{
		/* if(SUPPORT_HW_RADIO_DETECT(Adapter)) */
		pwrctl->bHWPwrPindetect = Adapter->registrypriv.hwpwrp_detect;
		/* else */
		/* pwrctl->bHWPwrPindetect = _FALSE; */ /* dongle not support new */


		/* hw power down mode selection , 0:rf-off / 1:power down */

		if (Adapter->registrypriv.hwpdn_mode == 2)
			pwrctl->bHWPowerdown = (PROMContent[EEPROM_FEATURE_OPTION_8723B] & BIT4);
		else
			pwrctl->bHWPowerdown = Adapter->registrypriv.hwpdn_mode;

		/* decide hw if support remote wakeup function */
		/* if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume */
		pwrctl->bSupportRemoteWakeup = (PROMContent[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT1) ? _TRUE : _FALSE;

		/* if(SUPPORT_HW_RADIO_DETECT(Adapter))	 */
		/* Adapter->registrypriv.usbss_enable = pwrctl->bSupportRemoteWakeup ; */

		RTW_INFO("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) ,bSupportRemoteWakeup(%x)\n", __FUNCTION__,
			pwrctl->bHWPwrPindetect, pwrctl->bHWPowerdown, pwrctl->bSupportRemoteWakeup);

		RTW_INFO("### PS params=>  power_mgnt(%x),usbss_enable(%x) ###\n", Adapter->registrypriv.power_mgnt, Adapter->registrypriv.usbss_enable);

	}

}
#endif

void
Hal_EfuseParsePIDVID_8723BU(
		PADAPTER		pAdapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (AutoLoadFail) {
		pHalData->EEPROMVID = 0;
		pHalData->EEPROMPID = 0;
	} else {
		/* VID, PID */
		pHalData->EEPROMVID = le16_to_cpu(*(u16 *)&hwinfo[EEPROM_VID_8723BU]);
		pHalData->EEPROMPID = le16_to_cpu(*(u16 *)&hwinfo[EEPROM_PID_8723BU]);

	}

	RTW_INFO("EEPROM VID = 0x%4x\n", pHalData->EEPROMVID);
	RTW_INFO("EEPROM PID = 0x%4x\n", pHalData->EEPROMPID);
}

static void
InitAdapterVariablesByPROM_8723BU(
		PADAPTER	padapter
)
{

	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8			*hwinfo = NULL;

	if (sizeof(pHalData->efuse_eeprom_data) < HWSET_MAX_SIZE_8723B)
		RTW_INFO("[WARNING] size of efuse_eeprom_data is less than HWSET_MAX_SIZE_8723B!\n");

	hwinfo = pHalData->efuse_eeprom_data;

	Hal_InitPGData(padapter, hwinfo);

	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParsePIDVID_8723BU(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseEEPROMVer_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	hal_config_macaddr(padapter, pHalData->bautoload_fail_flag);
	Hal_EfuseParseTxPowerInfo_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseBoardType_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);

	Hal_EfuseParseBTCoexistInfo_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);

	Hal_EfuseParseChnlPlan_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseThermalMeter_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	/*	_ReadLEDSetting(Adapter, PROMContent, pHalData->bautoload_fail_flag);
	 *	_ReadPSSetting(Adapter, PROMContent, pHalData->bautoload_fail_flag); */
	Hal_EfuseParseAntennaDiversity_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);

	Hal_EfuseParseEEPROMVer_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseCustomerID_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	/*	Hal_EfuseParseRateIndicationOption(padapter, hwinfo, pHalData->bautoload_fail_flag); */
	Hal_EfuseParseXtal_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	/*  */
	/* The following part initialize some vars by PG info. */
	/*  */

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	Hal_DetectWoWMode(padapter);
#endif

	/* hal_CustomizedBehavior_8723U(Adapter); */

	/* set coex. ant info once efuse parsing is done */
	rtw_btcoex_set_ant_info(padapter);

	/*	Adapter->bDongle = (PROMContent[EEPROM_EASY_REPLACEMENT] == 1)? 0: 1; */
	RTW_INFO("%s(): REPLACEMENT = %x\n", __FUNCTION__, padapter->bDongle);
}

static void _ReadPROMContent(
		PADAPTER		Adapter
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);

	u8			eeValue;
	u32			i;
	u16			value16;

	eeValue = rtw_read8(Adapter, REG_9346CR);
	/* To check system boot selection. */
	pHalData->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pHalData->bautoload_fail_flag	= (eeValue & EEPROM_EN) ? _FALSE : _TRUE;


	RTW_INFO("Boot from %s, Autoload %s !\n", (pHalData->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		 (pHalData->bautoload_fail_flag ? "Fail" : "OK"));


	InitAdapterVariablesByPROM_8723BU(Adapter);
}

static void
_ReadRFType(
		PADAPTER	Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif
}



/*
 *	Description:
 *		We should set Efuse cell selection to WiFi cell in default.
 *
 *	Assumption:
 *		PASSIVE_LEVEL
 *
 *	Added by Roger, 2010.11.23.
 *   */
void
hal_EfuseCellSel(
		PADAPTER	Adapter
)
{
	u32			value32;

	value32 = rtw_read32(Adapter, EFUSE_TEST);
	value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
	rtw_write32(Adapter, EFUSE_TEST, value32);
}

static u8 ReadAdapterInfo8723BU(PADAPTER Adapter)
{
	/* Read EEPROM size before call any EEPROM function */
	Adapter->EepromAddressSize = GetEEPROMSize8723B(Adapter);

	hal_EfuseCellSel(Adapter);

	_ReadRFType(Adapter);/* rf_chip->_InitRFType() */
	_ReadPROMContent(Adapter);

	return _SUCCESS;
}

#define GPIO_DEBUG_PORT_NUM 0
static void rtl8723bu_trigger_gpio_0(_adapter *padapter)
{

	u32 gpioctrl;
	RTW_INFO("==> trigger_gpio_0...\n");
	rtw_write16_async(padapter, REG_GPIO_PIN_CTRL, 0);
	rtw_write8_async(padapter, REG_GPIO_PIN_CTRL + 2, 0xFF);
	gpioctrl = (BIT(GPIO_DEBUG_PORT_NUM) << 24) | (BIT(GPIO_DEBUG_PORT_NUM) << 16);
	rtw_write32_async(padapter, REG_GPIO_PIN_CTRL, gpioctrl);
	gpioctrl |= (BIT(GPIO_DEBUG_PORT_NUM) << 8);
	rtw_write32_async(padapter, REG_GPIO_PIN_CTRL, gpioctrl);
	RTW_INFO("<=== trigger_gpio_0...\n");

}

/*
 * If variable not handled here,
 * some variables will be processed in SetHwReg8723A()
 */
u8 SetHwReg8723BU(PADAPTER Adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u8 ret = _SUCCESS;

	switch (variable) {
	case HW_VAR_RXDMA_AGG_PG_TH:
#ifdef CONFIG_USB_RX_AGGREGATION
		{
			/* threshold == 1 , Disable Rx-agg when AP is B/G mode or wifi_spec=1 to prevent bad TP. */

			u8	threshold = *((u8 *)val);
			u32 agg_size;

			if (threshold == 0) {
				switch (pHalData->rxagg_mode) {
					case RX_AGG_DMA:
						agg_size = pHalData->rxagg_dma_size << 10;
						if (agg_size > RX_DMA_BOUNDARY_8723B)
							agg_size = RX_DMA_BOUNDARY_8723B >> 1;
						if ((agg_size + 2048) > MAX_RECVBUF_SZ)
							agg_size = MAX_RECVBUF_SZ - 2048;
						agg_size >>= 10; /* unit: 1K */
						if (agg_size > 0xF)
							agg_size = 0xF;

						threshold = (agg_size & 0xF);
						break;
					case RX_AGG_USB:
					case RX_AGG_MIX:
						agg_size = pHalData->rxagg_usb_size << 12;
						if ((agg_size + 2048) > MAX_RECVBUF_SZ)
							agg_size = MAX_RECVBUF_SZ - 2048;
						agg_size >>= 12; /* unit: 4K */
						if (agg_size > 0xF)
							agg_size = 0xF;

						threshold = (agg_size & 0xF);
						break;
					case RX_AGG_DISABLE:
					default:
						break;
				}
			}

			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, threshold);

#ifdef CONFIG_80211N_HT
			{
				/* 2014-07-24 Fix WIFI Logo -5.2.4/5.2.9 - DT3 low TP issue */
				/* Adjust RxAggrTimeout to close to zero disable RxAggr for RxAgg-USB mode, suggested by designer */
				/* Timeout value is calculated by 34 / (2^n) */
				struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
				struct ht_priv		*phtpriv = &pmlmepriv->htpriv;

				if (pHalData->rxagg_mode == RX_AGG_USB) {
					/* BG mode || (wifi_spec=1 && BG mode Testbed)	 */
					if ((threshold == 1) && (phtpriv->ht_option == _FALSE))
						rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH + 1, 0);
					else
						rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH + 1, pHalData->rxagg_usb_timeout);
				}
			}
#endif/* CONFIG_80211N_HT */ 
		}
#endif/* CONFIG_USB_RX_AGGREGATION */
		break;

	case HW_VAR_SET_RPWM:
		rtw_write8(Adapter, REG_USB_HRPWM, *val);
		break;

	case HW_VAR_TRIGGER_GPIO_0:
		rtl8723bu_trigger_gpio_0(Adapter);
		break;

	default:
		ret = SetHwReg8723B(Adapter, variable, val);
		break;
	}

	return ret;
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8723A()
 */
void GetHwReg8723BU(PADAPTER Adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);


	switch (variable) {
	default:
		GetHwReg8723B(Adapter, variable, val);
		break;
	}

}

/*
 *	Description:
 *		Query setting of specified variable.
 *   */
u8
GetHalDefVar8723BUsb(
		PADAPTER				Adapter,
		HAL_DEF_VARIABLE		eVariable,
		void						*pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
		*((u8 *)pValue) = _FALSE;
#endif
		break;

	case HAL_DEF_DRVINFO_SZ:
		*((u32 *)pValue) = DRVINFO_SZ;
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pValue) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		*((u32 *)pValue) = RXDESC_SIZE + DRVINFO_SZ * 8;
		break;
	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		*((HT_CAP_AMPDU_FACTOR *)pValue) = MAX_AMPDU_FACTOR_64K;
		break;
	default:
		bResult = GetHalDefVar8723B(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}




/*
 *	Description:
 *		Change default setting of specified variable.
 *   */
u8
SetHalDefVar8723BUsb(
		PADAPTER				Adapter,
		HAL_DEF_VARIABLE		eVariable,
		void						*pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	default:
		bResult = SetHalDefVar8723B(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}

static u8 rtl8723bu_ps_func(PADAPTER Adapter, HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	u8 bResult = _TRUE;
	switch (efunc_id) {

#if defined(CONFIG_AUTOSUSPEND) && defined(SUPPORT_HW_RFOFF_DETECTED)
	case HAL_USB_SELECT_SUSPEND: {
		u8 bfwpoll = *((u8 *)val);
		rtl8723b_set_FwSelectSuspend_cmd(Adapter, bfwpoll , 500); /* note fw to support hw power down ping detect */
	}
	break;
#endif /* CONFIG_AUTOSUSPEND && SUPPORT_HW_RFOFF_DETECTED */

	default:
		break;
	}
	return bResult;
}

void rtl8723bu_set_hal_ops(_adapter *padapter)
{
	struct hal_ops	*pHalFunc = &padapter->hal_func;


	rtl8723b_set_hal_ops(pHalFunc);

	pHalFunc->hal_power_on = &_InitPowerOn_8723BU;
	pHalFunc->hal_power_off = &CardDisableRTL8723U;

	pHalFunc->hal_init = &rtl8723bu_hal_init;
	pHalFunc->hal_deinit = &rtl8723bu_hal_deinit;

	pHalFunc->inirp_init = &rtl8723bu_inirp_init;
	pHalFunc->inirp_deinit = &rtl8723bu_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8723bu_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8723bu_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8723bu_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8723bu_free_recv_priv;
#ifdef CONFIG_RTW_SW_LED
	pHalFunc->InitSwLeds = &rtl8723bu_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8723bu_DeInitSwLeds;
#endif/* CONFIG_RTW_SW_LED */

	pHalFunc->init_default_value = &rtl8723b_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8723bu_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8723BU;

	pHalFunc->set_hw_reg_handler = &SetHwReg8723BU;
	pHalFunc->GetHwRegHandler = &GetHwReg8723BU;
	pHalFunc->get_hal_def_var_handler = &GetHalDefVar8723BUsb;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8723BUsb;

	pHalFunc->hal_xmit = &rtl8723bu_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8723bu_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8723bu_hal_xmitframe_enqueue;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8723bu_hostap_mgnt_xmit_entry;
#endif
	pHalFunc->interface_ps_func = &rtl8723bu_ps_func;

#ifdef CONFIG_XMIT_THREAD_MODE
	pHalFunc->xmit_thread_handler = &rtl8723bu_xmit_buf_handler;
#endif
#ifdef CONFIG_SUPPORT_USB_INT
	pHalFunc->interrupt_handler = interrupt_handler_8723bu;
#endif

}
