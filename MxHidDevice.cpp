#pragma once
#include "stdafx.h"
#include <basetyps.h>
#include <setupapi.h>
#include <initguid.h>
#pragma warning( push )
#pragma warning( disable : 4201 )

extern "C" {
    #include "hidsdi.h"
}
#pragma warning( pop )
#include "hiddevice.h"
#include "MxHidDevice.h"
#include "MemoryInit.h"


#define DCD_WRITE

MxHidDevice::MxHidDevice()
{
    //Initialize(MX508_USB_VID,MX508_USB_PID);
	_chipFamily = MX508;

    //InitMemoryDevice();
	//TRACE("************The new i.mx device is initialized**********\n");
	//TRACE("\n");
	return;
//Exit:
//	TRACE("Failed to initialize the new i.MX device!!!\n");
}

MxHidDevice::~MxHidDevice()
{
}

/// <summary>
//-------------------------------------------------------------------------------------		
// Function to 16 byte SDP command format, these 16 bytes will be sent by host to 
// device in SDP command field of report 1 data structure
//
// @return
//		a report packet to be sent.
//-------------------------------------------------------------------------------------
//
VOID MxHidDevice::PackSDPCmd(PSDPCmd pSDPCmd)
{
    memset((UCHAR *)m_pWriteReport, 0, m_Capabilities.OutputReportByteLength);
    m_pWriteReport->ReportId = (unsigned char)REPORT_ID_SDP_CMD;
    PLONG pTmpSDPCmd = (PLONG)(m_pWriteReport->Payload);

	pTmpSDPCmd[0] = (  ((pSDPCmd->address  & 0x00FF0000) << 8) 
		          | ((pSDPCmd->address  & 0xFF000000) >> 8) 
		          |  (pSDPCmd->command   & 0x0000FFFF) );

	pTmpSDPCmd[1] = (   (pSDPCmd->dataCount & 0xFF000000)
		          | ((pSDPCmd->format   & 0x000000FF) << 16)
		          | ((pSDPCmd->address  & 0x000000FF) <<  8)
		          | ((pSDPCmd->address  & 0x0000FF00) >>  8 ));

	pTmpSDPCmd[2] = (   (pSDPCmd->data     & 0xFF000000)
		          | ((pSDPCmd->dataCount & 0x000000FF) << 16)
		          |  (pSDPCmd->dataCount & 0x0000FF00)
		          | ((pSDPCmd->dataCount & 0x00FF0000) >> 16));

	pTmpSDPCmd[3] = (  ((0x00  & 0x000000FF) << 24)
		          | ((pSDPCmd->data     & 0x00FF0000) >> 16) 
		          |  (pSDPCmd->data     & 0x0000FF00)
		          | ((pSDPCmd->data     & 0x000000FF) << 16));   

}

//Report1 
BOOL MxHidDevice::SendCmd(PSDPCmd pSDPCmd)
{
	//First, pack the command to a report.
	PackSDPCmd(pSDPCmd);

	//Send the report to USB HID device
	if ( Write((unsigned char *)m_pWriteReport, m_Capabilities.OutputReportByteLength) != ERROR_SUCCESS)
	{
		return FALSE;
	}

	return TRUE;
}

//Report 2
BOOL MxHidDevice::SendData(const unsigned char * DataBuf, UINT ByteCnt)
{
	memcpy(m_pWriteReport->Payload, DataBuf, ByteCnt);

	m_pWriteReport->ReportId = REPORT_ID_DATA;
	if (Write((unsigned char *)m_pWriteReport, m_Capabilities.OutputReportByteLength) != ERROR_SUCCESS)
		return FALSE;	

	return TRUE;
}

//Report3, Device to Host
BOOL MxHidDevice::GetHABType()
{
    memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);

    //Get Report3, Device to Host:
    //4 bytes HAB mode indicating Production/Development part
	if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength )  != ERROR_SUCCESS)
	{
		return FALSE;
	}
	if ( (*(unsigned int *)(m_pReadReport->Payload) != HabEnabled)  && 
		 (*(unsigned int *)(m_pReadReport->Payload) != HabDisabled) ) 
	{
		return FALSE;	
	}

	return TRUE;
}

