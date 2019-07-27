//=========== (C) Copyright 1999 Valve, L.L.C. All rights reserved. ===========
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================

#include "stdafx.h"
#include "worldcraft.h"
#include "OptionProperties.h"
#include "OPTTextures.h"
#include "Options.h"
#include <shlobj.h>


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// COPTTextures property page

IMPLEMENT_DYNCREATE(COPTTextures, CPropertyPage)

COPTTextures::COPTTextures() : CPropertyPage(COPTTextures::IDD)
{
	//{{AFX_DATA_INIT(COPTTextures)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT

	m_pMaterialConfig = NULL;
	m_bDeleted = FALSE;
}

COPTTextures::~COPTTextures()
{
	// detach the material exclusion list box
	m_MaterialExclude.Detach();
}

void COPTTextures::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(COPTTextures)
	DDX_Control(pDX, IDC_TEXTUREFILES, m_TextureFiles);
	DDX_Control(pDX, IDC_BRIGHTNESS, m_cBrightness);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(COPTTextures, CPropertyPage)
	//{{AFX_MSG_MAP(COPTTextures)
	ON_BN_CLICKED(IDC_EXTRACT, OnExtract)
	ON_BN_CLICKED(IDC_ADDTEXFILE, OnAddtexfile)
	ON_BN_CLICKED(IDC_REMOVETEXFILE, OnRemovetexfile)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_ADDTEXFILE2, OnAddtexfile2)

	ON_BN_CLICKED( ID_MATERIALEXCLUDE_ADD, OnMaterialExcludeAdd )
	ON_BN_CLICKED( ID_MATERIALEXCLUDE_REM, OnMaterialExcludeRemove )
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// COPTTextures message handlers

BOOL COPTTextures::OnInitDialog() 
{
	CPropertyPage::OnInitDialog();
	
	// load texture file list with options
	int i;
	for(i = 0; i < Options.textures.nTextureFiles; i++)
	{
		m_TextureFiles.AddString(Options.textures.TextureFiles[i]);
	}

	// set brightness control & values
	m_cBrightness.SetRange(1, 50); // 10 is default
	m_cBrightness.SetPos(int(Options.textures.fBrightness * 10));

	// attach the material exclusion list box
	m_MaterialExclude.Attach( GetDlgItem( ID_MATERIALEXCLUDE_LIST )->m_hWnd );

	return TRUE;
}


