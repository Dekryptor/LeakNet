/***
*
*	Copyright (c) 1998, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
****/

#ifndef INCLUDED_STUDIOMODEL
#define INCLUDED_STUDIOMODEL

#include "mathlib.h"
#include "studio.h"
#include "mouthinfo.h"
#include "UtlLinkedList.h"
#include "UtlSymbol.h"

#define DEFAULT_BLEND_TIME 0.2

typedef struct IFACE_TAG IFACE;
typedef struct IMESH_TAG IMESH;
typedef struct ResolutionUpdateTag ResolutionUpdate;
typedef struct FaceUpdateTag FaceUpdate;
class IMaterial;


class IStudioPhysics;
class CPhysmesh;
struct hlmvsolid_t;
struct constraint_ragdollparams_t;
class IStudioRender;


class AnimationLayer
{
public:
	float					m_cycle;			// 0 to 1 animation playback index
	int						m_sequence;			// sequence index
	float					m_weight;
	float					m_playbackrate;
	int						m_priority;			// lower priorities get layered first
};



class StudioModel
{
public:
	// memory handling, uses calloc so members are zero'd out on instantiation
    static void						*operator new( size_t stAllocateBlock );
	static void						operator delete( void *pMem );

	static void				Init( void );
	static void				Shutdown( void ); // garymcthack - need to call this.
	static void				UpdateViewState( const Vector& viewOrigin,
											 const Vector& viewRight,
											 const Vector& viewUp,
											 const Vector& viewPlaneNormal );

	static void						ReleaseStudioModel( void );
	static void						RestoreStudioModel( void );

	char const						*GetFileName( void ) { return m_pModelName; }

	IStudioRender				    *GetStudioRender() { return m_pStudioRender; }

	static void UpdateStudioRenderConfig( bool bFlat, bool bWireframe, bool bNormals );
	studiohdr_t						*getStudioHeader ( void ) const { return m_pstudiohdr; }
	studiohdr_t						*getAnimHeader (int i) const { return m_panimhdr[i]; }

	virtual void					ModelInit( void ) { }

	bool							IsModelLoaded() const { return m_pstudiohdr != 0; }

	const matrix3x4_t				*BoneToWorld( int nBoneIndex ) const { return &m_pBoneToWorld[nBoneIndex]; } // VXP

	void							FreeModel ( bool bReleasing );
	bool							LoadModel( const char *modelname );
	virtual bool					PostLoadModel ( const char *modelname );
	bool							SaveModel ( const char *modelname );

	virtual int						DrawModel( bool mergeBones = false );

	virtual void					AdvanceFrame( float dt );
	float							GetCycle( void );
	float							GetFrame( void );
	int								GetMaxFrame( void );
	int								SetFrame( int frame );
	float							GetCycle( int iLayer );
	float							GetFrame( int iLayer );
	int								GetMaxFrame( int iLayer );
	int								SetFrame( int iLayer, int frame );

	void							ExtractBbox( Vector &mins, Vector &maxs );

	void							SetBlendTime( float blendtime );
	int								LookupSequence( const char *szSequence );
	int								SetSequence( int iSequence );
	int								SetSequence( const char *szSequence );
	void							ClearOverlaysSequences( void );
	void							ClearAnimationLayers( void );
	int								GetNewAnimationLayer( int iPriority = 0 );

	int								SetOverlaySequence( int iLayer, int iSequence, float flWeight );
	float							SetOverlayRate( int iLayer, float flCycle, float flFrameRate );
	void							StartBlending( void );

