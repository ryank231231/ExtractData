#include	"stdafx.h"
#include	"../Image.h"
#include	"AOS.h"

//////////////////////////////////////////////////////////////////////////////////////////
//	Mount

BOOL	CAOS::Mount(
	CArcFile*			pclArc							// Archive
	)
{
	if( pclArc->GetArcExten() != _T(".aos") )
	{
		return	FALSE;
	}

	// Unknown 4 bytes

	pclArc->SeekHed( 4 );

	// Get offset

	DWORD				dwOffset;

	pclArc->Read( &dwOffset, 4 );

	// Get index size

	DWORD				dwIndexSize;

	pclArc->Read( &dwIndexSize, 4 );

	// Get archive filename

	char				szArchiveName[261];

	pclArc->Read( szArchiveName, 261 );

	if( pclArc->GetArcName() != szArchiveName )
	{
		// Archive filename is different

		pclArc->SeekHed();
		return	FALSE;
	}

	// Get index

	YCMemory<BYTE>		clmIndex( dwIndexSize );

	pclArc->Read( &clmIndex[0], dwIndexSize );

	// Get file info

	for( DWORD i = 0 ; i < dwIndexSize ; i += 40 )
	{
		SFileInfo			stFileInfo;

		stFileInfo.name.Copy( (char*) &clmIndex[i], 32 );
		stFileInfo.start = *(DWORD*) &clmIndex[i + 32] + dwOffset;
		stFileInfo.sizeCmp = *(DWORD*) &clmIndex[i + 36];
		stFileInfo.sizeOrg = stFileInfo.sizeCmp;
		stFileInfo.end = stFileInfo.start + stFileInfo.sizeCmp;

		pclArc->AddFileInfo( stFileInfo );
	}

	return	TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	Decode

BOOL	CAOS::Decode(
	CArcFile*			pclArc							// Archive
	)
{
	if( pclArc->GetArcExten() != _T(".aos") )
	{
		return	FALSE;
	}

	BOOL				bReturn = FALSE;
	SFileInfo*			pstFileInfo = pclArc->GetOpenFileInfo();

	if( pstFileInfo->format == _T("ABM") )
	{
		// ABM

		bReturn = DecodeABM( pclArc );
	}
	else if( pstFileInfo->format == _T("MSK") )
	{
		// Image mask

		bReturn = DecodeMask( pclArc );
	}
	else if( pstFileInfo->format == _T("SCR") )
	{
		// Script

		bReturn = DecodeScript( pclArc );
	}

	return	bReturn;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	ABM Decoding

BOOL	CAOS::DecodeABM(
	CArcFile*			pclArc							// Archive
	)
{
	SFileInfo*			pstFileInfo = pclArc->GetOpenFileInfo();

	// Read data

	DWORD				dwSrcSize = pstFileInfo->sizeCmp;

	YCMemory<BYTE>		clmSrc( dwSrcSize );

	pclArc->Read( &clmSrc[0], dwSrcSize );

	// Get bitmap header

	BITMAPFILEHEADER*	pstbfhSrc = (BITMAPFILEHEADER*) &clmSrc[0];
	BITMAPINFOHEADER*	pstbihSrc = (BITMAPINFOHEADER*) &clmSrc[14];

	CImage				clImage;
	YCMemory<BYTE>		clmDst;
	YCString			clsLastName;
	DWORD				dwDstSize;
	DWORD				dwFrames;
	DWORD				dwOffsetToData;
	DWORD				dwSrcPtr = 0;
	DWORD				dwDstPtr = 0;

	switch( pstbihSrc->biBitCount )
	{
	case	1:
		// Multi-frame

		dwFrames = *(DWORD*) &clmSrc[58];
		dwOffsetToData = *(DWORD*) &clmSrc[66];

		dwDstSize = (pstbihSrc->biWidth * pstbihSrc->biHeight * 4);

		clmDst.resize( dwDstSize );

		if( dwFrames >= 2 )
		{
			// Multiple files

			clsLastName.Format( _T("_000") );
		}

		// Decompression

		dwSrcPtr = dwOffsetToData;

		for( DWORD i = 0 ; i < dwDstSize ; i += 4 )
		{
			clmDst[i + 0] = clmSrc[dwSrcPtr++];
			clmDst[i + 1] = clmSrc[dwSrcPtr++];
			clmDst[i + 2] = clmSrc[dwSrcPtr++];
			clmDst[i + 3] = 0xFF;
		}

		// Output

		clImage.Init( pclArc, pstbihSrc->biWidth, pstbihSrc->biHeight, 32, NULL, 0, clsLastName );
		clImage.WriteReverse( &clmDst[0], dwDstSize );
		clImage.Close();

		// 

		for( DWORD i = 1 ; i < dwFrames ; i++ )
		{
			DWORD				dwOffsetToFrame = *(DWORD*) &clmSrc[70 + (i - 1) * 4];

			clsLastName.Format( _T("_%03d"), i );

			// Decompression

			ZeroMemory( &clmDst[0], dwDstSize );

			DecompABM( &clmDst[0], dwDstSize, &clmSrc[dwOffsetToFrame], (dwSrcSize - dwOffsetToFrame) );

			// Output

			clImage.Init( pclArc, pstbihSrc->biWidth, pstbihSrc->biHeight, 32, NULL, 0, clsLastName );
			clImage.WriteReverse( &clmDst[0], dwDstSize, FALSE );
			clImage.Close();
		}

		break;

	case	32:
		// 32bit

		dwDstSize = (pstbihSrc->biWidth * pstbihSrc->biHeight * 4);

		clmDst.resize( dwDstSize );

		// Decompression

		DecompABM( &clmDst[0], dwDstSize, &clmSrc[54], (dwSrcSize - 54) );

		// Output

		clImage.Init( pclArc, pstbihSrc->biWidth, pstbihSrc->biHeight, pstbihSrc->biBitCount );
		clImage.WriteReverse( &clmDst[0], dwDstSize );
		clImage.Close();

		break;

	default:
		// Other

		pclArc->OpenFile();
		pclArc->WriteFile( &clmSrc[0], dwSrcSize );
		pclArc->CloseFile();
	}

	return	TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	Decode Image Mask

BOOL	CAOS::DecodeMask(
	CArcFile*			pclArc							// Archive
	)
{
	SFileInfo*			pstFileInfo = pclArc->GetOpenFileInfo();

	// Read Data

	DWORD				dwSrcSize = pstFileInfo->sizeCmp;

	YCMemory<BYTE>		clmSrc( dwSrcSize );

	pclArc->Read( &clmSrc[0], dwSrcSize );

	// Output

	CImage				clImage;

	clImage.Init( pclArc, &clmSrc[0] );
	clImage.Write( dwSrcSize );
	clImage.Close();

	return	TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	Decode Script

BOOL	CAOS::DecodeScript(
	CArcFile*			pclArc							// Archive
	)
{
	SFileInfo*			pstFileInfo = pclArc->GetOpenFileInfo();

	// Read compressed data

	DWORD				dwSrcSize = pstFileInfo->sizeCmp;

	YCMemory<BYTE>		clmSrc( dwSrcSize );

	pclArc->Read( &clmSrc[0], dwSrcSize );

	// Buffer allocation for extraction

	DWORD				dwDstSize = *(DWORD*) &clmSrc[0];

	YCMemory<BYTE>		clmDst( dwDstSize );

	// Decompression

	DecompScript( &clmDst[0], dwDstSize, &clmSrc[4], (dwSrcSize - 4) );

	// Output

	pclArc->OpenScriptFile();
	pclArc->WriteFile( &clmDst[0], dwDstSize, dwSrcSize );
	pclArc->CloseFile();

	return	TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	ABM Decompression

BOOL	CAOS::DecompABM(
	BYTE*				pbtDst,							// Destination
	DWORD				dwDstSize,						// Destination size
	const BYTE*			pbtSrc,							// Compressed data
	DWORD				dwSrcSize						// Compressed data size
	)
{
	DWORD				dwSrcPtr = 0;
	DWORD				dwDstPtr = 0;

	BYTE				btCurrentSrc;
	DWORD				dwAlphaCount = 0;

	while( dwDstPtr < dwDstSize )
	{
		DWORD				dwLength;

		btCurrentSrc = pbtSrc[dwSrcPtr++];

		switch( btCurrentSrc )
		{
		case	0:
			// Is 0x00

			dwLength = pbtSrc[dwSrcPtr++];

			for( DWORD i = 0 ; i < dwLength ; i++ )
			{
				pbtDst[dwDstPtr++] = 0;

				dwAlphaCount++;

				if( dwAlphaCount == 3 )
				{
					pbtDst[dwDstPtr++] = 0;

					dwAlphaCount = 0;
				}
			}

			break;

		case	255:
			// Is 0xFF

			dwLength = pbtSrc[dwSrcPtr++];

			for( DWORD i = 0 ; i < dwLength ; i++ )
			{
				pbtDst[dwDstPtr++] = pbtSrc[dwSrcPtr++];

				dwAlphaCount++;

				if( dwAlphaCount == 3 )
				{
					pbtDst[dwDstPtr++] = 0xFF;

					dwAlphaCount = 0;
				}
			}

			break;

		default:
			// Other

			pbtDst[dwDstPtr++] = pbtSrc[dwSrcPtr++];

			dwAlphaCount++;

			if( dwAlphaCount == 3 )
			{
				pbtDst[dwDstPtr++] = btCurrentSrc;

				dwAlphaCount = 0;
			}

		}
	}

	return	TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	Decompress Script

BOOL	CAOS::DecompScript(
	BYTE*				pbtDst,							// Destination
	DWORD				dwDstSize,						// Destination Size
	const BYTE*			pbtSrc,							// Compressed Data
	DWORD				dwSrcSize						// Compressed Data Size
	)
{
	// Construct huffman table

	DWORD				adwTableOfBit0[511];
	DWORD				adwTableOfBit1[511];
	DWORD				dwSrcPtr = 0;
	DWORD				dwTablePtr = 256;
	DWORD				dwCurrentSrc = 0;
	DWORD				dwBitShift = 0;

	ZeroMemory( adwTableOfBit0, sizeof(adwTableOfBit0) );
	ZeroMemory( adwTableOfBit1, sizeof(adwTableOfBit1) );

	dwTablePtr = CreateHuffmanTable( adwTableOfBit0, adwTableOfBit1, pbtSrc, &dwSrcPtr, &dwTablePtr, &dwCurrentSrc, &dwBitShift );

	// Decompress

	DecompHuffman( pbtDst, dwDstSize, adwTableOfBit0, adwTableOfBit1, &pbtSrc[dwSrcPtr], dwTablePtr, dwCurrentSrc, dwBitShift );

	return	TRUE;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	Construct Huffman Table

DWORD	CAOS::CreateHuffmanTable(
	DWORD*				pdwTableOfBit0,					// bit0 Table
	DWORD*				pdwTableOfBit1,					// bit1 Table
	const BYTE*			pbtSrc,							// Compressed Data
	DWORD*				pdwSrcPtr,						// Compressed Data Pointer
	DWORD*				pdwTablePtr,					// Table Pointer
	DWORD*				pdwCurrentSrc,					// Current Data
	DWORD*				pdwBitShift						// Bit shift
	)
{
	DWORD				dwReturn = 0;
	DWORD				dwTablePtr;

	if( *pdwBitShift == 0 )
	{
		// Read 8-bits

		*pdwCurrentSrc = pbtSrc[(*pdwSrcPtr)++];
		*pdwBitShift = 8;
	}

	(*pdwBitShift) -= 1;

	if( (*pdwCurrentSrc >> *pdwBitShift) & 1 )
	{
		// Bit 1

		dwTablePtr = *pdwTablePtr;

		(*pdwTablePtr) += 1;

		if( dwTablePtr < 511 )
		{
			pdwTableOfBit0[dwTablePtr] = CreateHuffmanTable( pdwTableOfBit0, pdwTableOfBit1, pbtSrc, pdwSrcPtr, pdwTablePtr, pdwCurrentSrc, pdwBitShift );
			pdwTableOfBit1[dwTablePtr] = CreateHuffmanTable( pdwTableOfBit0, pdwTableOfBit1, pbtSrc, pdwSrcPtr, pdwTablePtr, pdwCurrentSrc, pdwBitShift );

			dwReturn = dwTablePtr;
		}
	}
	else
	{
		// Bit 0

		DWORD				dwBitShiftTemp = 8;
		DWORD				dwResult = 0;

		while( dwBitShiftTemp > *pdwBitShift )
		{
			DWORD				dwWork = ((1 << *pdwBitShift) - 1) & *pdwCurrentSrc;

			dwBitShiftTemp -= *pdwBitShift;

			*pdwCurrentSrc = pbtSrc[(*pdwSrcPtr)++];

			dwResult |= (dwWork << dwBitShiftTemp);

			*pdwBitShift = 8;
		}

		(*pdwBitShift) -= dwBitShiftTemp;

		DWORD				dwMask = (1 << dwBitShiftTemp) - 1;

		dwReturn = ((*pdwCurrentSrc >> *pdwBitShift) & dwMask) | dwResult;
	}

	return	dwReturn;
}

//////////////////////////////////////////////////////////////////////////////////////////
//	Huffman Decompression

BOOL	CAOS::DecompHuffman(
	BYTE*				pbtDst,							// Destination
	DWORD				dwDstSize,						// Destination Size
	const DWORD*		pdwTableOfBit0,					// bit0 Table
	const DWORD*		pdwTableOfBit1,					// bit1 Table
	const BYTE*			pbtSrc,							// Compressed Data
	DWORD				dwRoot,							// Table Position Reference
	DWORD				dwCurrentSrc,					// Current Data
	DWORD				dwBitShift						// Bit Shift
	)
{
	if( dwDstSize <= 0 )
	{
		return	FALSE;
	}

	DWORD				dwSrcPtr = 0;
	DWORD				dwDstPtr = 0;

	while( dwDstPtr < dwDstSize )
	{
		DWORD				dwTablePtr = dwRoot;

		while( dwTablePtr >= 256 )
		{
			if( dwBitShift == 0 )
			{
				// Read 8-bits

				dwCurrentSrc = pbtSrc[dwSrcPtr++];
				dwBitShift = 8;
			}

			dwBitShift -= 1;

			if( (dwCurrentSrc >> dwBitShift) & 1 )
			{
				// bit1

				dwTablePtr = pdwTableOfBit1[dwTablePtr];
			}
			else
			{
				// bit0

				dwTablePtr = pdwTableOfBit0[dwTablePtr];
			}
		}

		pbtDst[dwDstPtr++] = (BYTE) dwTablePtr;
	}

	return	TRUE;
}