//Report4, Device to Host
BOOL MxHidDevice::GetDevAck(UINT RequiredCmdAck)
{
    memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);

    //Get Report4, Device to Host:
	if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength ) != ERROR_SUCCESS)
	{
		return FALSE;
	}

	if (*(unsigned int *)(m_pReadReport->Payload) != RequiredCmdAck)
	{
		TRACE("WriteReg(): Invalid write ack: 0x%x\n", ((PULONG)m_pReadReport)[0]);
		return FALSE; 
	}

    return TRUE;
}

BOOL MxHidDevice::GetCmdAck(UINT RequiredCmdAck)
{
	if(!GetHABType())
		return FALSE;

	if(!GetDevAck(RequiredCmdAck))
		return FALSE;

    return TRUE;
}

BOOL MxHidDevice::WriteReg(PSDPCmd pSDPCmd)
{
	if(!SendCmd(pSDPCmd))
		return FALSE;

	if ( !GetCmdAck(ROM_WRITE_ACK) )
	{
		return FALSE;
	}
    
    return TRUE;
}


#ifndef DCD_WRITE
BOOL MxHidDevice::InitMemoryDevice(MemoryType MemType)
{
    SDPCmd SDPCmd;

    SDPCmd.command = ROM_KERNEL_CMD_WR_MEM;
    SDPCmd.dataCount = 4;

    stMemoryInit * pMemPara = (MemType == LPDDR2) ? Mx508LPDDR2 : Mx508MDDR;
    UINT RegCount = (MemType == LPDDR2) ? sizeof(Mx508LPDDR2) : sizeof(Mx508MDDR);

    for(int i=0; i<RegCount/sizeof(stMemoryInit); i++)
    {
        SDPCmd.format = pMemPara[i].format;
        SDPCmd.data = pMemPara[i].data;
        SDPCmd.address = pMemPara[i].addr;
        if ( !WriteReg(&SDPCmd) )
        {
            TRACE("In InitMemoryDevice(): write memory failed\n");
            return FALSE;
        }
        _tprintf(_T("Reg #%d is initialized.\n"), i);
    }

	return TRUE;
}

#else

BOOL MxHidDevice::DCDWrite(PUCHAR DataBuf, UINT RegCount)
{
	SDPCmd SDPCmd;
    SDPCmd.command = ROM_KERNEL_CMD_DCD_WRITE;
    SDPCmd.format = 0;
    SDPCmd.data = 0;
    SDPCmd.address = 0;

	//Must reverse uint32 endian to adopt the requirement of ROM
	for(UINT i=0; i<RegCount*sizeof(stMemoryInit); i+=4)
	{
		UINT TempData = ((DataBuf[i]<<24) | (DataBuf[i+1]<<16) | (DataBuf[i+2] << 8) | (DataBuf[i+3]));
		((PUINT)DataBuf)[i/4] = TempData;
	}

    while(RegCount)
    {
		SDPCmd.dataCount = (RegCount > MAX_DCD_WRITE_REG_CNT) ? MAX_DCD_WRITE_REG_CNT : RegCount;
		RegCount -= SDPCmd.dataCount;
		UINT ByteCnt = SDPCmd.dataCount*sizeof(stMemoryInit);

		if(!SendCmd(&SDPCmd))
			return FALSE;

		if(!SendData(DataBuf, ByteCnt))
			return FALSE;

		if (!GetCmdAck(ROM_WRITE_ACK) )
		{
			return FALSE;
		}

		DataBuf += ByteCnt;
    }

	return TRUE;
}

BOOL MxHidDevice::InitMemoryDevice(MemoryType MemType)
{
	stMemoryInit * pMemPara;
    UINT RegCount;

	pMemPara = Mx50_LPDDR2;
	RegCount = sizeof(Mx50_LPDDR2);

	RegCount = RegCount/sizeof(stMemoryInit);

    if ( !DCDWrite((PUCHAR)pMemPara,RegCount) )
    {
        _tprintf(_T("Failed to initialize memory!\r\n"));
        return FALSE;
    }

	return TRUE;
}
#endif