	float							GetTransitionAmount( void );
	int								GetSequence( void );
	void							GetSequenceInfo( int iSequence, float *pflFrameRate, float *pflGroundSpeed );
	void							GetSequenceInfo( float *pflFrameRate, float *pflGroundSpeed );
	float							GetFPS( int iSequence );
	float							GetFPS( );
	float							GetDuration( int iSequence );
	float							GetDuration( );
	bool							GetSequenceLoops( int iSequence );
	void							GetMovement( float dt, Vector &vecPos, QAngle &vecAngles );
	void							GetMovement( float prevCycle, float currCycle, Vector &vecPos, QAngle &vecAngles );
	void							GetSeqAnims( int iSequence, mstudioanimdesc_t *panim[4], float *pweights );
	void							GetSeqAnims( mstudioanimdesc_t *panim[4], float *pweights );
	float							GetGroundSpeed( void );
	bool							IsHidden( int iSequence ); // VXP

	float							SetController( int iController, float flValue );

	int								LookupPoseParameter( char const *szName );
	float							SetPoseParameter( int iParameter, float flValue );
	float							SetPoseParameter( char const *szName, float flValue );
	float							GetPoseParameter( char const *szName );
	float							GetPoseParameter( int iParameter );
	bool 							GetPoseParameterRange( int iParameter, float *pflMin, float *pflMax );

	int								LookupAttachment( char const *szName );

	int								SetBodygroup( int iGroup, int iValue );
	int								SetSkin( int iValue );
	int								FindBone( const char *pName );

	int								LookupFlexController( char *szName );
	void							SetFlexController( char *szName, float flValue );
	void							SetFlexController( int iFlex, float flValue );
	float							GetFlexController( char *szName );
	float							GetFlexController( int iFlex );

	// void							CalcBoneTransform( int iBone, Vector pos[], Quaternion q[], matrix3x4_t& bonematrix );

	void							UpdateBoneChain( Vector pos[], Quaternion q[], int iBone, matrix3x4_t *pBoneToWorld );
	void							SetViewTarget( void ); // ???
	// void							SetHeadPosition( void ); // ???
	void							GetBodyPoseParametersFromFlex( void );
	void							SetHeadPosition( Vector pos[], Quaternion q[] );
	float							SetHeadPosition( matrix3x4_t& attToWorld, Vector const &vTargetPos, float dt );
	void							CalcDefaultView( mstudioattachment_t *patt, Vector pos[], Quaternion q[], Vector &vDefault );

	int								GetNumLODs() const;
	float							GetLODSwitchValue( int lod ) const;
	void							SetLODSwitchValue( int lod, float switchValue );
	
	void							scaleMeshes (float scale);
	void							scaleBones (float scale);

	// Physics
	void							OverrideBones( bool *override );
	int								Physics_GetBoneCount( void );
	const char *					Physics_GetBoneName( int index );
	int								Physics_GetBoneIndex( const char *pName );
	void							Physics_GetData( int boneIndex, hlmvsolid_t *psolid, constraint_ragdollparams_t *pConstraint ) const;
	void							Physics_SetData( int boneIndex, const hlmvsolid_t *psolid, constraint_ragdollparams_t const *pConstraint );
	void							Physics_SetPreview( int previewBone, int axis, float t );
	float							Physics_GetMass( void );
	void							Physics_SetMass( float mass );
	char							*Physics_DumpQC( void );

protected:
	// entity settings
	Vector							m_origin;
public:
	QAngle							m_angles;	
protected:
	int								m_bodynum;			// bodypart selection	
	int								m_skinnum;			// skin group selection
	float							m_controller[4];	// bone controllers

public:
	CMouthInfo						m_mouth;

protected:
	char							*m_pModelName;		// model file name

	// bool							m_owntexmodel;		// do we have a modelT.mdl ?

	// Previouse sequence data
	float							m_blendtime;
	float							m_sequencetime;
	int								m_prevsequence;
	float							m_prevcycle;

	float							m_dt;

	// Blending info

	// Gesture,Sequence layering state
#define MAXSTUDIOANIMLAYERS			8
	AnimationLayer					m_Layer[MAXSTUDIOANIMLAYERS];
	int								m_iActiveLayers;

public:
	float							m_cycle;			// 0 to 1 animation playback index
protected:
	int								m_sequence;			// sequence index
	float							m_poseparameter[MAXSTUDIOPOSEPARAM];		// intra-sequence blending
	float							m_weight;

