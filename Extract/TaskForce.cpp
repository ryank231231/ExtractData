#include "stdafx.h"
#include "Arc/LZSS.h"
#include "../Image.h"
#include "../Image/Tga.h"
#include "TaskForce.h"

/// Mounting
bool CTaskForce::Mount(CArcFile* pclArc)
{
	if (MountDat(pclArc))
		return true;

	if (MountTlz(pclArc))
		return true;

	if (MountBma(pclArc))
		return true;

	return false;
}

/// dat mounting
bool CTaskForce::MountDat(CArcFile* pclArc)
{
	if (pclArc->GetArcExten() != _T(".dat"))
		return false;

	if (memcmp(pclArc->GetHed(), "tskforce", 8) != 0)
		return false;

	pclArc->SeekHed(8);

	// Get file count
	DWORD dwFiles;
	pclArc->Read(&dwFiles, 4);

	// Get index
	YCMemory<SFileEntry> clmIndex(dwFiles);
	pclArc->Read(&clmIndex[0], (sizeof(SFileEntry)* dwFiles));

	// Get file information
	DWORD dwIndexPtr = 0;

	for (DWORD i = 0; i < dwFiles; i++)
	{
		SFileInfo stFileInfo;
		stFileInfo.name = clmIndex[i].szFileName;
		stFileInfo.sizeCmp = clmIndex[i].dwCompressedSize;
		stFileInfo.sizeOrg = clmIndex[i].dwOriginalSize;
		stFileInfo.start = clmIndex[i].dwOffset;
		stFileInfo.end = stFileInfo.start + stFileInfo.sizeCmp;

		if (stFileInfo.sizeCmp != stFileInfo.sizeOrg)
		{
			stFileInfo.format = _T("LZ");
		}

		pclArc->AddFileInfo(stFileInfo);

		dwIndexPtr += sizeof(SFileEntry);
	}

	return true;
}

/// tlz mounting
bool CTaskForce::MountTlz(CArcFile* pclArc)
{
	if ((pclArc->GetArcExten() != _T(".tsk")) && (pclArc->GetArcExten() != _T(".tfz")))
		return false;

	if (memcmp(pclArc->GetHed(), "tlz", 3) != 0)
		return false;

	return pclArc->Mount();
}

/// bma mounting
bool CTaskForce::MountBma(CArcFile* pclArc)
{
	if (pclArc->GetArcExten() != _T(".tsz"))
		return false;

	if (memcmp(pclArc->GetHed(), "bma", 3) != 0)
		return false;

	return pclArc->Mount();
}

/// Decoding
bool CTaskForce::Decode(CArcFile* pclArc)
{
	if (DecodeTlz(pclArc))
		return true;

	if (DecodeBma(pclArc))
		return true;

	if (DecodeTGA(pclArc))
		return true;

	return false;
}

/// tlz decoding
bool CTaskForce::DecodeTlz(CArcFile* pclArc)
{
	const SFileInfo* file_info = pclArc->GetOpenFileInfo();
	if ((file_info->name.GetFileExt() != _T(".tsk")) && (file_info->name.GetFileExt() != _T(".tfz")))
		return false;

	// Read header
	BYTE abtHeader[24];
	pclArc->Read(abtHeader, sizeof(abtHeader));
	if (memcmp(abtHeader, "tlz", 3) != 0)
	{
		pclArc->SeekHed(file_info->start);
		return false;
	}

	// Get file information
	DWORD dwDstSize = *(DWORD*)&abtHeader[16];
	DWORD dwSrcSize = *(DWORD*)&abtHeader[20];

	// Read compressed data
	YCMemory<BYTE> clmSrc(dwSrcSize);
	pclArc->Read(&clmSrc[0], dwSrcSize);

	// Buffer allocation for decompression
	YCMemory<BYTE> clmDst(dwDstSize);

	// LZSS Decompression
	CLZSS clLZSS;
	clLZSS.Decomp(&clmDst[0], dwDstSize, &clmSrc[0], dwSrcSize, 4096, 4078, 3);

	// Output
	pclArc->OpenFile();
	pclArc->WriteFile(&clmDst[0], dwDstSize, dwSrcSize);
	pclArc->CloseFile();

	return true;
}

/// BMA decoding
bool CTaskForce::DecodeBma(CArcFile* pclArc)
{
	const SFileInfo* file_info = pclArc->GetOpenFileInfo();
	if (file_info->name.GetFileExt() != _T(".tsz"))
		return false;

	// Read header
	BYTE abtHeader[24];
	pclArc->Read(abtHeader, sizeof(abtHeader));
	if (memcmp(abtHeader, "bma", 3) != 0)
	{
		pclArc->SeekHed(file_info->start);
		return false;
	}

	// Get file information
	long  lWidth = *(long*)&abtHeader[4];
	long  lHeight = *(long*)&abtHeader[8];
	DWORD dwDstSize = *(DWORD*)&abtHeader[16];
	DWORD dwSrcSize = *(DWORD*)&abtHeader[20];

	// Read compressed data
	YCMemory<BYTE> clmSrc(dwSrcSize);
	pclArc->Read(&clmSrc[0], dwSrcSize);

	// Buffer allocation for decompression
	YCMemory<BYTE> clmDst(dwDstSize);

	// LZSS Decoding
	CLZSS clLZSS;
	clLZSS.Decomp(&clmDst[0], dwDstSize, &clmSrc[0], dwSrcSize, 4096, 4078, 3);

	// Output
	CImage clImage;
	clImage.Init(pclArc, lWidth, lHeight, 32);
	clImage.WriteReverse(&clmDst[0], dwDstSize);
	clImage.Close();

	return true;
}

/// TGA Decoding
bool CTaskForce::DecodeTGA(CArcFile* pclArc)
{
	const SFileInfo* file_info = pclArc->GetOpenFileInfo();
	if (file_info->name.GetFileExt() != _T(".tga"))
		return false;

	// Read data
	DWORD          dwSrcSize = file_info->sizeCmp;
	YCMemory<BYTE> clmSrc(dwSrcSize);
	pclArc->Read(&clmSrc[0], dwSrcSize);
	if (file_info->format == _T("LZ"))
	{
		// Is compressed

		DWORD          dwDstSize = file_info->sizeOrg;
		YCMemory<BYTE> clmDst(dwDstSize);

		// LZSS Decompression
		CLZSS clLZSS;
		clLZSS.Decomp(&clmDst[0], dwDstSize, &clmSrc[0], dwSrcSize, 4096, 4078, 3);

		// Output
		CTga clTGA;
		clTGA.Decode(pclArc, &clmDst[0], dwDstSize);
	}
	else
	{
		// Uncompressed
		CTga clTGA;
		clTGA.Decode(pclArc, &clmSrc[0], dwSrcSize);
	}

	return true;
}