BOOL MxHidDevice::RunPlugIn(UCHAR* pBuffer, ULONGLONG dataCount, PMxFunc pMxFunc)
{
	DWORD * pPlugIn = (DWORD *)pBuffer;
	DWORD PlugInDataOffset;
	DWORD BootDataImgAddrIndex;
	PIvtHeader pIVT = NULL,pIVT2 = NULL;

	//Search for IVT
	DWORD ImgIVTOffset=0;
	while(pPlugIn[ImgIVTOffset/sizeof(DWORD)] != IVT_BARKER_HEADER || ImgIVTOffset >= dataCount)
		ImgIVTOffset+= 0x100;
	
	if(ImgIVTOffset >= dataCount)
		return FALSE;

	pIVT = (PIvtHeader) (pPlugIn + ImgIVTOffset/sizeof(DWORD));
	DWORD IVT2Offset = ImgIVTOffset + sizeof(IvtHeader);

	while(pPlugIn[IVT2Offset/sizeof(DWORD)] != IVT_BARKER_HEADER || IVT2Offset >= dataCount)
		IVT2Offset+= sizeof(DWORD);
	
	if(IVT2Offset >= dataCount)
		return FALSE;

	pIVT2 = (PIvtHeader)(pPlugIn + IVT2Offset/sizeof(DWORD));

	BootDataImgAddrIndex = (pIVT2->BootData - pIVT2->SelfAddr)/sizeof(DWORD);
	BootDataImgAddrIndex += (DWORD *)pIVT2 - pPlugIn;
	pMxFunc->MxTrans.PhyRAMAddr4KRL = pPlugIn[BootDataImgAddrIndex] + IVT_OFFSET - ImgIVTOffset;

	pMxFunc->MxTrans.ExecutingAddr = pIVT2->ImageStartAddr;
	DWORD PlugInAddr = pIVT->ImageStartAddr;
	PlugInDataOffset = pIVT->ImageStartAddr - pIVT->SelfAddr;

	if (!TransData(PlugInAddr, 0x1000, (PUCHAR)((DWORD)pIVT + PlugInDataOffset)))
	{
		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X,0x%X) failed.\n"), \
			PlugInAddr, dataCount, pBuffer);
		return FALSE;
	}

	if(!Execute(PlugInAddr))
		return FALSE;

    return TRUE;
}

BOOL MxHidDevice::Download(UCHAR* pBuffer, ULONGLONG dataCount, PMxFunc pMxFunc)
{
    //if(pMxFunc->Task == TRANS)
	DWORD byteIndex, numBytesToWrite = 0;
	for ( byteIndex = 0; byteIndex < dataCount; byteIndex += numBytesToWrite )
	{
		// Get some data
		numBytesToWrite = min(MAX_SIZE_PER_DOWNLOAD_COMMAND, dataCount - byteIndex);

		if (!TransData(pMxFunc->MxTrans.PhyRAMAddr4KRL + byteIndex, numBytesToWrite, pBuffer + byteIndex))
		{
			TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X,0x%X) failed.\n"), \
				pMxFunc->MxTrans.PhyRAMAddr4KRL + byteIndex, numBytesToWrite, pBuffer + byteIndex);
			return FALSE;
		}
	}
    return TRUE;
}