void COPTTextures::OnExtract() 
{
	// redo listbox content
	m_TextureFiles.ResetContent();
	for(int i = 0; i < Options.textures.nTextureFiles; i++)
	{
		m_TextureFiles.AddString(Options.textures.TextureFiles[i]);
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : 
//-----------------------------------------------------------------------------
void COPTTextures::OnAddtexfile(void)
{
	static char szInitialDir[MAX_PATH] = "\0";

	CFileDialog dlg(TRUE, "wad", NULL, OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST, "Texture files (*.wad;*.pak)|*.wad; *.pak||");

	if (szInitialDir[0] == '\0')
	{
		POSITION pos;
		APP()->GetFirstSearchDir(SEARCHDIR_WADS, szInitialDir, pos);
	}

	dlg.m_ofn.lpstrInitialDir = szInitialDir;

	if (dlg.DoModal() != IDOK)
	{
		return;
	}

	//
	// Get all the filenames from the open file dialog.
	//
	POSITION pos = dlg.GetStartPosition();
	CString str;
	while (pos != NULL)
	{
		str = dlg.GetNextPathName(pos);
		str.MakeLower();
		m_TextureFiles.AddString(str);
		SetModified();
	}

	//
	// Use this directory as the default directory for the next time.
	//
	int nBackslash = str.ReverseFind('\\');
	if (nBackslash != -1)
	{
		lstrcpyn(szInitialDir, str, nBackslash + 1);
	}
}


void COPTTextures::OnRemovetexfile() 
{
	int i = m_TextureFiles.GetCount();

	for (i--; i >= 0; i--)
	{
		if (m_TextureFiles.GetSel(i))
			m_TextureFiles.DeleteString(i);
	}

	m_bDeleted = TRUE;
}

void COPTTextures::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) 
{
	if(pScrollBar == (CScrollBar*) &m_cBrightness)
		SetModified();
	
	CPropertyPage::OnHScroll(nSBCode, nPos, pScrollBar);
}

BOOL COPTTextures::OnApply() 
{
	Options.textures.fBrightness = (float)m_cBrightness.GetPos() / 10.0f;

	int iSize = m_TextureFiles.GetCount();
	CString str;
	Options.textures.nTextureFiles = iSize;
	Options.textures.TextureFiles.RemoveAll();
	for(int i = 0; i < iSize; i++)
	{
		m_TextureFiles.GetText(i, str);
		Options.textures.TextureFiles.Add(str);
	}
	
	if(m_bDeleted)
	{
		// inform them that deleted files will only be reapplied after
		// they reload Worldcraft
		MessageBox("You have removed some texture files from the list.\n"
			"These texture files will continue to be used during this\n"
			"session, but will not be loaded the next time you run\n"
			"Worldcraft.", "A Quick Note");
	}

	Options.PerformChanges(COptions::secTextures);

	return CPropertyPage::OnApply();
}

void GetDirectory(char *pDest, const char *pLongName)
{
	strcpy(pDest, pLongName);
	int i = strlen(pDest);
	while (pLongName[i] != '\\' && pLongName[i] != '/' && i > 0)
		i--;

	if (i <= 0)
		i = 0;
	
	pDest[i] = 0;

	return;
}


void COPTTextures::OnAddtexfile2() 
{
	BROWSEINFO bi;
	char szDisplayName[MAX_PATH];

	bi.hwndOwner = m_hWnd;
	bi.pidlRoot = NULL;
	bi.pszDisplayName = szDisplayName;
	bi.lpszTitle = "Select your Quake II directory.";
	bi.ulFlags = BIF_RETURNONLYFSDIRS;
	bi.lpfn = NULL;
	bi.lParam = 0;
	
	LPITEMIDLIST pidlNew = SHBrowseForFolder(&bi);

	if(pidlNew)
	{

		// get path from the id list
		char szPathName[MAX_PATH];
		SHGetPathFromIDList(pidlNew, szPathName);
		
		
		if (AfxMessageBox("Add all subdirectories as separate Texture Groups?", MB_YESNO) == IDYES)
		//if (!_strcmpi("\\textures", &szPathName[strlen(szPathName) - strlen("\\textures")]))
		{
			char szNewPath[MAX_PATH];
			strcpy(szNewPath, szPathName);
			strcat(szNewPath, "\\*.*");
			WIN32_FIND_DATA FindData;
			HANDLE hFile = FindFirstFile(szNewPath, &FindData);

			if (hFile != INVALID_HANDLE_VALUE) do
			{
				if ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						&&(FindData.cFileName[0] != '.'))
				{
					sprintf(szNewPath, "%s\\%s", szPathName, FindData.cFileName);
					_strlwr(szNewPath);
					if (m_TextureFiles.FindStringExact(-1, szNewPath) == CB_ERR)
						m_TextureFiles.AddString(szNewPath);
				}
			} while (FindNextFile(hFile, &FindData));

		}
		else
		{
			_strlwr(szPathName);
			if (m_TextureFiles.FindStringExact(-1, szPathName) == CB_ERR)
				m_TextureFiles.AddString(_strlwr(szPathName));
		}
		SetModified();

		// free the previous return value from SHBrowseForFolder
		CoTaskMemFree(pidlNew);

	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pszTitle - 
//			*pszDirectory - 
// Output : Returns TRUE on success, FALSE on failure.
//-----------------------------------------------------------------------------
BOOL COPTTextures::BrowseForFolder( char *pszTitle, char *pszDirectory )
{
	char szTmp[MAX_PATH];

	BROWSEINFO bi;
	memset( &bi, 0, sizeof( bi ) );
	bi.hwndOwner = m_hWnd;
	bi.pszDisplayName = szTmp;
	bi.lpszTitle = pszTitle;
	bi.ulFlags = BIF_RETURNONLYFSDIRS;

	LPITEMIDLIST idl = SHBrowseForFolder( &bi );

	if( idl == NULL )
		return FALSE;

	SHGetPathFromIDList( idl, pszDirectory );
	CoTaskMemFree( idl );

	return TRUE;
}


//-----------------------------------------------------------------------------
// Purpose: intercept this call and update the "material" configuration pointer
//          if it is out of sync with the game configuration "parent" (on the
//          "Options Configs" page)
//  Output: returns TRUE on success, FALSE on failure
//-----------------------------------------------------------------------------
BOOL COPTTextures::OnSetActive( void )
{
	//
	// get the current game configuration from the "Options Configs" page
	//
	COptionProperties *pOptProps = ( COptionProperties* )GetParent();
	if( !pOptProps )
		return FALSE;

	CGameConfig *pConfig = pOptProps->Configs.GetCurrentConfig();
	if( !pConfig )
		return FALSE;

	// compare for a change
	if( m_pMaterialConfig != pConfig )
	{
		// update the materail config
		m_pMaterialConfig = pConfig;

		// update the all config specific material data on this page
		MaterialExcludeUpdate();

		// update the last materail config
		m_pMaterialConfig = pConfig;
	}

	return CPropertyPage::OnSetActive();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COPTTextures::MaterialExcludeUpdate( void )
{
	// remove all of the data in the current "material exclude" list box
	m_MaterialExclude.ResetContent();

	//
	// add the data from the current material config
	//
	for( int i = 0; i < m_pMaterialConfig->m_MaterialExcludeCount; i++ )
	{
		int result = m_MaterialExclude.AddString( m_pMaterialConfig->m_szMaterialExcludeDirs[i] );
		if( ( result == LB_ERR ) || ( result == LB_ERRSPACE ) )
			return;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void StripOffMaterialDirectory( const char *pszDirectoryName, char *pszName )
{
	// clear name
	pszName[0] = '\0';

	// create a lower case version of the string
	char *pLowerCase = __strlwr( _strdup( pszDirectoryName ) );
	char *pAtMat = strstr( pLowerCase, "materials" );
	if( !pAtMat )
		return;

	// move the pointer ahead 10 spaces = "materials\"
	pAtMat += 10;

	// copy the rest to the name string
	strcpy( pszName, pAtMat );

	// free duplicated string's memory
	free( pLowerCase );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COPTTextures::OnMaterialExcludeAdd( void )
{
	//
	// get the directory path to exclude
	//
	char szTmp[MAX_PATH];
	if( !BrowseForFolder( "Select Game Executable Directory", szTmp ) )
		return;

	// strip off the material directory
	char szSubDirName[MAX_PATH];
	StripOffMaterialDirectory( szTmp, &szSubDirName[0] );
	if( szSubDirName[0] == '\0' )
		return;

	//
	// add directory to list box
	//
	int result = m_MaterialExclude.AddString( szSubDirName );
	if( ( result == LB_ERR ) || ( result == LB_ERRSPACE ) )
		return;
	
	//
	// add name of directory to the global exlusion list
	//
	int ndx = m_pMaterialConfig->m_MaterialExcludeCount;
	if( ndx >= MAX_DIRECTORY_SIZE )
		return;


	strcpy( m_pMaterialConfig->m_szMaterialExcludeDirs[ndx], szSubDirName );
	m_pMaterialConfig->m_MaterialExcludeCount++;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COPTTextures::OnMaterialExcludeRemove( void )
{
	//
	// get the directory to remove
	//
	int ndxSel = m_MaterialExclude.GetCurSel();
	if( ndxSel == LB_ERR )
		return;

	char szTmp[MAX_PATH];
	m_MaterialExclude.GetText( ndxSel, &szTmp[0] );

	//
	// remove directory from the list box
	//
	int result = m_MaterialExclude.DeleteString( ndxSel );
	if( result == LB_ERR )
		return;

	//
	// remove the name of the directory from the global exlusion list
	//
	for( int i = 0; i < m_pMaterialConfig->m_MaterialExcludeCount; i++ )
	{
		if( !strcmp( szTmp, m_pMaterialConfig->m_szMaterialExcludeDirs[i] ) )
		{
			// remove the directory
			if( i != ( m_pMaterialConfig->m_MaterialExcludeCount - 1 ) )
			{
				strcpy( m_pMaterialConfig->m_szMaterialExcludeDirs[i],
					    m_pMaterialConfig->m_szMaterialExcludeDirs[m_pMaterialConfig->m_MaterialExcludeCount-1] );
			}

			// decrement count
			m_pMaterialConfig->m_MaterialExcludeCount--;
		}
	}
}
