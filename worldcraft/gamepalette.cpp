//
// CGamePalette implementation
//

#include "stdafx.h"
#include "GamePalette.h"
#include <fstream.h>

#pragma warning(disable:4244)

CGamePalette::CGamePalette()
{
	fBrightness = 1.0;

	uPaletteBytes = sizeof(LOGPALETTE) + sizeof(PALETTEENTRY) * 256;

	// allocate memory
	pPalette = (LOGPALETTE*) malloc(uPaletteBytes);
	pOriginalPalette = (LOGPALETTE*) malloc(uPaletteBytes);

	memset(pPalette, 0, uPaletteBytes);
	memset(pOriginalPalette, 0, uPaletteBytes);

	if(!pPalette || !pOriginalPalette)
	{
		AfxMessageBox("I couldn't allocate memory for the palette.");
		PostQuitMessage(-1);
		return;
	}

	pPalette->palVersion = 0x300;
	pPalette->palNumEntries = 256;

	pOriginalPalette->palVersion = 0x300;
	pOriginalPalette->palNumEntries = 256;

	GDIPalette.CreatePalette(pPalette);
}

CGamePalette::~CGamePalette()
{
	if(pPalette && pOriginalPalette)
	{
		// free memory
		free(pPalette);
		free(pOriginalPalette);
	}
}

BOOL CGamePalette::Create(LPCTSTR pszFile)
{
	strFile = pszFile;

	if(GetFileAttributes(pszFile) == 0xffffffff)
		return FALSE;	// not exist

	// open file & read palette
	ifstream file(strFile, ios::binary | ios::nocreate);

	if(!file.is_open())
		return FALSE;

	for(int i = 0; i < 256; i++)
	{
		if(file.eof())
			break;

		pOriginalPalette->palPalEntry[i].peRed = file.get();
		pOriginalPalette->palPalEntry[i].peGreen = file.get();
		pOriginalPalette->palPalEntry[i].peBlue = file.get();
		pOriginalPalette->palPalEntry[i].peFlags = D3DRMPALETTE_READONLY |
			PC_NOCOLLAPSE;
	}

	file.close();

	if(i < 256)
		return FALSE;

	// copy  into working palette
	memcpy((void*) pPalette, (void*) pOriginalPalette, uPaletteBytes);
	GDIPalette.SetPaletteEntries(0, 256, pPalette->palPalEntry);

	return TRUE;
}

static BYTE fixbyte(float fValue)
{
	if(fValue > 255.0)
		fValue = 255.0;
	if(fValue < 0)
		fValue = 0;

	return BYTE(fValue);
}

void CGamePalette::SetBrightness(float fValue)
{
	if(fValue <= 0)
		return;

	fBrightness = fValue;

	// if fValue is 1.0, memcpy
	if(fValue == 1.0)
	{
		memcpy((void*) pPalette, (void*) pOriginalPalette, uPaletteBytes);
		GDIPalette.SetPaletteEntries(0, 256, pPalette->palPalEntry);
		return;
	}

	// copy original palette to new palette, scaling by fValue
	PALETTEENTRY * pOriginalEntry;
	PALETTEENTRY * pNewEntry;

	for(int i = 0; i < 256; i++)
	{
		pOriginalEntry = &pOriginalPalette->palPalEntry[i];
		pNewEntry = &pPalette->palPalEntry[i];

		pNewEntry->peRed = fixbyte(pOriginalEntry->peRed * fBrightness);
		pNewEntry->peGreen = fixbyte(pOriginalEntry->peGreen * fBrightness);
		pNewEntry->peBlue = fixbyte(pOriginalEntry->peBlue * fBrightness);
	}

	GDIPalette.SetPaletteEntries(0, 256, pPalette->palPalEntry);
}

float CGamePalette::GetBrightness()
{
	return fBrightness;
}