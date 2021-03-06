//========= Copyright � 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose: Responsible for drawing the scene
//
// $NoKeywords: $
//=============================================================================
#include "cbase.h"
#include "view.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "ivieweffects.h"
#include "iinput.h"
#include "model_types.h"
#include "clientsideeffects.h"
#include "particlemgr.h"
#include "viewrender.h"
#include "iclientmode.h"
#if defined( TF2_CLIENT_DLL )
	#include "ground_line.h"
#endif
#include "voice_status.h"
#include "glow_overlay.h"
#include "materialsystem/imesh.h"
#include "materialsystem/ITexture.h"
#include "materialsystem/IMaterial.h"
#include "materialsystem/IMaterialVar.h"
#include "DetailObjectSystem.h"
#include "C_ClientStats.h"	
#include "tier0/vprof.h"
#include "engine/IEngineTrace.h"
#include "engine/ivmodelinfo.h"
#include "view_scene.h"
#include "particles_ez.h"
#include "engine/IStaticPropMgr.h"

// VXP
#include "materialsystem/IMaterialSystemHardwareConfig.h"

// GR
#include "rendertexture.h"

extern void ComputeCameraVariables( const Vector &vecOrigin, const QAngle &vecAngles, 
	Vector *pVecForward, Vector *pVecRight, Vector *pVecUp, VMatrix *pMatCamInverse );

static Vector g_vecCurrentRenderOrigin(0,0,0);
static QAngle g_vecCurrentRenderAngles(0,0,0);
static Vector g_vecCurrentVForward(0,0,0), g_vecCurrentVRight(0,0,0), g_vecCurrentVUp(0,0,0);
static VMatrix g_matCurrentCamInverse;
static bool s_bCanAccessCurrentView = false;

static ConVar r_WaterDrawRefraction( "r_WaterDrawRefraction", "1", 0, "Enable water refraction" );
static ConVar r_WaterDrawReflection( "r_WaterDrawReflection", "1", 0, "Enable water reflection" );
static ConVar r_WaterEntReflection( "r_WaterEntReflection", "0", FCVAR_ARCHIVE, "Enable water entity reflection" );
static ConVar r_ForceWaterLeaf( "r_ForceWaterLeaf", "1", 0, "Enable for optimization to water - considers view in leaf under water for purposes of culling" );
static ConVar cl_copyframebuffertotexture( "cl_copyframebuffertotexture", "1" );
static ConVar cl_alwayscopyframebuffertotexture( "cl_alwayscopyframebuffertotexture", "1" );
static ConVar cl_maxrenderable_dist("cl_maxrenderable_dist", "3000", 0, "Max distance from the camera at which things will be rendered" );

static bool g_bWaterReflectionCull = false;
static float g_bWaterCullHeight = 0.0f;
static ConVar r_watercullheight( "r_watercullheight", "25" );
static ConVar r_watercullenable( "r_watercullenable", "0" );
static ConVar cl_drawmaterial( "cl_drawmaterial", "", 0, "Draw a particular material over the frame" );

// Matches the version in the engine
static ConVar r_drawtranslucentworld( "r_drawtranslucentworld", "1" );

static bool	g_bRenderingView = false;	// For debugging..

static ConVar	r_drawviewmodel( "r_drawviewmodel","1");
static ConVar	r_3dsky( "r_3dsky","1", 0, "Enable the rendering of 3d sky boxes" );
static ConVar	r_skybox( "r_skybox","1", 0, "Enable the rendering of sky boxes" );

static ConVar mat_showframebuffertexture( "mat_showframebuffertexture", "0" );
static ConVar cl_drawshadowtexture( "cl_drawshadowtexture", "0" );
  
static ConVar mat_showwatertextures( "mat_showwatertextures", "0" );
static ConVar mat_showcamerarendertarget( "mat_showcamerarendertarget", "0" );

static ConVar r_drawopaqueworld( "r_drawopaqueworld", "1" );
static ConVar r_drawtranslucentrenderables( "r_drawtranslucentrenderables", "1" );
static ConVar r_drawopaquerenderables( "r_drawopaquerenderables", "1" );

static ConVar mat_drawwater( "mat_drawwater", "1" );
static ConVar mat_hsv( "mat_hsv", "0" );
static ConVar mat_yuv( "mat_yuv", "0" );

// GR - HDR
static ConVar mat_bloom( "mat_bloom", "1" );

ConVar r_DoCovertTransitions("r_DoCovertTransitions", "1", FCVAR_NEVER_AS_STRING );				// internally used by game code and engine to choose when to allow LOD transitions.
ConVar r_TransitionSensitivity("r_TransitionSensitivity", "6", 0, "Controls when LODs are changed. Lower numbers cause more overt LOD transitions.");
ConVar cl_drawhud( "cl_drawhud","1", 0, "Enable the rendering of the hud" );


// (the engine owns this cvar).
ConVar mat_wireframe( "mat_wireframe", "0" );

CViewRender::CViewRender()
{
	m_AnglesHistoryCounter = 0;
	memset(m_AnglesHistory, 0, sizeof(m_AnglesHistory));
	m_flCheapWaterStartDistance = 0.0f;
	m_flCheapWaterEndDistance = 0.1f;
}


//-----------------------------------------------------------------------------
// Accessors to return the current view being rendered
//-----------------------------------------------------------------------------
const Vector &CurrentViewOrigin()
{
//	Assert( s_bCanAccessCurrentView ); // VXP: TODO when SmokeGrenade concommand called
	if ( !s_bCanAccessCurrentView )
		DevWarning( "CurrentViewOrigin: !s_bCanAccessCurrentView\n" );
	return g_vecCurrentRenderOrigin;
}

const QAngle &CurrentViewAngles()
{
	Assert( s_bCanAccessCurrentView );
	return g_vecCurrentRenderAngles;
}

const Vector &CurrentViewForward()
{
	Assert( s_bCanAccessCurrentView );
	return g_vecCurrentVForward;
}

const Vector &CurrentViewRight()
{
	Assert( s_bCanAccessCurrentView );
	return g_vecCurrentVRight;
}

const Vector &CurrentViewUp()
{
	Assert( s_bCanAccessCurrentView );
	return g_vecCurrentVUp;
}