	// internal data
	studiohdr_t						*m_pstudiohdr;
	mstudiomodel_t					*m_pmodel;
	studiohwdata_t					m_HardwareData;	

public:
	// I'm saving this as internal data because we may add or remove hitboxes
	// I'm using a utllinkedlist so hitbox IDs remain constant on add + remove
	typedef CUtlLinkedList<mstudiobbox_t, unsigned short> Hitboxes_t;
	CUtlVector< Hitboxes_t >		m_HitboxSets;
	struct hbsetname_s
	{
		char name[ 128 ];
	};

	CUtlVector< hbsetname_s >		m_HitboxSetNames;

	CUtlVector< CUtlSymbol >		m_SurfaceProps;
protected:

	// class data
	static IStudioRender			*m_pStudioRender;
	static Vector					*m_AmbientLightColors;
	static Vector					*m_TotalLightColors;

	// Added data
	// IMESH						*m_pimesh;
	// VertexUpdate					*m_pvertupdate;
	// FaceUpdate					*m_pfaceupdate;
	IFACE							*m_pface;

	// studiohdr_t					*m_ptexturehdr;
	studiohdr_t						*m_panimhdr[32];

	Vector4D						m_adj;				// FIX: non persistant, make static

public:
	IStudioPhysics					*m_pPhysics;
private:
	int								m_physPreviewBone;
	int								m_physPreviewAxis;
	float							m_physPreviewParam;
	float							m_physMass;

public:
	mstudioseqdesc_t				*GetSeqDesc( int seq );
private:
	mstudioanimdesc_t				*GetAnimDesc( int anim );
	mstudioanim_t					*GetAnim( int anim );

	void							Transform( Vector const &in1, mstudioboneweight_t const *pboneweight, Vector &out1 );
	void							Rotate( Vector const &in1, mstudioboneweight_t const *pboneweight, Vector &out1 );
	void							DrawPhysmesh( CPhysmesh *pMesh, int boneIndex, IMaterial *pMaterial, float *color );
	void							DrawPhysConvex( CPhysmesh *pMesh, IMaterial *pMaterial );

	void							SetupLighting( void );

	virtual void					SetupModel ( int bodypart );

private:
	float							m_flexweight[MAXSTUDIOFLEXCTRL];
//	matrix3x4_t						m_pBoneToWorld[MAXSTUDIOBONES]; // VXP
	matrix3x4_t						*m_pBoneToWorld; // VXP
public:
	virtual int						FlexVerts( mstudiomesh_t *pmesh );
	virtual void					RunFlexRules( void );
	virtual int						BoneMask( void ); // VXP
	virtual void					SetUpBones ( bool mergeBones );

	int								GetLodUsed( void );
	float							GetLodMetric( void );

	const char						*GetKeyValueText( int iSequence );

private:
	// Drawing helper methods
	void DrawBones( );
	void DrawAttachments( );
	void DrawEditAttachment();
	void DrawHitboxes();
	void DrawPhysicsModel( );

public:
	// generic interface to rendering?
	void drawBox (Vector const *v, float const * color );
	void drawWireframeBox (Vector const *v, float const* color );
	void drawTransform( matrix3x4_t& m, float flLength = 4 );
	void drawLine( Vector const &p1, Vector const &p2, int r = 0, int g = 0, int b = 255 );
	void drawTransparentBox( Vector const &bbmin, Vector const &bbmax, const matrix3x4_t& m, float const *color, float const *wirecolor );

private:
	int						m_LodUsed;
	float					m_LodMetric;
};

extern Vector g_vright;		// needs to be set to viewer's right in order for chrome to work
extern StudioModel *g_pStudioModel;
extern StudioModel *g_pStudioExtraModel[4]; // VXP



#endif // INCLUDED_STUDIOMODEL