BOOL MxHidDevice::Execute(UINT32 ImageStartAddr)
{
	int FlashHdrAddr;

	//transfer length of ROM_TRANSFER_SIZE is a must to ROM code.
	unsigned char FlashHdr[ROM_TRANSFER_SIZE] = { 0 };
	unsigned char Tempbuf[ROM_TRANSFER_SIZE] = { 0 };	

	PIvtHeader pIvtHeader = (PIvtHeader)FlashHdr;

	FlashHdrAddr = ImageStartAddr - sizeof(IvtHeader);
    //FlashHdrAddr = 0xF8006000;//IRAM

    //Read the data first
	if ( !ReadData(FlashHdrAddr, ROM_TRANSFER_SIZE, FlashHdr) )
	{
		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X) failed.\n"), \
            FlashHdrAddr, ROM_TRANSFER_SIZE, FlashHdr);
		return FALSE;
	}
	//Add IVT header to the image.
    //Clean the IVT header region
    ZeroMemory(FlashHdr, sizeof(IvtHeader));
    
    //Fill IVT header parameter
	pIvtHeader->IvtBarker = IVT_BARKER_HEADER;
	pIvtHeader->ImageStartAddr = ImageStartAddr;
	pIvtHeader->SelfAddr = FlashHdrAddr;
    
    //Send the IVT header to destiny address
	if ( !TransData(FlashHdrAddr, ROM_TRANSFER_SIZE, FlashHdr) )
	{
		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X) failed.\n"), \
            FlashHdrAddr, ROM_TRANSFER_SIZE, FlashHdr);
		return FALSE;
	}
    
    //Verify the data
	if ( !ReadData(FlashHdrAddr, ROM_TRANSFER_SIZE, Tempbuf) )
	{
		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X) failed.\n"), \
            FlashHdrAddr, ROM_TRANSFER_SIZE, FlashHdr);
		return FALSE;
	}

    if(memcmp(FlashHdr, Tempbuf, ROM_TRANSFER_SIZE)!= 0 )
	{
		TRACE(_T("DownloadImage(): TransData(0x%X, 0x%X, 0x%X) failed.\n"), \
            FlashHdrAddr, ROM_TRANSFER_SIZE, FlashHdr);
		return FALSE;
	}

    if( !Jump(FlashHdrAddr))
	{
        TRACE(_T("DownloadImage(): Failed to jump to RAM address: 0x%x.\n"), FlashHdrAddr);
		return FALSE;
	}

	return TRUE;
}

BOOL MxHidDevice::ReadData(UINT address, UINT byteCount, unsigned char * pBuf)
{
    SDPCmd SDPCmd;

    SDPCmd.command = ROM_KERNEL_CMD_RD_MEM;
    SDPCmd.dataCount = byteCount;
    SDPCmd.format = 32;
    SDPCmd.data = 0;
    SDPCmd.address = address;

	if(!SendCmd(&SDPCmd))
		return FALSE;

	if(!GetHABType())
		return FALSE;

    UINT MaxHidTransSize = m_Capabilities.InputReportByteLength -1;
    
    while(byteCount > 0)
    {
        UINT TransSize = (byteCount > MaxHidTransSize) ? MaxHidTransSize : byteCount;

        memset((UCHAR *)m_pReadReport, 0, m_Capabilities.InputReportByteLength);

        if ( Read( (UCHAR *)m_pReadReport, m_Capabilities.InputReportByteLength )  != ERROR_SUCCESS)
        {
            return FALSE;
        }

        memcpy(pBuf, m_pReadReport->Payload, TransSize);
        pBuf += TransSize;

        byteCount -= TransSize;
        //TRACE("Transfer Size: %d\n", TransSize);
    }

	return TRUE;
}


BOOL MxHidDevice::TransData(UINT address, UINT byteCount, const unsigned char * pBuf)
{
    SDPCmd SDPCmd;

    SDPCmd.command = ROM_KERNEL_CMD_WR_FILE;
    SDPCmd.dataCount = byteCount;
    SDPCmd.format = 0;
    SDPCmd.data = 0;
    SDPCmd.address = address;

	if(!SendCmd(&SDPCmd))
		return FALSE;
    
    Sleep(10);

    UINT MaxHidTransSize = m_Capabilities.OutputReportByteLength -1;
    UINT TransSize;
    
    while(byteCount > 0)
    {
        TransSize = (byteCount > MaxHidTransSize) ? MaxHidTransSize : byteCount;

		if(!SendData(pBuf, TransSize))
			return FALSE;

        byteCount -= TransSize;
        pBuf += TransSize;
        //TRACE("Transfer Size: %d\n", MaxHidTransSize);
    }

    //below function should be invoked for mx50
	if ( !GetCmdAck(ROM_STATUS_ACK) )
	{
		return FALSE;
	}

	return TRUE;
}

BOOL MxHidDevice::Jump(UINT RAMAddress)
{
    SDPCmd SDPCmd;

    SDPCmd.command = ROM_KERNEL_CMD_JUMP_ADDR;
    SDPCmd.dataCount = 0;
    SDPCmd.format = 0;
    SDPCmd.data = 0;
    SDPCmd.address = RAMAddress;

	if(!SendCmd(&SDPCmd))
		return FALSE;


	if(!GetHABType())
		return FALSE;


	//TRACE("*********Jump to Ramkernel successfully!**********\r\n");
	return TRUE;
}