const VMatrix &CurrentWorldToViewMatrix()
{
	Assert( s_bCanAccessCurrentView );
	return g_matCurrentCamInverse;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : allow - 
//-----------------------------------------------------------------------------
void AllowCurrentViewAccess( bool allow )
{
	s_bCanAccessCurrentView = allow;
}

void SetupCurrentView( const Vector &vecOrigin, const QAngle &angles )
{
	// Store off view origin and angles
	g_vecCurrentRenderOrigin = vecOrigin;
	g_vecCurrentRenderAngles = angles;

	// Compute the world->main camera transform
	ComputeCameraVariables( vecOrigin, angles, 
		&g_vecCurrentVForward, &g_vecCurrentVRight, &g_vecCurrentVUp, &g_matCurrentCamInverse );

	s_bCanAccessCurrentView = true;
}

void FinishCurrentView()
{
	s_bCanAccessCurrentView = false;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CViewRender::ShouldDrawEntities( void )
{
	if ( m_pDrawEntities && !m_pDrawEntities->GetInt() )
		return false;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Check all conditions which would prevent drawing the view model
// Input  : drawViewmodel - 
//			*viewmodel - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CViewRender::ShouldDrawViewModel( bool drawViewmodel )
{
	if ( !drawViewmodel )
		return false;

	if ( !r_drawviewmodel.GetBool() )
		return false;

	if ( input->CAM_IsThirdPerson() )
		return false;

	if ( !ShouldDrawEntities() )
		return false;

	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player && player->pl.deadflag )
		return false;

	if ( render->GetViewEntity() > engine->GetMaxClients() )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Actually draw the view model
// Input  : drawViewModel - 
//-----------------------------------------------------------------------------
void CViewRender::DrawViewModels( const CViewSetup &view, bool drawViewmodel )
{
	int i;

	VPROF( "CViewRender::DrawViewModel" );

	if ( !ShouldDrawViewModel( drawViewmodel ) )
		return;

	// Restore the matrices
	materials->MatrixMode( MATERIAL_PROJECTION );
	materials->PushMatrix();

	// Set up for drawing the view model
	render->SetProjectionMatrix( view.fovViewmodel, view.zNearViewmodel, view.zFarViewmodel );

	// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
	float			depthmin = 0, depthmax = 1;
	// Force clipped down range
	materials->DepthRange( depthmin, depthmin + 0.3*( depthmax - depthmin ) );
	
	CUtlVector< IClientRenderable * > viewModelList;

	ClientLeafSystem()->CollateRenderablesInViewModelRenderGroup( viewModelList );
	// Iterate over all leaves that were visible, and draw opaque things in them.
	int nViewModel = viewModelList.Count();

	for( i=0; i < nViewModel; ++i )
	{
		IClientUnknown *pUnk = viewModelList[i]->GetIClientUnknown();
		Assert( pUnk );
		C_BaseEntity *ent = pUnk->GetBaseEntity();
		Assert( ent );

		// Non-view models wanting to render in view model list...
		if ( ent->ShouldDraw() )
		{
			ent->DrawModel( STUDIO_RENDER | STUDIO_FRUSTUMCULL );
		}
	}

	// Reset the depth range to the origina values
	materials->DepthRange( depthmin, depthmax );

	// Restore the matrices
	materials->MatrixMode( MATERIAL_PROJECTION );
	materials->PopMatrix();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEnt - 
// Output : int
//-----------------------------------------------------------------------------

void CViewRender::AddVisibleEntity( C_BaseEntity *pEnt )
{
	IClientLeafSystem *pLeafSystem = ClientLeafSystem();

	RenderGroup_t renderGroup = pEnt->GetRenderGroup();

	if( pEnt->GetRenderHandle() == INVALID_CLIENT_RENDER_HANDLE )
	{

		// If this is the first time they've called this, setup a render handle.
		pEnt->SetupEntityRenderHandle( renderGroup );
	}
	else
	{
		// Tell the client leaf system to update the entity.. it may decide to relink
		// it into the leaves or not.
		pLeafSystem->DetectChanges( pEnt->GetRenderHandle(), renderGroup );
	}
}

VPlane* CViewRender::GetFrustum()
{
	Assert(g_bRenderingView);	// The frustum is only valid while in a RenderView call.
	return m_Frustum;
}



//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CViewRender::ShouldDrawBrushModels( void )
{
	if ( m_pDrawBrushModels && !m_pDrawBrushModels->GetInt() )
		return false;

	return true;
}


void SortEntities( CRenderList::CEntry *pEntities, int nEntities )
{
	// Don't sort if we only have 1 entity
	if ( nEntities <= 1 )
		return;

	float dists[CRenderList::MAX_GROUP_ENTITIES];

	const Vector &vecRenderOrigin = CurrentViewOrigin();
	const Vector &vecRenderForward = CurrentViewForward();

	// First get a distance for each entity.
	int i;
	for( i=0; i < nEntities; i++ )
	{
		IClientRenderable *pRenderable = pEntities[i].m_pRenderable;

		// Compute the center of the object (needed for translucent brush models)
		Vector boxcenter;
		Vector mins,maxs;
		pRenderable->GetRenderBounds( mins, maxs );
		VectorAdd( mins, maxs, boxcenter );
		VectorMA( pRenderable->GetRenderOrigin(), 0.5f, boxcenter, boxcenter );

		// Compute distance...
		Vector delta;
		VectorSubtract( boxcenter, vecRenderOrigin, delta );
		dists[i] = DotProduct( delta, vecRenderForward );
	}

	// H-sort.
	int stepSize = 4;
	while( stepSize )
	{
		int end = nEntities - stepSize;
		for( i=0; i < end; i += stepSize )
		{
			if( dists[i] > dists[i+stepSize] )
			{
				swap( pEntities[i], pEntities[i+stepSize] );
				swap( dists[i], dists[i+stepSize] );

				if( i == 0 )
				{
					i = -stepSize;
				}
				else
				{
					i -= stepSize << 1;
				}
			}
		}

		stepSize >>= 1;
	}
}


// FIXME: This is not static because we needed to turn it off for TF2 playtests
ConVar r_DrawDetailProps( "r_DrawDetailProps", "1" );

void CViewRender::SetupRenderList( const CViewSetup *pView, WorldListInfo_t& info, CRenderList &renderList )
{
	VPROF( "CViewRender::SetupRenderList" );

	// Clear the list.
	int i;
	for( i=0; i < RENDER_GROUP_COUNT; i++ )
	{
		renderList.m_RenderGroupCounts[i] = 0;
	}

	// Precache information used commonly in CollateRenderables
	SetupRenderInfo_t setupInfo;
	setupInfo.m_nRenderFrame = m_BuildWorldListsNumber;
	setupInfo.m_pRenderList = &renderList;
	setupInfo.m_bDrawDetailObjects = g_pClientMode->ShouldDrawDetailObjects() && r_DrawDetailProps.GetInt();

	if (pView)
	{
		setupInfo.m_vecRenderOrigin = pView->origin;
		setupInfo.m_flRenderDistSq = cl_maxrenderable_dist.GetFloat();
		setupInfo.m_flRenderDistSq  *= setupInfo.m_flRenderDistSq;
	}
	else
	{
		setupInfo.m_flRenderDistSq = 0.0f;
	}

	// Now collate the entities in the leaves.
	IClientLeafSystem *pClientLeafSystem = ClientLeafSystem();
	for( i=0; i < info.m_LeafCount; i++ )
	{
		int nTranslucent = renderList.m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY];

		// Add renderables from this leaf...
		pClientLeafSystem->CollateRenderablesInLeaf( info.m_pLeafList[i], i, setupInfo );

		int nNewTranslucent = renderList.m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY] - nTranslucent;
		if( nNewTranslucent )
		{
			// Sort the new translucent entities.
			SortEntities( &renderList.m_RenderGroups[RENDER_GROUP_TRANSLUCENT_ENTITY][nTranslucent], nNewTranslucent );
		}
	}
}

static ConVar mat_wateroverlaysize( "mat_wateroverlaysize", "128" );
static void OverlayWaterTexture( void )
{
	float offsetS = ( 0.5f / 256.0f );
	float offsetT = ( 0.5f / 256.0f );
	bool found;
	IMaterial *pMaterial;
	pMaterial = materials->FindMaterial( "debug/debugreflect", &found, true );
	if( found )
	{
		materials->Bind( pMaterial );
		IMesh* pMesh = materials->GetDynamicMesh( true );

		float w = mat_wateroverlaysize.GetFloat();
		float h = mat_wateroverlaysize.GetFloat();

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

		meshBuilder.Position3f( 0.0f, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( w, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( w, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( 0.0f, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.End();
		pMesh->Draw();
	}

	pMaterial = materials->FindMaterial( "debug/debugrefract", &found, true );
	if( found )
	{
		materials->Bind( pMaterial );
		IMesh* pMesh = materials->GetDynamicMesh( true );

		float w = mat_wateroverlaysize.GetFloat();
		float h = mat_wateroverlaysize.GetFloat();
		float xoffset = mat_wateroverlaysize.GetFloat();


		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

		meshBuilder.Position3f( xoffset + 0.0f, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( xoffset + w, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( xoffset + w, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( xoffset + 0.0f, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.End();
		pMesh->Draw();
	}

}

static ConVar mat_camerarendertargetoverlaysize( "mat_camerarendertargetoverlaysize", "128" );
static void OverlayCameraRenderTarget( void )
{
	float offsetS = ( 0.5f / 256.0f );
	float offsetT = ( 0.5f / 256.0f );
	bool found;
	IMaterial *pMaterial;
	pMaterial = materials->FindMaterial( "debug/debugcamerarendertarget", &found, true );
	if( found )
	{
		materials->Bind( pMaterial );
		IMesh* pMesh = materials->GetDynamicMesh( true );

		float w = mat_wateroverlaysize.GetFloat();
		float h = mat_wateroverlaysize.GetFloat();

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

		meshBuilder.Position3f( 0.0f, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( w, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( w, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( 0.0f, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.End();
		pMesh->Draw();
	}
}


static ConVar mat_framebuffercopyoverlaysize( "mat_framebuffercopyoverlaysize", "128" );
static void OverlayFrameBufferTexture( void )
{
	float offsetS = ( 0.5f / 256.0f );
	float offsetT = ( 0.5f / 256.0f );
	bool found;
	IMaterial *pMaterial;
	pMaterial = materials->FindMaterial( "debug/debugfbtexture", &found, true );
	if( found )
	{
		materials->Bind( pMaterial );
		IMesh* pMesh = materials->GetDynamicMesh( true );

		float w = mat_framebuffercopyoverlaysize.GetFloat();
		float h = mat_framebuffercopyoverlaysize.GetFloat();

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

		meshBuilder.Position3f( 0.0f, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( w, 0.0f, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 0.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( w, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( 0.0f, h, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f + offsetS, 1.0f + offsetT );
		meshBuilder.AdvanceVertex();

		meshBuilder.End();
		pMesh->Draw();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws the world
//-----------------------------------------------------------------------------
void CViewRender::BuildWorldRenderLists( 
	const CViewSetup *pView, 
	WorldListInfo_t& info, 
	CRenderList &renderList, 
	bool updateLightmaps,
	int iForceViewLeaf )
{
	VPROF_BUDGET( "BuildWorldRenderLists", VPROF_BUDGETGROUP_WORLD_RENDERING );

	{
		MEASURE_TIMED_STAT( CS_BUILDWORLDLISTS_TIME );
		++m_BuildWorldListsNumber;
		render->BuildWorldLists( &info, updateLightmaps, iForceViewLeaf );
	}

	// Now that we have the list of all leaves, regenerate shadows cast
	g_pClientShadowMgr->ComputeShadowTextures( pView, info.m_LeafCount, info.m_pLeafList );

	// Compute the prop opacity based on the view position
	staticpropmgr->ComputePropOpacity( CurrentViewOrigin() );

	// Build a list of detail props to render
	DetailObjectSystem()->BuildDetailObjectRenderLists();

	{
		MEASURE_TIMED_STAT( CS_BUILDRENDERABLELIST_TIME );
		// For better sorting, find out the leaf *nearest* to the camera
		// and render translucent objects as if they are in that leaf.
		if( ShouldDrawEntities() )
		{
			ClientLeafSystem()->ComputeTranslucentRenderLeaf( 
				info.m_LeafCount, info.m_pLeafList, info.m_pLeafFogVolume, m_BuildWorldListsNumber );
		}
		
		SetupRenderList( pView, info, renderList );
	}

	// Iterate through any bmodels that aren't rotated/translated ( they use the identity matrix )
	//  and therefore are rendered with the world as an optimization
	{
		MEASURE_TIMED_STAT( CS_BUILDWORLDLISTS_TIME );

		if ( ShouldDrawEntities() && ShouldDrawBrushModels() )
		{
			for ( int i=0 ; i < renderList.m_RenderGroupCounts[RENDER_GROUP_WORLD] ; i++)
			{
				// garymcthack - verify diffs on this one.
				IClientRenderable *ent = renderList.m_RenderGroups[RENDER_GROUP_WORLD][i].m_pRenderable;
				
				// Draw special case brush model
				if ( ent->LODTest() )
				{
					render->DrawIdentityBrushModel( const_cast<struct model_t*>(ent->GetModel()) );
				}
			}
		}
	}
}


static inline unsigned long BuildDrawFlags( bool drawSkybox, bool drawUnderWater, bool drawAboveWater, bool drawWaterSurface )
{
	unsigned long engineFlags = 0;
	if (drawSkybox)
		engineFlags |= DRAWWORLDLISTS_DRAW_SKYBOX;

	if( drawAboveWater && drawUnderWater )
	{
		engineFlags |= DRAWWORLDLISTS_DRAW_STRICTLYABOVEWATER;
		engineFlags |= DRAWWORLDLISTS_DRAW_STRICTLYUNDERWATER;
		engineFlags |= DRAWWORLDLISTS_DRAW_INTERSECTSWATER;
	}
	else if( drawAboveWater )
	{
		engineFlags |= DRAWWORLDLISTS_DRAW_STRICTLYABOVEWATER;
		engineFlags |= DRAWWORLDLISTS_DRAW_INTERSECTSWATER;
	}
	else if( drawUnderWater )
	{
		engineFlags |= DRAWWORLDLISTS_DRAW_STRICTLYUNDERWATER;
		engineFlags |= DRAWWORLDLISTS_DRAW_INTERSECTSWATER;
	}

	static ConVar r_drawwatersurface( "r_drawwatersurface", "1" );
	if( drawWaterSurface && r_drawwatersurface.GetBool() )
	{
		engineFlags |= DRAWWORLDLISTS_DRAW_WATERSURFACE;
	}

	return engineFlags;
}

void CViewRender::DrawWorld( WorldListInfo_t& info, CRenderList &renderList, int flags )
{
	VPROF_BUDGET( "DrawWorld", VPROF_BUDGETGROUP_WORLD_RENDERING );
	if( !r_drawopaqueworld.GetBool() )
	{
		return;
	}
	MEASURE_TIMED_STAT( CS_OPAQUE_WORLD_RENDER_TIME );

	bool drawSkybox = (flags & DF_DRAWSKYBOX) != 0;
	bool drawUnderWater = (flags & DF_RENDER_UNDERWATER) != 0;
	bool drawAboveWater = (flags & DF_RENDER_ABOVEWATER) != 0;
	bool drawWaterSurface = (flags & DF_RENDER_WATER) != 0;

	unsigned long engineFlags = BuildDrawFlags( drawSkybox, drawUnderWater, drawAboveWater, drawWaterSurface );

	render->DrawWorldLists( engineFlags );
}

static inline void DrawOpaqueRenderable( IClientRenderable *pEnt, bool twoPass )
{
	float color[3];

	pEnt->GetColorModulation( color );
	render->SetColorModulation(	color );


	int flags = STUDIO_RENDER | STUDIO_FRUSTUMCULL | STUDIO_OCCLUSIONCULL;
	if (twoPass)
	{
		flags |= STUDIO_TWOPASS;
	}

	pEnt->DrawModel( flags );
}


static inline void DrawTranslucentRenderable( IClientRenderable *pEnt, bool twoPass )
{
	// Determine blending amount and tell engine
	float blend = (float)( pEnt->GetFxBlend() / 255.0f );

	// Totally gone
	if ( blend <= 0.0f )
		return;

	// Tell engine
	render->SetBlend( blend );

	float color[3];
	pEnt->GetColorModulation( color );
	render->SetColorModulation(	color );

	int flags = STUDIO_RENDER | STUDIO_FRUSTUMCULL | STUDIO_OCCLUSIONCULL | STUDIO_TRANSPARENCY;
	if (twoPass)
		flags |= STUDIO_TWOPASS;

	pEnt->DrawModel( flags );
}


//-----------------------------------------------------------------------------
// Draws all opaque renderables in leaves that were rendered
//-----------------------------------------------------------------------------
void CViewRender::DrawOpaqueRenderables( WorldListInfo_t& info, CRenderList &renderList )
{
	VPROF("CViewRender::DrawOpaqueRenderables");

	if( !r_drawopaquerenderables.GetBool() )
	{
		return;
	}
	
	MEASURE_TIMED_STAT( CS_OPAQUE_RENDER_TIME );

	if( !ShouldDrawEntities() )
		return;

	render->SetBlend( 1 );
	
	// Iterate over all leaves that were visible, and draw opaque things in them.
	CRenderList::CEntry *pEntities = renderList.m_RenderGroups[RENDER_GROUP_OPAQUE_ENTITY];
	int nOpaque = renderList.m_RenderGroupCounts[RENDER_GROUP_OPAQUE_ENTITY];

	for( int i=0; i < nOpaque; ++i )
	{
		DrawOpaqueRenderable( pEntities[i].m_pRenderable, (pEntities[i].m_TwoPass != 0) );
	}
}


//-----------------------------------------------------------------------------
// Draws all translucent renderables in leaves that were rendered
//-----------------------------------------------------------------------------
void CViewRender::DrawTranslucentRenderables( WorldListInfo_t& info, CRenderList &renderList )
{
	VPROF( "DrawTranslucentRenderables" );
	int iCurLeaf = info.m_LeafCount - 1;
	bool bDrawTranslucentWorld = r_drawtranslucentworld.GetBool();
	bool bDrawTranslucentRenderables = r_drawtranslucentrenderables.GetBool();

	unsigned long drawFlags = DRAWWORLDLISTS_DRAW_STRICTLYABOVEWATER | 
		DRAWWORLDLISTS_DRAW_STRICTLYUNDERWATER | DRAWWORLDLISTS_DRAW_INTERSECTSWATER;
	
	MEASURE_TIMED_STAT( CS_TRANSLUCENT_RENDER_TIME );

	if (ShouldDrawEntities() && bDrawTranslucentRenderables)
	{
		// Draw the particle singletons.
		DrawParticleSingletons();
				
		CRenderList::CEntry *pEntities = renderList.m_RenderGroups[RENDER_GROUP_TRANSLUCENT_ENTITY];
		int iCurTranslucentEntity = renderList.m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY] - 1;

		while( iCurTranslucentEntity >= 0 )
		{
			// Seek the current leaf up to our current translucent-entity leaf.
			int iNextLeaf = pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf;

			// First draw the translucent parts of the world at this leaf
			if (bDrawTranslucentWorld)
			{
				for( ; iCurLeaf >= iNextLeaf; iCurLeaf-- )
				{
					render->DrawTranslucentSurfaces( iCurLeaf, drawFlags );
				}
			}
			else
			{
				iCurLeaf = iNextLeaf;
			}

			// Draw all the translucent entities with this leaf.
			while( pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf == iNextLeaf && iCurTranslucentEntity >= 0 )
			{
				DrawTranslucentRenderable( pEntities[iCurTranslucentEntity].m_pRenderable, 
					(pEntities[iCurTranslucentEntity].m_TwoPass != 0) );

				--iCurTranslucentEntity;
			}
		}
	}

	// Draw the rest of the surfaces in world leaves
	if (bDrawTranslucentWorld)
	{
		while( iCurLeaf >= 0 )
		{
			render->DrawTranslucentSurfaces( iCurLeaf, drawFlags );
			--iCurLeaf;
		}
	}

	// Reset the blend state.
	render->SetBlend( 1 );
}


//-----------------------------------------------------------------------------
// Renders all translucent world + detail objects in a particular set of leaves
//-----------------------------------------------------------------------------
void CViewRender::DrawTranslucentWorldInLeaves( int iCurLeafIndex, int iFinalLeafIndex, int *pLeafList,
											   int drawFlags, int &nDetailLeafCount, int* pDetailLeafList )
{
	VPROF_BUDGET( "CViewRender::DrawTranslucentWorldInLeaves", VPROF_BUDGETGROUP_WORLD_RENDERING );
	for( ; iCurLeafIndex >= iFinalLeafIndex; iCurLeafIndex-- )
	{
		if ( render->LeafContainsTranslucentSurfaces( iCurLeafIndex, drawFlags ) )
		{
			// First draw any queued-up detail props from previously visited leaves
			DetailObjectSystem()->RenderTranslucentDetailObjects( CurrentViewOrigin(), CurrentViewForward(), nDetailLeafCount, pDetailLeafList );
			nDetailLeafCount = 0;

			// Now draw the surfaces in this leaf
			render->DrawTranslucentSurfaces( iCurLeafIndex, drawFlags );
		}

		// Queue up detail props that existed in this leaf
		if ( ClientLeafSystem()->ShouldDrawDetailObjectsInLeaf( pLeafList[iCurLeafIndex], m_BuildWorldListsNumber ) )
		{
			pDetailLeafList[nDetailLeafCount] = pLeafList[iCurLeafIndex];
			++nDetailLeafCount;
		}
	}
}


//-----------------------------------------------------------------------------
// A version which renders detail objects in a big batch for perf reasons
//-----------------------------------------------------------------------------
void CViewRender::DrawTranslucentRenderablesV2( WorldListInfo_t& info, CRenderList &renderList )
{
	VPROF("CViewRender::DrawTranslucentRenderablesV2");

	int iCurLeaf = info.m_LeafCount - 1;
	int nDetailLeafCount = 0;
	int *pDetailLeafList = (int*)stackalloc( info.m_LeafCount * sizeof(int) );

	bool bDrawTranslucentWorld = r_drawtranslucentworld.GetBool();
	bool bDrawTranslucentRenderables = r_drawtranslucentrenderables.GetBool();

	unsigned long drawFlags = DRAWWORLDLISTS_DRAW_STRICTLYABOVEWATER | 
		DRAWWORLDLISTS_DRAW_STRICTLYUNDERWATER | DRAWWORLDLISTS_DRAW_INTERSECTSWATER;
	
	MEASURE_TIMED_STAT( CS_TRANSLUCENT_RENDER_TIME );

	if (ShouldDrawEntities() && bDrawTranslucentRenderables)
	{
		// Draw the particle singletons.
		DrawParticleSingletons();
				
		CRenderList::CEntry *pEntities = renderList.m_RenderGroups[RENDER_GROUP_TRANSLUCENT_ENTITY];
		int iCurTranslucentEntity = renderList.m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY] - 1;

		while( iCurTranslucentEntity >= 0 )
		{
			// Seek the current leaf up to our current translucent-entity leaf.
			int iNextLeaf = pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf;

			// First draw the translucent parts of the world at this leaf
			if (bDrawTranslucentWorld)
			{
				DrawTranslucentWorldInLeaves( iCurLeaf, iNextLeaf, info.m_pLeafList, drawFlags, nDetailLeafCount, pDetailLeafList );
				iCurLeaf = iNextLeaf - 1;
			}

			// Draw all the translucent entities with this leaf.
			while( pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf == iNextLeaf && iCurTranslucentEntity >= 0 )
			{
				DrawTranslucentRenderable( pEntities[iCurTranslucentEntity].m_pRenderable, 
					(pEntities[iCurTranslucentEntity].m_TwoPass != 0) );

				--iCurTranslucentEntity;
			}
		}
	}

	// Draw the rest of the surfaces in world leaves
	if (bDrawTranslucentWorld)
	{
		DrawTranslucentWorldInLeaves( iCurLeaf, 0, info.m_pLeafList, drawFlags, nDetailLeafCount, pDetailLeafList );

		// Draw any queued-up detail props from previously visited leaves
		DetailObjectSystem()->RenderTranslucentDetailObjects( CurrentViewOrigin(), CurrentViewForward(), nDetailLeafCount, pDetailLeafList );
	}

	// Reset the blend state.
	render->SetBlend( 1 );
}


//-----------------------------------------------------------------------------
// Renders the shadow texture to screen...
//-----------------------------------------------------------------------------
static void RenderMaterial( const char *pMaterialName )
{
	// So it's not in the very top left
	float x = 100.0f, y = 100.0f;
	// float x = 0.0f, y = 0.0f;

	bool bFound;
	IMaterial *pMaterial = materials->FindMaterial( pMaterialName, &bFound, false );
	if (bFound)
	{
		materials->Bind( pMaterial );
		IMesh* pMesh = materials->GetDynamicMesh( true );

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

		meshBuilder.Position3f( x, y, 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f, 0.0f );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( x + pMaterial->GetMappingWidth(), y, 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f, 0.0f );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( x + pMaterial->GetMappingWidth(), y + pMaterial->GetMappingHeight(), 0.0f );
		meshBuilder.TexCoord2f( 0, 1.0f, 1.0f );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertex();

		meshBuilder.Position3f( x, y + pMaterial->GetMappingHeight(), 0.0f );
		meshBuilder.TexCoord2f( 0, 0.0f, 1.0f );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertex();

		meshBuilder.End();
		pMesh->Draw();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Sets the screen space effect material (can't be done during rendering)
//-----------------------------------------------------------------------------
void CViewRender::SetScreenSpaceEffectMaterial( IMaterial *pMaterial )
{
	m_ScreenSpaceEffectMaterial.Init( pMaterial );
}


//-----------------------------------------------------------------------------
// Purpose: Performs screen space effects, if any
//-----------------------------------------------------------------------------
void CViewRender::PerformScreenSpaceEffects()
{
	VPROF("CViewRender::PerformScreenSpaceEffects()");

	if (m_ScreenSpaceEffectMaterial)
	{
		// First copy the FB off to the offscreen texture
		UpdateScreenEffectTexture();

		// Now draw the entire screen using the material...
		materials->DrawScreenSpaceQuad( m_ScreenSpaceEffectMaterial );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Sets the screen space effect material (can't be done during rendering)
//-----------------------------------------------------------------------------
void CViewRender::SetScreenOverlayMaterial( IMaterial *pMaterial )
{
	m_ScreenOverlayMaterial.Init( pMaterial );
}

IMaterial *CViewRender::GetScreenOverlayMaterial( )
{
	return m_ScreenOverlayMaterial;
}


//-----------------------------------------------------------------------------
// Purpose: Performs screen space effects, if any
//-----------------------------------------------------------------------------
void CViewRender::PerformScreenOverlay()
{
	VPROF("CViewRender::PerformScreenOverlay()");

	if (m_ScreenOverlayMaterial)
	{
		if ( !m_ScreenOverlayMaterial->NeedsFrameBufferTexture() )
		{
			byte color[4] = { 255, 255, 255, 255 };
			render->ViewDrawFade( color, m_ScreenOverlayMaterial );
		}
		else
		{
			// First copy the FB off to the offscreen texture
			UpdateScreenEffectTexture();

			// Now draw the entire screen using the material...
			materials->DrawScreenSpaceQuad( m_ScreenOverlayMaterial );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Renders world and all entities, etc.
//-----------------------------------------------------------------------------
void CViewRender::DrawWorldAndEntities( bool drawSkybox, const CViewSetup &view )
{
	VPROF("CViewRender::DrawWorldAndEntities");

	WorldListInfo_t info;	
	CRenderList renderList;

	EnableWorldFog();

	int flags = DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	if (drawSkybox)
		flags |= DF_DRAWSKYBOX;

	render->ViewSetup3D( &view, m_Frustum );
	BuildWorldRenderLists( &view, info, renderList, true, -1 );
	
	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	DrawWorld( info, renderList, flags );
	DrawOpaqueRenderables( info, renderList );
	DrawTranslucentRenderablesV2( info, renderList );
}


//-----------------------------------------------------------------------------
// Draws all the debugging info
//-----------------------------------------------------------------------------
void CViewRender::Draw3DDebuggingInfo( const CViewSetup &view )
{
	VPROF("CViewRender::Draw3DDebuggingInfo");

	// Draw 3d overlays
	render->Draw3DDebugOverlays();

	// Draw the line file used for debugging leaks
	render->DrawLineFile();
	
	// Draw client side effects
	// NOTE: These are not sorted against the rest of the frame
	clienteffects->DrawEffects( gpGlobals->frametime );	
}


//-----------------------------------------------------------------------------
// Draws all the debugging info
//-----------------------------------------------------------------------------
void CViewRender::Draw2DDebuggingInfo( const CViewSetup &view )
{
	if ( mat_yuv.GetInt() && (engine->GetDXSupportLevel() >= 80) )
	{
		bool bFound;
		IMaterial *pMaterial;
		pMaterial = materials->FindMaterial( "debug/yuv", &bFound, true );
		if( bFound )
		{
			// First copy the FB off to the offscreen texture
			UpdateScreenEffectTexture();
			materials->DrawScreenSpaceQuad( pMaterial );
		}
	}

	if ( mat_hsv.GetInt() && (engine->GetDXSupportLevel() >= 90) )
	{
		bool bFound;
		IMaterial *pMaterial;
		pMaterial = materials->FindMaterial( "debug/hsv", &bFound, true );
		if( bFound )
		{
			// First copy the FB off to the offscreen texture
			UpdateScreenEffectTexture();
			materials->DrawScreenSpaceQuad( pMaterial );
		}
	}

	if( mat_bloom.GetInt() > 1 && engine->SupportsHDR() )
	{
		bool bFound = false;
		IMaterial *pMaterial = NULL;
		switch( mat_bloom.GetInt() )
		{
		case 2:
			pMaterial = materials->FindMaterial( "debug/showdestalpha", &bFound, true );
			break;
		case 3:
			pMaterial = materials->FindMaterial( "debug/showdestalpha_blurred", &bFound, true );
			break;
		case 4:
			pMaterial = materials->FindMaterial( "debug/showblurredcolor", &bFound, true );
			break;
		case 5:
			pMaterial = materials->FindMaterial( "debug/showdestalphatimescolor_blurred", &bFound, true );
			break;
		default:
			break;
		}
		if( bFound )
		{
			// Since bloom is ON, FB texture is already copied
			materials->DrawScreenSpaceQuad( pMaterial );
		}
	}

	// Draw debugging lightmaps
	render->DrawLightmaps();

	if( cl_drawshadowtexture.GetInt() )
	{
		g_pClientShadowMgr->RenderShadowTexture( view.width, view.height );
	}

	const char *pDrawMaterial = cl_drawmaterial.GetString();
	if( pDrawMaterial && pDrawMaterial[0] )
	{
		RenderMaterial( pDrawMaterial ); 
	}

	if( mat_showwatertextures.GetBool() )
	{
		OverlayWaterTexture();
	}

	if( mat_showcamerarendertarget.GetBool() )
	{
		OverlayCameraRenderTarget();
	}

	if( mat_showframebuffertexture.GetBool() )
	{
		OverlayFrameBufferTexture();
	}
}


//-----------------------------------------------------------------------------
// Purpose: Renders world and all entities, etc.
//-----------------------------------------------------------------------------
void CViewRender::ViewDrawScene( bool drawSkybox, const CViewSetup &view, bool bSetupViewModel, bool bDrawViewModel )
{
	VPROF( "CViewRender::ViewDrawScene" );

	SetupCurrentView( view. origin, view.angles );

	// Invoke pre-render methods
	IGameSystem::PreRenderAllSystems();

	// Start view, clear frame/z buffer if necessary
	SetupVis( view );

	g_ParticleMgr.IncrementFrameCode();

//#if !defined( TF2_CLIENT_DLL )
	WaterDrawWorldAndEntities( drawSkybox, view );
//#else
//	DrawWorldAndEntities( drawSkybox, view );
//#endif

	// Disable fog for the rest of the stuff
	DisableFog();

	// UNDONE: Don't do this with masked brush models, they should probably be in a separate list
	// render->DrawMaskEntities()
	
	// Here are the overlays...

	// This is an overlay that goes over everything else
#if defined( TF2_CLIENT_DLL )
	CGroundLine::DrawAllGroundLines();
#endif

	CGlowOverlay::DrawOverlays();

	// And here are the screen-space effects
	PerformScreenSpaceEffects();

	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	// Debugging info goes over the top
	Draw3DDebuggingInfo( view );

	// Let the particle manager simulate things that haven't been simulated.
	g_ParticleMgr.SimulateUndrawnEffects();

	FinishCurrentView();
}



static ConVar fog_override( "fog_override", "0" );
// set any of these to use the maps fog
static ConVar fog_start( "fog_start", "-1" );
static ConVar fog_end( "fog_end", "-1" );
static ConVar fog_color( "fog_color", "-1 -1 -1" );
static ConVar fog_enable( "fog_enable", "1" );
static ConVar fog_startskybox( "fog_startskybox", "-1" );
static ConVar fog_endskybox( "fog_endskybox", "-1" );
static ConVar fog_colorskybox( "fog_colorskybox", "-1 -1 -1" );
static ConVar fog_enableskybox( "fog_enableskybox", "1" );


//-----------------------------------------------------------------------------
// Purpose: Returns the fog color to use in rendering the current frame.
//-----------------------------------------------------------------------------
static void GetFogColor( float *pColor )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	const char *fogColorString = fog_color.GetString();
	if( fog_override.GetInt() && fogColorString )
	{
		sscanf( fogColorString, "%f%f%f", pColor, pColor+1, pColor+2 );
	}
	else
	{
		if( local->m_fog.blend )
		{
			//
			// Blend between two fog colors based on viewing angle.
			// The secondary fog color is at 180 degrees to the primary fog color.
			//
			Vector forward;
			AngleVectors(pbp->GetAbsAngles(), &forward);
			
			Vector vNormalized = local->m_fog.dirPrimary;
			VectorNormalize( vNormalized );
			local->m_fog.dirPrimary = vNormalized;

			float flBlendFactor = 0.5 * forward.Dot( local->m_fog.dirPrimary ) + 0.5;

			// FIXME: convert to linear colorspace
			pColor[0] = local->m_fog.colorPrimary.GetR() * flBlendFactor + local->m_fog.colorSecondary.GetR() * ( 1 - flBlendFactor );
			pColor[1] = local->m_fog.colorPrimary.GetG() * flBlendFactor + local->m_fog.colorSecondary.GetG() * ( 1 - flBlendFactor );
			pColor[2] = local->m_fog.colorPrimary.GetB() * flBlendFactor + local->m_fog.colorSecondary.GetB() * ( 1 - flBlendFactor );
		}
		else
		{
			pColor[0] = local->m_fog.colorPrimary.GetR();
			pColor[1] = local->m_fog.colorPrimary.GetG();
			pColor[2] = local->m_fog.colorPrimary.GetB();
		}
	}

	VectorScale( pColor, 1.0f / 255.0f, pColor );
}


static float GetFogStart( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return 0.0f;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	if( fog_override.GetInt() )
	{
		if( fog_start.GetFloat() == -1.0f )
		{
			return local->m_fog.start;
		}
		else
		{
			return fog_start.GetFloat();
		}
	}
	else
	{
		return local->m_fog.start;
	}
}

static float GetFogEnd( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return 0.0f;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	if( fog_override.GetInt() )
	{
		if( fog_end.GetFloat() == -1.0f )
		{
			return local->m_fog.end;
		}
		else
		{
			return fog_end.GetFloat();
		}
	}
	else
	{
		return local->m_fog.end;
	}
}

static bool GetFogEnable( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return false;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	// Ask the clientmode
	if ( g_pClientMode->ShouldDrawFog() == false )
		return false;

	if( fog_override.GetInt() )
	{
		if( fog_enable.GetInt() )
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return local->m_fog.enable != false;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Returns the skybox fog color to use in rendering the current frame.
//-----------------------------------------------------------------------------
static void GetSkyboxFogColor( float *pColor )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	const char *fogColorString = fog_colorskybox.GetString();
	if( fog_override.GetInt() && fogColorString )
	{
		sscanf( fogColorString, "%f%f%f", pColor, pColor+1, pColor+2 );
	}
	else
	{
		if( local->m_skybox3d.fog.blend )
		{
			//
			// Blend between two fog colors based on viewing angle.
			// The secondary fog color is at 180 degrees to the primary fog color.
			//
			Vector forward;
			AngleVectors(pbp->GetAbsAngles(), &forward);
			
			Vector vNormalized;
			VectorNormalize(vNormalized);
			local->m_skybox3d.fog.dirPrimary = vNormalized;

			float flBlendFactor = 0.5 * forward.Dot( local->m_skybox3d.fog.dirPrimary ) + 0.5;

			// FIXME: convert to linear colorspace
			pColor[0] = local->m_skybox3d.fog.colorPrimary.GetR() * flBlendFactor + local->m_skybox3d.fog.colorSecondary.GetR() * ( 1 - flBlendFactor );
			pColor[1] = local->m_skybox3d.fog.colorPrimary.GetG() * flBlendFactor + local->m_skybox3d.fog.colorSecondary.GetG() * ( 1 - flBlendFactor );
			pColor[2] = local->m_skybox3d.fog.colorPrimary.GetB() * flBlendFactor + local->m_skybox3d.fog.colorSecondary.GetB() * ( 1 - flBlendFactor );
		}
		else
		{
			pColor[0] = local->m_skybox3d.fog.colorPrimary.GetR();
			pColor[1] = local->m_skybox3d.fog.colorPrimary.GetG();
			pColor[2] = local->m_skybox3d.fog.colorPrimary.GetB();
		}
	}

	VectorScale( pColor, 1.0f / 255.0f, pColor );
}


static float GetSkyboxFogStart( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return 0.0f;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	if( fog_override.GetInt() )
	{
		if( fog_startskybox.GetFloat() == -1.0f )
		{
			return local->m_skybox3d.fog.start;
		}
		else
		{
			return fog_startskybox.GetFloat();
		}
	}
	else
	{
		return local->m_skybox3d.fog.start;
	}
}

static float GetSkyboxFogEnd( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return 0.0f;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	if( fog_override.GetInt() )
	{
		if( fog_endskybox.GetFloat() == -1.0f )
		{
			return local->m_skybox3d.fog.end;
		}
		else
		{
			return fog_endskybox.GetFloat();
		}
	}
	else
	{
		return local->m_skybox3d.fog.end;
	}
}

static bool GetSkyboxFogEnable( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return false;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	if( fog_override.GetInt() )
	{
		if( fog_enableskybox.GetInt() )
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return !!local->m_skybox3d.fog.enable;
	}
}

void CViewRender::EnableWorldFog( void )
{
	VPROF("CViewRender::EnableWorldFog");
	if( GetFogEnable() )
	{
		float fogColor[3];
		GetFogColor( fogColor );
		materials->FogMode( MATERIAL_FOG_LINEAR );
		materials->FogColor3fv( fogColor );
		materials->FogStart( GetFogStart() );
		materials->FogEnd( GetFogEnd() );
	}
	else
	{
		materials->FogMode( MATERIAL_FOG_NONE );
	}
}

void CViewRender::Enable3dSkyboxFog( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	if( GetSkyboxFogEnable() )
	{
		float fogColor[3];
		GetSkyboxFogColor( fogColor );
		float scale = 1.0f;
		if ( local->m_skybox3d.scale > 0.0f )
		{
			scale = 1.0f / local->m_skybox3d.scale;
		}
		materials->FogMode( MATERIAL_FOG_LINEAR );
		materials->FogColor3fv( fogColor );
		materials->FogStart( GetSkyboxFogStart() * scale );
		materials->FogEnd( GetSkyboxFogEnd() * scale );
	}
	else
	{
		materials->FogMode( MATERIAL_FOG_NONE );
	}
}

void CViewRender::DisableFog( void )
{
	VPROF("CViewRander::DisableFog()");

	materials->FogMode( MATERIAL_FOG_NONE );
}

bool CViewRender::Draw3dSkyboxworld( const CViewSetup &view )
{
	VPROF( "CViewRender::Draw3dSkyboxworld" );

	// render the 3D skybox
	if ( !r_3dsky.GetInt() )
		return false;

	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();

	// No local player object yet...
	if ( !pbp )
		return false;

	CPlayerLocalData* local = &pbp->m_Local;
	if ( local->m_skybox3d.area == 255 )
		return false;

	unsigned char **areabits = render->GetAreaBits();
	unsigned char *savebits;
	unsigned char tmpbits[ 32 ];
	savebits = *areabits;
	memset( tmpbits, 0, sizeof(tmpbits) );
	
	// set the sky area bit
	tmpbits[local->m_skybox3d.area>>3] |= 1 << (local->m_skybox3d.area&7);

	*areabits = tmpbits;
	CViewSetup skyView = view;
	skyView.zNear = 0.5;
	skyView.zFar = 18000;
	skyView.clearDepth = true;
	skyView.clearColor = view.clearColor;

	// scale origin by sky scale
	if ( local->m_skybox3d.scale > 0 )
	{
		float scale = 1.0f/local->m_skybox3d.scale;
		VectorScale( skyView.origin, scale, skyView.origin );
	}
	Enable3dSkyboxFog();
	VectorAdd( skyView.origin, local->m_skybox3d.origin, skyView.origin );
	
	skyView.m_vUnreflectedOrigin = skyView.origin;

	// BUGBUG: Fix this!!!  We shouldn't need to call setup vis for the sky if we're connecting
	// the areas.  We'd have to mark all the clusters in the skybox area in the PVS of any 
	// cluster with sky.  Then we could just connect the areas to do our vis.
	//m_bOverrideVisOrigin could hose us here, so call direct
	render->ViewSetupVis( false, 1, &local->m_skybox3d.origin.Get() );
	render->ViewSetup3D( &skyView, m_Frustum );

	// Store off view origin and angles
	SetupCurrentView( skyView. origin, skyView.angles );

	// Invoke pre-render methods
	IGameSystem::PreRenderAllSystems();

	WorldListInfo_t info;
	CRenderList renderList;

	BuildWorldRenderLists( NULL, info, renderList, true, -1 );
	DrawWorld( info, renderList, DF_DRAWSKYBOX | DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER | DF_RENDER_WATER );

	// Iterate over all leaves and render objects in those leaves
	DrawOpaqueRenderables( info, renderList );

	// Iterate over all leaves and render objects in those leaves
	DrawTranslucentRenderablesV2( info, renderList );

	DisableFog();

	// restore old area bits
	*areabits = savebits;

	FinishCurrentView();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CViewRender::SetupVis( const CViewSetup& view )
{
	VPROF( "CViewRender::SetupVis" );

	if ( m_bOverrideVisOrigin )
	{
		// Pass array or vis origins to merge
		render->ViewSetupVis( m_bForceNoVis, m_nNumVisOrigins, m_rgVisOrigins );
	}
	else
	{
		// Use render origin as vis origin by default
		render->ViewSetupVis( m_bForceNoVis, 1, &view.origin );
	}
}


// GR - HDR
static void DoScreenSpaceBloom( void )
{
	int w, h;
	IMaterial *pMatDownsample;
	IMaterial *pMatBlurX;
	IMaterial *pMatBlurY;
	IMaterial *pMatBloom;

	bool bFound;
	pMatDownsample = materials->FindMaterial( "dev/downsample", &bFound, true );
	if( !bFound )
		return;
	pMatBlurX = materials->FindMaterial( "dev/blurfilterx", &bFound, true );
	if( !bFound )
		return;
	pMatBlurY = materials->FindMaterial( "dev/blurfiltery", &bFound, true );
	if( !bFound )
		return;
	pMatBloom = materials->FindMaterial( "dev/bloom", &bFound, true );
	if( !bFound )
		return;

	int oldX, oldY, oldW, oldH;
	materials->GetViewport( oldX, oldY, oldW, oldH );

	ITexture *pSaveRenderTarget = materials->GetRenderTarget();

//	ITexture *pFBTexture = GetFullFrameFrameBufferTexture();
	ITexture *pFBTexture = GetFullFrameFrameBufferTexture( 0 );
	ITexture *pTex0 = GetSmallBufferHDR0();
	ITexture *pTex1 = GetSmallBufferHDR1();

	// First copy the FB off to the offscreen texture
	materials->CopyRenderTargetToTexture( pFBTexture );
	
	w = pTex0->GetActualWidth();
	h = pTex0->GetActualHeight();

	// Downsample image
	materials->SetRenderTarget( pTex0 );
	materials->Viewport( 0, 0, w, h );
	materials->DrawScreenSpaceQuad( pMatDownsample );

	// Blur filter pass 1
	materials->SetRenderTarget( pTex1 );
	materials->Viewport( 0, 0, w, h );
	materials->DrawScreenSpaceQuad( pMatBlurX );

	// Blur filter pass 2
	materials->SetRenderTarget( pTex0 );
	materials->Viewport( 0, 0, w, h );
	materials->DrawScreenSpaceQuad( pMatBlurY );

	// Render bloom
	materials->SetRenderTarget( pSaveRenderTarget );
	materials->Viewport( oldX, oldY, oldW, oldH );
	materials->DrawScreenSpaceQuad( pMatBloom );
}

bool g_bBlurredLastTime = false;
// To toggle the blur on and off
ConVar pp_motionblur("pp_motionblur", "0", FCVAR_ARCHIVE, "Motion Blur"); 
// The amount of alpha to use when adding the FB to our custom buffer
ConVar pp_motionblur_addalpha("pp_motionblur_addalpha", "0.7", FCVAR_ARCHIVE, "Motion Blur Alpha");
// The amount of alpha to use when adding our custom buffer to the FB
ConVar pp_motionblur_drawalpha("pp_motionblur_drawalpha", "1", FCVAR_ARCHIVE, "Motion Blur Draw Alpha");
// Delay to add between capturing the FB
ConVar pp_motionblur_time("pp_motionblur_time", "0", FCVAR_ARCHIVE, "The amount of time to wait until updating the FB");
/*void CViewRender::DoMotionBlur( void )
{
	if ( pp_motionblur.GetInt() == 0 ) return;

	static float fNextDrawTime = 0.0f;

	bool found;
	IMaterialVar* mv = NULL;
	IMaterial *pMatScreen = NULL;
	ITexture *pMotionBlur = NULL;
	ITexture *pOriginalTexture = NULL; 

	// Get the front buffer material
//	pMatScreen = materials->FindMaterial( "frontbuffer", TEXTURE_GROUP_OTHER, true );
	bool nFound;
	pMatScreen = materials->FindMaterial( "frontbuffer", &nFound, true );
	if( !nFound )
		return;
	
	// Set the camera up so we can draw the overlay
//	int oldX, oldY, oldW, oldH;
//	materials->GetViewport( oldX, oldY, oldW, oldH );

	// Get our custom render target
//	pMotionBlur = GetMotionBlurTex0( oldW, oldH );
	pMotionBlur = GetMotionBlurTex0();
	// Store the current render target	
	ITexture *pOriginalRenderTarget = materials->GetRenderTarget();

	// Set the camera up so we can draw the overlay
	// VXP: Uphere
//	int oldX, oldY, oldW, oldH;
//	materials->GetViewport( oldX, oldY, oldW, oldH );
 
	materials->MatrixMode( MATERIAL_PROJECTION );
	materials->PushMatrix();
	materials->LoadIdentity();	

	materials->MatrixMode( MATERIAL_VIEW );
	materials->PushMatrix();
	materials->LoadIdentity();	

	if ( fNextDrawTime - gpGlobals->curtime > 1.0f)
//	if ( fNextDrawTime - gpGlobals->frametime > 1.0f)
	{
		fNextDrawTime = 0.0f;
	}
	if( gpGlobals->curtime >= fNextDrawTime ) 
	{
	//	UpdateScreenEffectTexture( 0 );
		UpdateScreenEffectTexture();

		// Set the alpha to whatever our console variable is
		mv = pMatScreen->FindVar( "$alpha", &found, false );
		if (found)
		{
			if ( fNextDrawTime == 0 )
			{
				mv->SetFloatValue( 1.0f );
			}
			else
			{
				mv->SetFloatValue( pp_motionblur_addalpha.GetFloat() );
			}
		}

		materials->SetRenderTarget( pMotionBlur );
		materials->DrawScreenSpaceQuad( pMatScreen );

		// Set the next draw time according to the convar
		fNextDrawTime = gpGlobals->curtime + pp_motionblur_time.GetFloat();
	//	fNextDrawTime = gpGlobals->frametime + pp_motionblur_time.GetFloat();
	}
 
	// Set the alpha
	mv = pMatScreen->FindVar( "$alpha", &found, false );
	if (found)
	{
		mv->SetFloatValue( pp_motionblur_drawalpha.GetFloat() );
	}

	// Set the texture to our buffer
	mv = pMatScreen->FindVar( "$basetexture", &found, false );
	if (found)
	{
		pOriginalTexture = mv->GetTextureValue();
		mv->SetTextureValue( pMotionBlur );
	}

	// Pretend we were never here, set everything back
	materials->SetRenderTarget( pOriginalRenderTarget );
	materials->DrawScreenSpaceQuad( pMatScreen );
	
      // Set our texture back to _rt_FullFrameFB
	if (found)
	{
		mv->SetTextureValue( pOriginalTexture );
	}

	materials->DepthRange( 0.0f, 1.0f );
	materials->MatrixMode( MATERIAL_PROJECTION );
	materials->PopMatrix();
	materials->MatrixMode( MATERIAL_VIEW );
	materials->PopMatrix();
}*/
//void CASWViewRender::DoMotionBlur( const CViewSetup &view )
void CViewRender::DoMotionBlur( const CViewSetup &view )
{	
	float g_fMarinePoisonDuration = 0.0f;
	if ( pp_motionblur.GetInt() == 0 && g_fMarinePoisonDuration <= 0)
	{
		g_bBlurredLastTime = false;
		return;
	}

	static float fNextDrawTime = 0.0f;

	bool found;
	IMaterialVar* mv = NULL;
	IMaterial *pMatScreen = NULL;
	ITexture *pMotionBlur = NULL;
	ITexture *pOriginalTexture = NULL; 

	// Get the front buffer material
//	pMatScreen = materials->FindMaterial( "swarm/effects/frontbuffer", TEXTURE_GROUP_OTHER, true );
	pMatScreen = materials->FindMaterial( "frontbuffer", &found, true );
	if ( !found )
	{
		return;
	}
	// Get our custom render target
//	pMotionBlur = g_pASWRenderTargets->GetASWMotionBlurTexture();
	pMotionBlur = GetMotionBlurTexture();
	// Store the current render target
//	CMatRenderContextPtr pRenderContext( materials );
//	ITexture *pOriginalRenderTarget = pRenderContext->GetRenderTarget();
	ITexture *pOriginalRenderTarget = materials->GetRenderTarget();

	// Set the camera up so we can draw the overlay
	int oldX, oldY, oldW, oldH;
//	pRenderContext->GetViewport( oldX, oldY, oldW, oldH );

//	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
//	pRenderContext->PushMatrix();
//	pRenderContext->LoadIdentity();

//	pRenderContext->MatrixMode( MATERIAL_VIEW );
//	pRenderContext->PushMatrix();
//	pRenderContext->LoadIdentity();

	materials->GetViewport( oldX, oldY, oldW, oldH );

	materials->MatrixMode( MATERIAL_PROJECTION );
	materials->PushMatrix();
	materials->LoadIdentity();

	materials->MatrixMode( MATERIAL_VIEW );
	materials->PushMatrix();
	materials->LoadIdentity();

	// set our blur parameters, based on convars or the poison duration
	float add_alpha = pp_motionblur_addalpha.GetFloat();
	float blur_time = pp_motionblur_time.GetFloat();
	float draw_alpha = pp_motionblur_drawalpha.GetFloat();
	if (g_fMarinePoisonDuration > 0)
	{		
		if (g_fMarinePoisonDuration < 1.0f)
		{
			draw_alpha = g_fMarinePoisonDuration;
			add_alpha = 0.3f;
		}
		else
		{
			draw_alpha = 1.0f;
			float over_time = g_fMarinePoisonDuration - 1.0f;
		//	over_time = -MIN(4.0f, over_time);
			over_time = -min(4.0f, over_time);
			// map 0 to -4, to 0.3 to 0.05
			add_alpha = (over_time + 4) * 0.0625 + 0.05f;
		}
		blur_time = 0.05f;
	}
	if (!g_bBlurredLastTime)
		add_alpha = 1.0f;	// add the whole buffer if this is the first time we're blurring after a while, so we don't end up with images from ages ago

	if ( fNextDrawTime - gpGlobals->curtime > 1.0f)
	{
		fNextDrawTime = 0.0f;
	}

	if( gpGlobals->curtime >= fNextDrawTime ) 
	{
		UpdateScreenEffectTexture( 0, view.x, view.y, view.width, view.height );
	//	UpdateScreenEffectTexture();

		// Set the alpha to whatever our console variable is
		mv = pMatScreen->FindVar( "$alpha", &found, false );
		if (found)
		{
			if ( fNextDrawTime == 0 )
			{
				mv->SetFloatValue( 1.0f );
			}
			else
			{
				mv->SetFloatValue( add_alpha );
			}
		}

	//	pRenderContext->SetRenderTarget( pMotionBlur );
	//	pRenderContext->DrawScreenSpaceQuad( pMatScreen );
		materials->SetRenderTarget( pMotionBlur );
		materials->DrawScreenSpaceQuad( pMatScreen );

		// Set the next draw time according to the convar
		fNextDrawTime = gpGlobals->curtime + blur_time;
	}

	// Set the alpha
	mv = pMatScreen->FindVar( "$alpha", &found, false );
	if (found)
	{
		mv->SetFloatValue( draw_alpha );
	}

	// Set the texture to our buffer
	mv = pMatScreen->FindVar( "$basetexture", &found, false );
	if (found)
	{
		pOriginalTexture = mv->GetTextureValue();
		mv->SetTextureValue( pMotionBlur );
	}

	// Pretend we were never here, set everything back
//	pRenderContext->SetRenderTarget( pOriginalRenderTarget );
//	pRenderContext->DrawScreenSpaceQuad( pMatScreen );
	materials->SetRenderTarget( pOriginalRenderTarget );
	materials->DrawScreenSpaceQuad( pMatScreen );

	// Set our texture back to _rt_FullFrameFB
	if (found)
	{
		mv->SetTextureValue( pOriginalTexture );
	}

//	pRenderContext->DepthRange( 0.0f, 1.0f );
//	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
//	pRenderContext->PopMatrix();
//	pRenderContext->MatrixMode( MATERIAL_VIEW );
//	pRenderContext->PopMatrix();

	materials->DepthRange( 0.0f, 1.0f );
	materials->MatrixMode( MATERIAL_PROJECTION );
	materials->PopMatrix();
	materials->MatrixMode( MATERIAL_VIEW );
	materials->PopMatrix();

	g_bBlurredLastTime = true;
}

//-----------------------------------------------------------------------------
// Purpose: Renders entire view
// Input  : &view - 
//			drawViewModel - 
//-----------------------------------------------------------------------------
void CViewRender::RenderView( const CViewSetup &view, bool drawViewModel )
{
	VPROF( "CViewRender::RenderView" );

	CViewSetup tmpView = view;
	g_bRenderingView = true;

	// Must be first 
	render->SceneBegin();
		
	bool drawSky = r_skybox.GetBool();

	// if the 3d skybox world is drawn, then don't draw the normal skybox
	if ( Draw3dSkyboxworld( view ) )
	{
		// don't clear the framebuffer, we'll erase the 3d skybox
		tmpView.clearColor = false;
		
		// skip the normal skybox.
		drawSky = false;
	}

	// Force it to clear the framebuffer if they're in solid space.
	if ( !tmpView.clearColor )
	{
		if ( enginetrace->GetPointContents( tmpView.origin ) == CONTENTS_SOLID )
		{
			tmpView.clearColor = true;
		}
	}

	// Start view, clear frame/z buffer if necessary
//	SetupVis( tmpView );

	// Render world and all entities, particles, etc.
	ViewDrawScene( drawSky, tmpView, true, drawViewModel );

	// We can still use the 'current view' stuff set up in ViewDrawScene
	s_bCanAccessCurrentView = true;

	engine->DrawPortals();

	DisableFog();
	   
	// Finish scene
	render->SceneEnd();

	// Draw lightsources if enabled
	render->DrawLights();
	
	GetClientVoiceMgr()->DrawHeadLabels();

	// Now actually draw the viewmodel
	DrawViewModels( tmpView, drawViewModel );

	g_pClientMode->PostRenderWorld();

	// Draw fade over entire screen if needed
	byte color[4];
	bool blend;
	vieweffects->GetFadeParams( tmpView.context, &color[0], &color[1], &color[2], &color[3], &blend );

	// Overlay screen fade on entire screen
	IMaterial* pMaterial = blend ? m_ModulateSingleColor : m_TranslucentSingleColor;
	render->ViewDrawFade( color, pMaterial );
	PerformScreenOverlay();

	// Prevent sound stutter if going slow
	engine->Sound_ExtraUpdate();	

	// GR - HDR
	if( mat_bloom.GetBool() && engine->SupportsHDR() )
	{
		DoScreenSpaceBloom();
	}
	// VXP: This stub is temporary, I think
//	if( !engine->SupportsHDR() )
//	{
	//	DoMotionBlur();
		DoMotionBlur( tmpView );
//	}

	// Draw the 2D graphics
	tmpView.clearColor = false;
	tmpView.clearDepth = false;
	render->ViewSetup2D( &tmpView );

	Draw2DDebuggingInfo( tmpView );

	m_AnglesHistory[m_AnglesHistoryCounter] = tmpView.angles;
	m_AnglesHistoryCounter = (m_AnglesHistoryCounter+1) & ANGLESHISTORY_MASK;
	
	// If the angles are moving fast enough, allow LOD transitions.
	float angleMovementDelta = 0;
	for(int i=0; i < ANGLESHISTORY_SIZE; i++)
	{
		angleMovementDelta += (m_AnglesHistory[(i+1) & ANGLESHISTORY_MASK] - m_AnglesHistory[i]).Length();
	}

	angleMovementDelta /= ANGLESHISTORY_SIZE;
	if(angleMovementDelta > r_TransitionSensitivity.GetFloat())
	{
		r_DoCovertTransitions.SetValue(1);
	}
	else
	{
		r_DoCovertTransitions.SetValue(0);
	}

	g_bRenderingView = false;

	// We can no longer use the 'current view' stuff set up in ViewDrawScene
	s_bCanAccessCurrentView = false;

	// Next frame!
	++m_FrameNumber;
}

void ViewTransform( const Vector &worldSpace, Vector &viewSpace )
{
	const VMatrix &viewMatrix = engine->WorldToViewMatrix();
	Vector3DMultiplyPosition( viewMatrix, worldSpace, viewSpace );
}


//-----------------------------------------------------------------------------
// Purpose: UNDONE: Clean this up some, handle off-screen vertices
// Input  : *point - 
//			*screen - 
// Output : int
//-----------------------------------------------------------------------------
int ScreenTransform( const Vector& point, Vector& screen )
{
// UNDONE: Clean this up some, handle off-screen vertices
	float w;
	const VMatrix &worldToScreen = engine->WorldToScreenMatrix();

	screen.x = worldToScreen[0][0] * point[0] + worldToScreen[0][1] * point[1] + worldToScreen[0][2] * point[2] + worldToScreen[0][3];
	screen.y = worldToScreen[1][0] * point[0] + worldToScreen[1][1] * point[1] + worldToScreen[1][2] * point[2] + worldToScreen[1][3];
//	z		 = worldToScreen[2][0] * point[0] + worldToScreen[2][1] * point[1] + worldToScreen[2][2] * point[2] + worldToScreen[2][3];
	w		 = worldToScreen[3][0] * point[0] + worldToScreen[3][1] * point[1] + worldToScreen[3][2] * point[2] + worldToScreen[3][3];

	// Just so we have something valid here
	screen.z = 0.0f;

	bool behind;
	if( w < 0.001f )
	{
		behind = true;
		screen.x *= 100000;
		screen.y *= 100000;
	}
	else
	{
		behind = false;
		float invw = 1.0f / w;
		screen.x *= invw;
		screen.y *= invw;
	}

	return behind;
}



//-----------------------------------------------------------------------------
//
// NOTE: Below here is all of the stuff that needs to be done for water rendering
//
//-----------------------------------------------------------------------------

static ConVar r_debugcheapwater( "r_debugcheapwater", "0" );

//-----------------------------------------------------------------------------
// Draws the scene in water
//-----------------------------------------------------------------------------
void CViewRender::WaterDrawWorldAndEntities( bool drawSkybox, const CViewSetup &view )
{
	CViewSetup tmpView = view;
	m_pViewDrawSceneRenderTarget = materials->GetRenderTarget();

	bool eyeInFogVolume;
	int visibleFogVolume, visibleFogVolumeLeaf;
	float distanceToWater = 0.0f;

	{
		VPROF( "GetVisibleFogVolume" );
		MEASURE_TIMED_STAT( CS_GETVISIBLEFOGVOLUME_TIME );
		// FIXME!!! : We shouldn't have to do this here, but the 3dskybox/water combo is b0rked if we don't.
	//		render->ViewSetupVis( false, 1, &view.origin );
 		render->ViewSetup3D( &tmpView, m_Frustum );
		render->GetVisibleFogVolume( &visibleFogVolume, &visibleFogVolumeLeaf, &eyeInFogVolume, &distanceToWater, view.origin );
	}

	if( r_debugcheapwater.GetBool() )
	{
		if( visibleFogVolume != -1 )
		{
			Warning( "CheapWaterStartDistance: %f CheapWaterEndDistance: %f distanceToWater: %f ", 
				m_flCheapWaterStartDistance, m_flCheapWaterEndDistance, distanceToWater );
			if( distanceToWater > m_flCheapWaterEndDistance )
			{
				Warning( "drawing cheap water\n" );
			}
			else
			{
				Warning( "DRAWING EXPENSIVE WATER\n" );
			}
		}
		else
		{
			Warning( "no water visible\n" );
		}
	}
	
	// Force color clears for water texture 
	// It's needed to clear alpha channel of textures for HDR
	tmpView.clearColor = true;

	bool bCheapWater = false;
	bool bReflect = false;
	bool bRefract = false;
	bool bReflectEntities = false;
	if( visibleFogVolume != -1 )
	{
		if( distanceToWater > m_flCheapWaterEndDistance )
		{
			// pretend like we don't see water since it's far enough away that the LOD should take care of it.
			visibleFogVolume = -1;
			bCheapWater = true;
		}
		else
		{
			// Get the material that is for the water surface that is visible and check to see
			// what render targets need to be rendered, if any.
			IMaterial *pWaterMaterial = render->GetFogVolumeMaterial( visibleFogVolume );
			if( pWaterMaterial )
			{
				IMaterialVar *pReflectTextureMatVar = pWaterMaterial->FindVar( "$reflecttexture", NULL, false );
				IMaterialVar *pRefractTextureMatVar = pWaterMaterial->FindVar( "$refracttexture", NULL, false );
				bReflect = pReflectTextureMatVar->GetType() == MATERIAL_VAR_TYPE_TEXTURE;
				bRefract = pRefractTextureMatVar->GetType() == MATERIAL_VAR_TYPE_TEXTURE;
				if( !r_WaterDrawRefraction.GetBool() )
				{
					bRefract = false;
				}
				if( !r_WaterDrawReflection.GetBool() )
				{
					bReflect = false;
				}
				if( bReflect )
				{
				//	IMaterialVar *pReflectEntitiesMatVar = pWaterMaterial->FindVar( "$reflectentities", NULL, false );
				//	bReflectEntities = pReflectEntitiesMatVar->GetIntValue() != 0;
				//	bReflectEntities = true;
					bReflectEntities = r_WaterEntReflection.GetBool();
				}
				
				if( !( bReflect || bRefract ) )
				{
					visibleFogVolume = -1;
					bCheapWater = true;
				}
			}
			else
			{
				visibleFogVolume = -1;
				bCheapWater = true;
			}
		}
	}

	if( visibleFogVolume != -1 && mat_drawwater.GetBool() )
	{
		ClientStats().IncrementCountedStat( CS_WATER_VISIBLE, 1 );

		// We can see water of some sort
		if( !eyeInFogVolume )
		{
			int iForceWaterLeaf = -1;
			if ( r_ForceWaterLeaf.GetBool() )
				iForceWaterLeaf = visibleFogVolumeLeaf;

			ViewDrawScene_EyeAboveWater( drawSkybox, tmpView, visibleFogVolume, iForceWaterLeaf, bReflect, bRefract, bReflectEntities );
		}
		else
		{
			ViewDrawScene_EyeUnderWater( drawSkybox, tmpView, visibleFogVolume, bReflect, bRefract, bReflectEntities );
		}
	}
	else
	{
		ViewDrawScene_NoWater( drawSkybox, view, bCheapWater );
	}

	materials->SetRenderTarget( m_pViewDrawSceneRenderTarget );
}


void CViewRender::DrawOpaqueRenderablesInWater( WorldListInfo_t& info, CRenderList &renderList,
										 bool drawUnderWater, bool drawAboveWater )
{
	VPROF( "DrawOpaqueRenderablesInWater" );
	if( !r_drawopaquerenderables.GetBool() )
	{
		return;
	}
	
	MEASURE_TIMED_STAT( CS_OPAQUE_RENDER_TIME );

	if( !ShouldDrawEntities() )
		return;

	render->SetBlend( 1 );
	
	// Iterate over all leaves that were visible, and draw opaque things in them.
	// Iterate in front to back order. This is important for fog volumes; 
	// when we're in a fog volume and an object is partially in that volume, 
	// it'll be rendered as part of the fog volume (and have the correct fogging). 
	// It also helps with fast z reject
	int lastFogVolume = -1;
	
	CRenderList::CEntry *pEntities = renderList.m_RenderGroups[RENDER_GROUP_OPAQUE_ENTITY];
	int nOpaque = renderList.m_RenderGroupCounts[RENDER_GROUP_OPAQUE_ENTITY];

	for( int i=0; i < nOpaque; i++ )
	{
		int iLeaf = pEntities[i].m_iWorldListInfoLeaf;
		
		if( info.m_pLeafFogVolume[iLeaf] != lastFogVolume )
		{
//			render->SetFogVolumeState( info.m_pLeafFogVolume[i] );
			lastFogVolume = info.m_pLeafFogVolume[i];
		}
		bool underWater = ClientLeafSystem()->IsRenderableUnderWater( info, &pEntities[i] );
		bool aboveWater = ClientLeafSystem()->IsRenderableAboveWater( info, &pEntities[i] );
		if( ( drawUnderWater && underWater ) || ( drawAboveWater && aboveWater ) )
		{
			if( g_bWaterReflectionCull )
			{
				Vector mins, maxs;
				pEntities[i].m_pRenderable->GetRenderBounds( mins, maxs );
				float minz = mins.z + pEntities[i].m_pRenderable->GetRenderOrigin().z;
				if( minz > g_bWaterCullHeight )
				{
					continue;
				}
			}
			DrawOpaqueRenderable( pEntities[i].m_pRenderable, (pEntities[i].m_TwoPass != 0) );
		}
	}
}


void CViewRender::DrawTranslucentRenderablesInWater( WorldListInfo_t& info, CRenderList &renderList, 
											 bool drawUnderWater, bool drawAboveWater )
{
	VPROF( "DrawTranslucentRenderablesInWater" );
	bool bDrawTranslucentWorld = r_drawtranslucentworld.GetBool();
	bool bDrawTranslucentRenderables = r_drawtranslucentrenderables.GetBool();

	unsigned long drawFlags = BuildDrawFlags( false, drawUnderWater, drawAboveWater, false );
	
	MEASURE_TIMED_STAT( CS_TRANSLUCENT_RENDER_TIME );

	int lastFogVolume = -1;

	int iCurLeaf = info.m_LeafCount - 1;
	if( ShouldDrawEntities() && bDrawTranslucentRenderables )
	{
		DrawParticleSingletons();
		
		CRenderList::CEntry *pEntities = renderList.m_RenderGroups[RENDER_GROUP_TRANSLUCENT_ENTITY];
		int iCurTranslucentEntity = renderList.m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY] - 1;
		while( iCurTranslucentEntity >= 0 )
		{
			// Seek the current leaf up to our current translucent-entity leaf.
			int iNextLeaf = pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf;

			if (bDrawTranslucentWorld)
			{
				for( ; iCurLeaf >= iNextLeaf; iCurLeaf-- )
				{
					render->DrawTranslucentSurfaces( iCurLeaf, drawFlags );
				}
			}
			else
			{
				iCurLeaf = iNextLeaf;
			}

			// Draw the entities in this leaf.
			int iThisLeaf = pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf;
			if( info.m_pLeafFogVolume[iThisLeaf] != lastFogVolume )
			{
//				render->SetFogVolumeState( info.m_pLeafFogVolume[iThisLeaf] );
				lastFogVolume = info.m_pLeafFogVolume[iThisLeaf];
			}

			// Draw all the translucent entities with this leaf.
			while( pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf == iThisLeaf && iCurTranslucentEntity >= 0 )
			{
				// garymcthack - this all should move inside of the conditional below!
				static int lastCopyFrameNumber = -1;
				if( ( cl_alwayscopyframebuffertotexture.GetInt() || m_FrameNumber != lastCopyFrameNumber ) &&
					pEntities[iCurTranslucentEntity].m_pRenderable->UsesFrameBufferTexture() &&
					cl_copyframebuffertotexture.GetInt() )
				{
					MEASURE_TIMED_STAT( CS_COPYFB_TIME );
					ClientStats().IncrementCountedStat( CS_NUM_COPYFB, 1 );
					UpdateRefractTexture();
					lastCopyFrameNumber = m_FrameNumber;
				}

				Assert( ClientLeafSystem()->IsRenderableUnderWater( info, &pEntities[iCurTranslucentEntity] ) ||
						ClientLeafSystem()->IsRenderableAboveWater( info, &pEntities[iCurTranslucentEntity] ) );
				bool underWater = ClientLeafSystem()->IsRenderableUnderWater( info, &pEntities[iCurTranslucentEntity] );
				bool aboveWater = ClientLeafSystem()->IsRenderableAboveWater( info, &pEntities[iCurTranslucentEntity] );
				if( ( drawUnderWater && underWater ) || ( drawAboveWater && aboveWater ) )
				{
					if( g_bWaterReflectionCull )
					{
						Vector mins, maxs;
						pEntities[iCurTranslucentEntity].m_pRenderable->GetRenderBounds( mins, maxs );
						float minz = mins.z + pEntities[iCurTranslucentEntity].m_pRenderable->GetRenderOrigin().z;
						if( minz > g_bWaterCullHeight )
						{
							--iCurTranslucentEntity;
							continue;
						}
					}
					DrawTranslucentRenderable( pEntities[iCurTranslucentEntity].m_pRenderable, 
						(pEntities[iCurTranslucentEntity].m_TwoPass != 0) );
				}
				--iCurTranslucentEntity;
			}
		}
	}

	// Draw the rest of the surfaces in world leaves
	if (bDrawTranslucentWorld)
	{
		while( iCurLeaf >= 0 )
		{
			render->DrawTranslucentSurfaces( iCurLeaf, drawFlags );
			--iCurLeaf;
		}
	}

	// Reset the blend state.
	render->SetBlend( 1 );
}


void CViewRender::DrawTranslucentRenderablesInWaterV2( WorldListInfo_t& info, CRenderList &renderList, 
											 bool drawUnderWater, bool drawAboveWater )
{
	VPROF( "DrawTranslucentRenderablesInWaterV2" );
	int iCurLeaf = info.m_LeafCount - 1;
	int nDetailLeafCount = 0;
	int *pDetailLeafList = (int*)stackalloc( info.m_LeafCount * sizeof(int) );

	bool bDrawTranslucentWorld = r_drawtranslucentworld.GetBool();
	bool bDrawTranslucentRenderables = r_drawtranslucentrenderables.GetBool();

	unsigned long drawFlags = BuildDrawFlags( false, drawUnderWater, drawAboveWater, false );
	
	MEASURE_TIMED_STAT( CS_TRANSLUCENT_RENDER_TIME );

	int lastFogVolume = -1;

	if( ShouldDrawEntities() && bDrawTranslucentRenderables )
	{
		DrawParticleSingletons();
		
		CRenderList::CEntry *pEntities = renderList.m_RenderGroups[RENDER_GROUP_TRANSLUCENT_ENTITY];
		int iCurTranslucentEntity = renderList.m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY] - 1;

		while( iCurTranslucentEntity >= 0 )
		{
			// Seek the current leaf up to our current translucent-entity leaf.
			int iNextLeaf = pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf;

			if (bDrawTranslucentWorld)
			{
				DrawTranslucentWorldInLeaves( iCurLeaf, iNextLeaf, info.m_pLeafList, drawFlags, nDetailLeafCount, pDetailLeafList );
				iCurLeaf = iNextLeaf - 1;
			}

			// Draw the entities in this leaf.
			int iThisLeaf = pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf;
			if( info.m_pLeafFogVolume[iThisLeaf] != lastFogVolume )
			{
//				render->SetFogVolumeState( info.m_pLeafFogVolume[iThisLeaf] );
				lastFogVolume = info.m_pLeafFogVolume[iThisLeaf];
			}

			// Draw all the translucent entities with this leaf.
			while( pEntities[iCurTranslucentEntity].m_iWorldListInfoLeaf == iThisLeaf && iCurTranslucentEntity >= 0 )
			{
				// garymcthack - this all should move inside of the conditional below!
				static int lastCopyFrameNumber = -1;
				if( ( cl_alwayscopyframebuffertotexture.GetInt() || m_FrameNumber != lastCopyFrameNumber ) &&
					pEntities[iCurTranslucentEntity].m_pRenderable->UsesFrameBufferTexture() &&
					cl_copyframebuffertotexture.GetInt() )
				{
					MEASURE_TIMED_STAT( CS_COPYFB_TIME );
					ClientStats().IncrementCountedStat( CS_NUM_COPYFB, 1 );
					UpdateRefractTexture();
					lastCopyFrameNumber = m_FrameNumber;
				}

				Assert( ClientLeafSystem()->IsRenderableUnderWater( info, &pEntities[iCurTranslucentEntity] ) ||
						ClientLeafSystem()->IsRenderableAboveWater( info, &pEntities[iCurTranslucentEntity] ) );
				bool underWater = ClientLeafSystem()->IsRenderableUnderWater( info, &pEntities[iCurTranslucentEntity] );
				bool aboveWater = ClientLeafSystem()->IsRenderableAboveWater( info, &pEntities[iCurTranslucentEntity] );
				if( ( drawUnderWater && underWater ) || ( drawAboveWater && aboveWater ) )
				{
					if( g_bWaterReflectionCull )
					{
						Vector mins, maxs;
						pEntities[iCurTranslucentEntity].m_pRenderable->GetRenderBounds( mins, maxs );
						float minz = mins.z + pEntities[iCurTranslucentEntity].m_pRenderable->GetRenderOrigin().z;
						if( minz > g_bWaterCullHeight )
						{
							--iCurTranslucentEntity;
							continue;
						}
					}
					DrawTranslucentRenderable( pEntities[iCurTranslucentEntity].m_pRenderable, 
						(pEntities[iCurTranslucentEntity].m_TwoPass != 0) );
				}
				--iCurTranslucentEntity;
			}
		}
	}

	// Draw the rest of the surfaces in world leaves
	if (bDrawTranslucentWorld)
	{
		DrawTranslucentWorldInLeaves( iCurLeaf, 0, info.m_pLeafList, drawFlags, nDetailLeafCount, pDetailLeafList );

		// Draw any queued-up detail props from previously visited leaves
		DetailObjectSystem()->RenderTranslucentDetailObjects( CurrentViewOrigin(), CurrentViewForward(), nDetailLeafCount, pDetailLeafList );
	}

	// Reset the blend state.
	render->SetBlend( 1 );
}



void CViewRender::SetRenderTargetAndView( CViewSetup &view, float waterHeight, int flags )
{
	float spread = 5.0f;
//	float waterHeight = -64.0f; // test_water
//	float waterHeight = 712.0f; // water2
//	float waterHeight = -1152.0f; // test_water2
	float origWaterHeight = waterHeight;
	if( flags & DF_FUDGE_UP )
	{
		waterHeight += spread;
	}
	else
	{
		waterHeight -= spread;
	}
	if( flags & DF_RENDER_REFRACTION )
	{
		ITexture *pTexture = GetWaterRefractionTexture();
		//pTexture->IncrementReferenceCount();
		materials->SetFogZ( waterHeight );
		materials->SetHeightClipZ( waterHeight );
		Assert( pTexture );
		if( pTexture )
		{
			view.width = pTexture->GetActualWidth();
			view.height = pTexture->GetActualHeight();
			materials->SetRenderTarget( pTexture );
		}
	}
	else if( flags & DF_RENDER_REFLECTION )
	{
		ITexture *pTexture = GetWaterReflectionTexture();
		//pTexture->IncrementReferenceCount();
		Assert( pTexture );
		materials->SetFogZ( waterHeight );
		materials->SetHeightClipZ( waterHeight );
		if( pTexture )
		{
			view.width = pTexture->GetActualWidth();
			view.height = pTexture->GetActualHeight();
			view.angles[0] = -view.angles[0];
			view.angles[2] = -view.angles[2];
			view.origin[2] -= 2.0f * ( view.origin[2] - (origWaterHeight));
			materials->SetRenderTarget( pTexture );
		}
	}
	else
	{
		materials->SetRenderTarget( m_pViewDrawSceneRenderTarget );
	}
}

static ConVar mat_clipz( "mat_clipz", "1" );

void CViewRender::WaterDrawHelper( 
	const CViewSetup &view, 
	WorldListInfo_t &info, 
	CRenderList &renderList, 
	float waterHeight, 
	int flags,
	int iForceViewLeaf )
{
	bool bClearDepth = ( flags & DF_CLEARDEPTH ) != 0;
	bool bClearColor = ( flags & DF_CLEARCOLOR ) != 0;

	CViewSetup tmpView;
	const CViewSetup *pView = &view;
	if( bClearColor || bClearDepth )
	{
		tmpView = view;
		tmpView.clearColor = bClearColor;
		tmpView.clearDepth = bClearDepth;
		SetRenderTargetAndView( tmpView, waterHeight, flags );
		render->ViewSetup3D( &tmpView, m_Frustum );
		pView = &tmpView;
	}

	if( flags & DF_BUILDWORLDLISTS )
	{
		BuildWorldRenderLists( pView, info, renderList, bClearDepth, iForceViewLeaf );
	}
	
	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	// This sucks. . need to do an implementation that uses user clip planes for 
	// hardware that has it.
	MaterialHeightClipMode_t clipMode = MATERIAL_HEIGHTCLIPMODE_DISABLE;
	if( ( flags & DF_CLIP_Z ) && mat_clipz.GetBool() )
	{
		if( flags & DF_CLIP_BELOW )
		{
		//	materials->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT );
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT;
		}
		else
		{
		//	materials->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_RENDER_BELOW_HEIGHT );
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_BELOW_HEIGHT;
		}
	}
//	else
//	{
//		materials->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_DISABLE );
//	}

	materials->SetHeightClipMode( clipMode );

	if (!bClearDepth)
		flags &= ~DF_DRAWSKYBOX;

	bool renderUnderWater = (flags & DF_RENDER_UNDERWATER) != 0;
	bool renderAboveWater = (flags & DF_RENDER_ABOVEWATER) != 0;

//	ITexture *pSaveFrameBufferCopyTexture = materials->GetFrameBufferCopyTexture();
	ITexture *pSaveFrameBufferCopyTexture = materials->GetFrameBufferCopyTexture(0);
//	materials->SetFrameBufferCopyTexture( GetPowerOfTwoFrameBufferTexture() );
	materials->SetFrameBufferCopyTexture( GetPowerOfTwoFrameBufferTexture(), 0 );

	DrawWorld( info, renderList, flags );
	if( flags & DF_DRAW_ENTITITES )
	{
		DrawOpaqueRenderablesInWater( info, renderList, renderUnderWater, renderAboveWater );
		DrawTranslucentRenderablesInWaterV2( info, renderList, renderUnderWater, renderAboveWater );
	}

	materials->SetFrameBufferCopyTexture( pSaveFrameBufferCopyTexture );
	materials->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_DISABLE );
}

//-----------------------------------------------------------------------------
// Returns true if the view plane intersects the water
//-----------------------------------------------------------------------------
bool DoesViewPlaneIntersectWater( float waterZ, int leafWaterDataID )
{
	if ( leafWaterDataID == -1 )
		return false;

	VMatrix viewMatrix, projectionMatrix, viewProjectionMatrix, inverseViewProjectionMatrix;
	materials->GetMatrix( MATERIAL_VIEW, &viewMatrix );
	materials->GetMatrix( MATERIAL_PROJECTION, &projectionMatrix );
	MatrixMultiply( projectionMatrix, viewMatrix, viewProjectionMatrix );
	MatrixInverseGeneral( viewProjectionMatrix, inverseViewProjectionMatrix );

	Vector mins, maxs;
	ClearBounds( mins, maxs );
	Vector testPoint[4];
	testPoint[0].Init( -1.0f, -1.0f, 0.0f );
	testPoint[1].Init( -1.0f, 1.0f, 0.0f );
	testPoint[2].Init( 1.0f, -1.0f, 0.0f );
	testPoint[3].Init( 1.0f, 1.0f, 0.0f );
	int i;
	bool bAbove = false;
	bool bBelow = false;
	float fudge = 7.0f;
	for( i = 0; i < 4; i++ )
	{
		Vector worldPos;
		Vector3DMultiplyPositionProjective( inverseViewProjectionMatrix, testPoint[i], worldPos );
		AddPointToBounds( worldPos, mins, maxs );
//		Warning( "viewplanez: %f waterZ: %f\n", worldPos.z, waterZ );
		if( worldPos.z + fudge > waterZ )
		{
			bAbove = true;
		}
		if( worldPos.z - fudge < waterZ )
		{
			bBelow = true;
		}
	}

	// early out if the near plane doesn't cross the z plane of the water.
	if( !( bAbove && bBelow ) )
		return false;

	Vector vecFudge( fudge, fudge, fudge );
	mins -= vecFudge;
	maxs += vecFudge;
	
	// the near plane does cross the z value for the visible water volume.  Call into
	// the engine to find out if the near plane intersects the water volume.
	return render->DoesBoxIntersectWaterVolume( mins, maxs, leafWaterDataID );
}

void CViewRender::ViewDrawScene_EyeAboveWater( bool drawSkybox, const CViewSetup &view, int visibleFogVolume, int visibleFogVolumeLeaf, bool bReflect, bool bRefract, bool bReflectEntities )
{
	VPROF( "CViewRender::ViewDrawScene_EyeAboveWater" );

	WorldListInfo_t info;	
	CRenderList renderList;

	float waterHeight = render->GetWaterHeight( visibleFogVolume );

	// eye is outside of water
	
	// render the reflection
	if( bReflect )
	{
		if( r_watercullenable.GetBool() )
		{
			g_bWaterReflectionCull = true;
			g_bWaterCullHeight = r_watercullheight.GetFloat();
		}

		int flags = DF_RENDER_REFLECTION | DF_CLIP_Z | DF_CLIP_BELOW | 

		// VXP: Fix for buggy water at e3_techdemo_5
	//	int flags = DF_RENDER_REFLECTION | DF_CLIP_BELOW | 
			DF_RENDER_ABOVEWATER | DF_CLEARDEPTH | DF_CLEARCOLOR | DF_BUILDWORLDLISTS;
		flags |= DF_DRAWSKYBOX;
		if( bReflectEntities )
		{
			flags |= DF_DRAW_ENTITITES;
		}
		EnableWorldFog();
		WaterDrawHelper( view, info, renderList, waterHeight, flags, visibleFogVolumeLeaf );
		g_bWaterReflectionCull = false;
		
		materials->Flush();
	}
	render->SetFogVolumeState( visibleFogVolume, true );
	
	// render refraction
	if ( bRefract )
	{
		int flags = DF_RENDER_REFRACTION | DF_CLIP_Z | DF_RENDER_UNDERWATER | 

		// VXP: Fix for buggy water at e3_techdemo_6
		// VXP: TODO: Need to fix clip planes properly!
		// Changing normal planes to clip planes
		// (EnableFastClip( true ); SetFastClipPlane(plane); in CMaterialSystem::UpdateHeightClipUserClipPlane)
		// can help, but then clip plane is showing underwater.
	//	int flags = DF_RENDER_REFRACTION | DF_RENDER_UNDERWATER | 
			DF_CLEARDEPTH | DF_CLEARCOLOR | DF_FUDGE_UP | DF_BUILDWORLDLISTS | DF_DRAW_ENTITITES;
		WaterDrawHelper( view, info, renderList, waterHeight, flags );
	}
	
	materials->Flush();
	EnableWorldFog();
	
	// render the world
	int flags = DF_RENDER_ABOVEWATER | DF_RENDER_WATER | DF_CLEARDEPTH | DF_DRAW_ENTITITES;
//	int flags = DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER | DF_RENDER_WATER | DF_CLEARDEPTH | DF_DRAW_ENTITITES;
	
	// Normally, the refraction pass builds our world lists since it's from the same viewpoint.
	// If we're not drawing refraction, then we must setup the world lists since they aren't set up yet.
	if ( !bRefract )
		flags |= DF_BUILDWORLDLISTS;
	
	if (drawSkybox)
		flags |= DF_DRAWSKYBOX;

	WaterDrawHelper( view, info, renderList, waterHeight, flags );
}

void CViewRender::ViewDrawScene_EyeUnderWater( bool drawSkybox, const CViewSetup &view, int visibleFogVolume, bool bReflect, bool bRefract, bool bReflectEntities )
{
	VPROF( "CViewRender::ViewDrawScene_EyeUnderWater" );

	WorldListInfo_t info;	
	CRenderList renderList;
	float waterHeight = render->GetWaterHeight( visibleFogVolume );

	// render refraction (out of water)
	int flags = DF_RENDER_REFRACTION | DF_CLIP_Z | DF_CLIP_BELOW | 
		DF_RENDER_ABOVEWATER | DF_CLEARDEPTH | DF_CLEARCOLOR | DF_BUILDWORLDLISTS | 
		DF_DRAW_ENTITITES;
//	if (drawSkybox)
		flags |= DF_DRAWSKYBOX;

//	if ( mat_clipz.GetInt() == 2 )
//		flags |= DF_CLIP_Z | DF_CLIP_BELOW;

	EnableWorldFog();
	WaterDrawHelper( view, info, renderList, waterHeight, flags );

	render->SetFogVolumeState( visibleFogVolume, false );
	
	// render the world underwater
	flags = DF_RENDER_UNDERWATER | DF_RENDER_WATER | DF_CLEARDEPTH | DF_DRAW_ENTITITES;
	WaterDrawHelper( view, info, renderList, waterHeight, flags );

	materials->FogMode( MATERIAL_FOG_NONE );
}

void CViewRender::ViewDrawScene_NoWater( bool drawSkybox, const CViewSetup &view, bool bCheapWater )
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	WorldListInfo_t info;	
	CRenderList renderList;

	float waterHeight = 0.0f;

	EnableWorldFog();

	int flags = DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER | DF_CLEARDEPTH | DF_BUILDWORLDLISTS | DF_DRAW_ENTITITES;
	if( bCheapWater )
	{
		flags |= DF_RENDER_WATER;
	}
	if (drawSkybox)
	{
		flags |= DF_DRAWSKYBOX;
	}

	WaterDrawHelper( view, info, renderList, waterHeight, flags );
}

void CViewRender::SetCheapWaterStartDistance( float flCheapWaterStartDistance )
{
	m_flCheapWaterStartDistance = flCheapWaterStartDistance;
}

void CViewRender::SetCheapWaterEndDistance( float flCheapWaterEndDistance )
{
	m_flCheapWaterEndDistance = flCheapWaterEndDistance;
}

void CViewRender::GetWaterLODParams( float &flCheapWaterStartDistance, float &flCheapWaterEndDistance )
{
	flCheapWaterStartDistance = m_flCheapWaterStartDistance;
	flCheapWaterEndDistance = m_flCheapWaterEndDistance;
}

static void CheapWaterStart_f( void )
{
	if( engine->Cmd_Argc() == 2 )
	{
		float dist = atof( engine->Cmd_Argv( 1 ) );
		view->SetCheapWaterStartDistance( dist );
	}
	else
	{
		float start, end;
		view->GetWaterLODParams( start, end );
		Warning( "r_cheapwaterstart: %f\n", start );
	}
}

static void CheapWaterEnd_f( void )
{
	if( engine->Cmd_Argc() == 2 )
	{
		float dist = atof( engine->Cmd_Argv( 1 ) );
		view->SetCheapWaterEndDistance( dist );
	}
	else
	{
		float start, end;
		view->GetWaterLODParams( start, end );
		Warning( "r_cheapwaterend: %f\n", end );
	}
}

static void ScreenOverlay_f( void )
{
	if( engine->Cmd_Argc() == 2 )
	{
		bool bFound;
		if ( !Q_stricmp( "off", engine->Cmd_Argv(1) ) )
		{
			view->SetScreenOverlayMaterial( NULL );
		}
		else
		{
			IMaterial *pMaterial = materials->FindMaterial( engine->Cmd_Argv(1), &bFound, false );
			if ( bFound )
			{
				view->SetScreenOverlayMaterial( pMaterial );
			}
			else
			{
				view->SetScreenOverlayMaterial( NULL );
			}
		}
	}
	else
	{
		IMaterial *pMaterial = view->GetScreenOverlayMaterial();
		Warning( "r_screenoverlay: %s\n", pMaterial ? pMaterial->GetName() : "off" );
	}
}

static ConCommand r_cheapwaterstart( "r_cheapwaterstart", CheapWaterStart_f );
static ConCommand r_cheapwaterend( "r_cheapwaterend", CheapWaterEnd_f );
static ConCommand r_screenspacematerial( "r_screenoverlay", ScreenOverlay_f );
