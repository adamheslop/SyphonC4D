/////////////////////////////////////////////////////////////
// CINEMA 4D SDK                                           //
/////////////////////////////////////////////////////////////
// (c) 1989-2004 MAXON Computer GmbH, all rights reserved  //
/////////////////////////////////////////////////////////////

#include "lib_clipmap.h"
#include "SyphonC4DServer.h"
#include "SyphonPlugin.h"
#include "c4d.h"
#include "r13.h"
#include "c4d_symbols.h"
#include "main.h"
#include <sstream>
#include <ios>
#include <iomanip>
#include <vector>
#include <map>
#include "c4d_gl.h"

#include <stdarg.h>
void _printf(const char *format,...);
void _printf(const char *format,...)
{
#ifndef RELEASE
	char fmt[1024];
	sprintf(fmt,"--- Syphon: %s",format);
	va_list arp;
	va_start(arp,format);
	char buf[1024];
	vsprintf_safe(buf,sizeof(buf),fmt,arp);
	GePrint(buf);
	va_end(arp);
#endif
}

// Syphon must be static because of multithreading
class SyphonVP;
static SyphonVP * thePlugin = NULL;


////////////////////////////////////
//
// PLUGIN CLASS
//
class SyphonVP : public VideoPostData
{
	INSTANCEOF(SyphonVP,VideoPostData)

public:

	Bool Init(GeListNode *node);
	void Free(GeListNode *node);
	Bool GetDEnabling(GeListNode *node, const DescID &id,const GeData &t_data,DESCFLAGS_ENABLE flags,const BaseContainer *itemdesc);
	static NodeData *Alloc(void) {
#ifdef C4DR13
		return gNew SyphonVP;
#else
		return NewObjClear(SyphonVP);
#endif
	}
	void AllocateBuffers(BaseVideoPost* node, Render* render, BaseDocument* doc);

	VIDEOPOSTINFO GetRenderInfo(BaseVideoPost* node);
	RENDERRESULT Execute(BaseVideoPost *node, VideoPostStruct *vps);
	void ExecuteLine(BaseVideoPost* node, PixelPost* pp);
    
    
    
    // -------------------------------||Adam adding things ||----------------
    
    
    
    virtual Bool GlDraw(BaseVideoPost* node, BaseDraw* bd, GlFrameBuffer* fbuf, Int32 colortex, Int32 depthtex, VIDEOPOST_GLDRAW flags);
    virtual VIDEOPOST_GLINFO GetGlInfo(BaseVideoPost* node, BaseDocument* doc, BaseDraw* bd);
    virtual Bool RenderEngineCheck(BaseVideoPost* node, Int32 id);
    
protected:
	static void* AllocCgDescription();
	static void FreeCgDescription(void* pData);
	static Bool ReadCgDescription(GlReadDescriptionData* pFile, void* pData);
	static Bool WriteCgDescription(GlWriteDescriptionData* pFile, const void* pData);
    // --------------------------------------------------------------------
    

private:

	// plugin options
	Bool		bSyphonEnabled;
	Int32		mSyphonMode;

	// scene info
	bool			bAlpha;
	Vector			res;				// Frame resolution
	CameraObject	*mCamera;

	// syphon lock
	pthread_mutex_t _lineLock;

};


/////////////////////////////////////////////
//
// VIDEOPOST
//
Bool SyphonVP::Init(GeListNode *node)
{
	thePlugin = this;

	pthread_mutex_init(&_lineLock, NULL);

	BaseVideoPost	*pp = (BaseVideoPost*)node;
	BaseContainer	*nodeData = pp->GetDataInstance();

	nodeData->SetBool(		VP_SYPHON_ENABLED,		true);
	nodeData->SetInt32(		VP_SYPHON_MODE,			VP_SYPHON_MODE_REALTIME);

	return true;
}

void SyphonVP::Free(GeListNode *node)
{
	if ( thePlugin )
	{
		thePlugin = NULL;
		pthread_mutex_destroy(&_lineLock);
	}
}

Bool SyphonVP::GetDEnabling(GeListNode *node, const DescID &id,const GeData &t_data,DESCFLAGS_ENABLE flags,const BaseContainer *itemdesc)
{
	bool enabled = true;
	BaseContainer *data = ((BaseObject*)node)->GetDataInstance();
	switch (id[0].id)
	{
		case VP_SYPHON_MODE:
			enabled = data->GetBool(VP_SYPHON_ENABLED);
			break;
	}
	return enabled;
}

void SyphonVP::AllocateBuffers(BaseVideoPost* node, Render* render, BaseDocument* doc)

{
	// Get camera
	mCamera = NULL;
	BaseDraw *bd_render = doc->GetRenderBaseDraw();		// the view set as "Render View"
	BaseDraw *bd_active = doc->GetActiveBaseDraw();		// The current view
	Int32 proj = bd_active->GetProjection();
	BaseDraw *bd = ( proj == Pperspective ? bd_active : bd_render );

	if ( bd )
	{
		mCamera = (CameraObject*) bd->GetSceneCamera(doc);		// the active camera
		if ( ! mCamera )
			mCamera = (CameraObject*) bd->GetEditorCamera();		// the defaut camera
	}

	if ( mCamera == NULL )
	{
		SyphonC4DServer::Instance().shutdown();
		_printf("Can't find camera! Syphon Server Disabled.");
	}
}

VIDEOPOSTINFO SyphonVP::GetRenderInfo(BaseVideoPost* node)
{
	return VIDEOPOSTINFO_EXECUTELINE;
}

RENDERRESULT SyphonVP::Execute(BaseVideoPost *node, VideoPostStruct *vps)
{
	BaseDocument *doc = vps->doc;
	BaseContainer *nodeData = node->GetDataInstance();

	// For each RENDER
	if (vps->vp==VIDEOPOSTCALL_FRAMESEQUENCE && vps->open)
	{
		BaseContainer renderData = ( vps->render ? vps->render->GetRenderData() : doc->GetActiveRenderData()->GetData() );

		bSyphonEnabled	= nodeData->GetBool(VP_SYPHON_ENABLED);
		mSyphonMode		= nodeData->GetInt32(VP_SYPHON_MODE);
		bAlpha			= renderData.GetBool(RDATA_ALPHACHANNEL);
		res.x			= renderData.GetFloat(RDATA_XRES);
		res.y			= renderData.GetFloat(RDATA_YRES);

		// Cinema4D appears to be sending invalid data on alpha channel
		// so no alpha for this server
		bAlpha = false;

		if ( bSyphonEnabled )
		{
			SyphonC4DServer::Instance().start("SyphonC4D");
			SyphonC4DServer::Instance().setSize( res.x, res.y, bAlpha );
		}
		else
		{
			SyphonC4DServer::Instance().shutdown();
		}
	}
	// Executed after each frame
	else if (vps->vp==VIDEOPOSTCALL_FRAME && ! vps->open)
	{
		// Publish full frame to Syphon Server
		if ( bSyphonEnabled )//&& mSyphonMode == VP_SYPHON_MODE_FRAME )
		{
			VPBuffer * buf = vps->render->GetBuffer(VPBUFFER_RGBA,0);
			SyphonC4DServer::Instance().publishBuffer( buf );
		}
	}

	return RENDERRESULT_OK;
}


void SyphonVP::ExecuteLine(BaseVideoPost* node, PixelPost* pp)
{
	// Publish line to Syphon Server
	if ( bSyphonEnabled && mSyphonMode == VP_SYPHON_MODE_REALTIME && pp->valid_line )
	{
		// several processes may be rendering
		// but only one may be publishing
		pthread_mutex_lock(&_lineLock);
		SyphonC4DServer::Instance().publishLine( pp );
		pthread_mutex_unlock(&_lineLock);
	}
}




//-----------------------|| Adam adding things ||------------------------------

struct VPInvertDescData
{
	GlString															 strTexsize, strTexture;
	GlProgramParameter										 paramTexsize, paramTexture;
	Int32																	 lVectorCount;
	const GlVertexBufferVectorInfo* const* ppVectorInfo;
};

void* SyphonVP::AllocCgDescription()
{
	return NewObjClear(VPInvertDescData);
    
}


void SyphonVP::FreeCgDescription(void* pData)
{
	VPInvertDescData* pDescData = (VPInvertDescData*)pData;
	DeleteObj(pDescData);
}

Bool SyphonVP::ReadCgDescription(GlReadDescriptionData* pFile, void* pData)
{
	VPInvertDescData* pDesc = (VPInvertDescData*)pData;
	if (!GlProgramFactory::ReadParameter(pFile, pDesc->paramTexsize))
		return false;
	if (!GlProgramFactory::ReadParameter(pFile, pDesc->paramTexture))
		return false;
	return true;
}

Bool SyphonVP::WriteCgDescription(GlWriteDescriptionData* pFile, const void* pData)
{
	const VPInvertDescData* pDesc = (const VPInvertDescData*)pData;
	if (!GlProgramFactory::WriteParameter(pFile, pDesc->paramTexsize))
		return false;
	if (!GlProgramFactory::WriteParameter(pFile, pDesc->paramTexture))
		return false;
	return true;
}

VIDEOPOST_GLINFO SyphonVP::GetGlInfo(BaseVideoPost* node, BaseDocument* doc, BaseDraw* bd)
{
	return VIDEOPOST_GLINFO_DRAW;
}

Bool SyphonVP::RenderEngineCheck(BaseVideoPost* node, Int32 id)
{
	// the following render engines are not supported by this effect
	if (id == RDATA_RENDERENGINE_PREVIEWSOFTWARE ||
        id == RDATA_RENDERENGINE_CINEMAN)
		return false;
    
	return true;
}

#define VP_INVERT_IMAGE_SHADER_VERSION 0

Bool SyphonVP::GlDraw(BaseVideoPost* node, BaseDraw* bd, GlFrameBuffer* fbuf, Int32 colortex, Int32 depthtex, VIDEOPOST_GLDRAW flags)
{
    
    
    C4DGLuint ind = fbuf->GetTexture(0,C4D_FRAMEBUFFER_COLOR);
    
    // Get the dimensions of the framebuffer
    UINT f_width;
    UINT f_height;
    
    fbuf->GetSize(C4D_FRAMEBUFFER_COLOR, f_width, f_height, true);
    
    String fb_width  = String::UIntToString(f_width);
    String fb_height = String::UIntToString(f_height);
    
    GePrint("Frame buffer dimensions | X : " + fb_width + " | Y : " + fb_height);
    
    
    // Initialise the bitmap to draw the framebuffer into
    BaseBitmap *pBitmap = BaseBitmap::Alloc();
    pBitmap->Init(f_width, f_height);
    
    // Draw the framebuffer into the bitmap (not really the best way to do this but whatever)
    fbuf->CopyToBitmap(bd, pBitmap, ind, C4D_FRAMEBUFFER_COLOR);
    
    
    
    
    if (pBitmap) {
        SyphonC4DServer::Instance().publishBufferGL(pBitmap);
    }
    BaseBitmap::Free(pBitmap);
    
    
    
	if (flags != VIDEOPOST_GLDRAW_DRAW)
		return false;
    
    
	Float32 prScale[3];
	VPInvertDescData* pDescData = nullptr;
	Bool			bFactoryBound = false;
	C4DGLuint nTexture;
	Int32			lAttributeCount, lVectorBufferCount;
	const GlVertexBufferAttributeInfo* const* ppAttibuteInfo;
	const GlVertexBufferVectorInfo* const*		ppVectorInfo;
	C4D_ALIGN(Int32 lIdentity[1], 8);
	lIdentity[0] = VP_INVERT_IMAGE_SHADER_VERSION;
    
	// get the scale ratios that we don't put the texture on the entire polygon
	Float v1, v2;
	fbuf->GetRatios(C4D_FRAMEBUFFER_COLOR, v1, v2);

	prScale[0] = v1;
	prScale[1] = v2;
	if (prScale[0] <= 0.0f || prScale[1] <= 0.0f)
		return false;
    // Split into incremennts -- I'm guessing
	prScale[0] = 1.0f / prScale[0]; // I think scale is simply 1 in the example
	prScale[1] = 1.0f / prScale[1];
    
	bd->SetDrawParam(DRAW_PARAMETER_USE_Z, false);
    
	if (!bd->GetFullscreenPolygonVectors(lAttributeCount, ppAttibuteInfo, lVectorBufferCount, ppVectorInfo))
		return false;
    
	GlProgramFactory* pFactory = GlProgramFactory::GetFactory(bd, node, 0, nullptr, lIdentity, sizeof(lIdentity), nullptr, 0, 0, ppAttibuteInfo, lAttributeCount, ppVectorInfo, lVectorBufferCount, nullptr);
	if (!pFactory)
		return false;
    
	pFactory->LockFactory();
	if (!pFactory->BindToView(bd))
	{
		pFactory->UnlockFactory();
		goto DisplayError;
	}
    
	pDescData = (VPInvertDescData*)pFactory->GetDescriptionData(0, 0, SyphonVP::AllocCgDescription, SyphonVP::FreeCgDescription, SyphonVP::ReadCgDescription, SyphonVP::WriteCgDescription);
	if (!pDescData)
	{
		pFactory->UnlockFactory();
		goto DisplayError;
	}
    
	if (!pFactory->IsProgram(CompiledProgram))
	{
		// add all necessary parameters
		pFactory->AddParameters(GL_PROGRAM_PARAM_OBJECTCOORD);
		pFactory->Init(0);

		pDescData->strTexsize = pFactory->AddUniformParameter(VertexProgram, UniformFloat2, "texsize");
		pDescData->strTexture = pFactory->AddUniformParameter(FragmentProgram, UniformTexture2D, "texture");
		if (!pFactory->HeaderFinished())
			goto DisplayError;
        
		// now, add the program source code
        // This is where you do stuff to the texture I think. Might not need this for the syphon stuff
		pFactory->AddLine(VertexProgram, "oposition = vec4(iposition.xy, -1.0, 1.0);");
		pFactory->AddLine(VertexProgram, "objectcoord = vec4(.5 * (iposition.xy + vec2(1.0)), 0.0, 0.0);");
		pFactory->AddLine(VertexProgram, "objectcoord.xy = objectcoord.xy * " + pDescData->strTexsize + ".xy;");
//		pFactory->AddLine(FragmentProgram, "ocolor.rgb=vec3(1.0)-texture2D(" + pDescData->strTexture + ", objectcoord.xy).rgb;");
		pFactory->AddLine(FragmentProgram, "ocolor.rgb= texture2D(" + pDescData->strTexture + ", objectcoord.xy).rgb;");
		pFactory->AddLine(FragmentProgram, "ocolor.a=1.0;");
        
		if (!pFactory->CompilePrograms())
		{
			pFactory->DestroyPrograms();
			goto DisplayError;
		}
		pDescData->paramTexsize = pFactory->GetParameterHandle(VertexProgram, pDescData->strTexsize.GetCString());
		pDescData->paramTexture = pFactory->GetParameterHandle(FragmentProgram, pDescData->strTexture.GetCString());
		pFactory->GetVectorInfo(pDescData->lVectorCount, pDescData->ppVectorInfo);
	}
	if (!pFactory->BindPrograms())
	{
		pFactory->UnbindPrograms();
		goto DisplayError;
	}
	bFactoryBound = true;
	pFactory->UnlockFactory();
    
	// set the program parameters
	pFactory->InitSetParameters();
	pFactory->SetParameterReal2(pDescData->paramTexsize, prScale);
	nTexture = fbuf->GetTexture(colortex, C4D_FRAMEBUFFER_COLOR);
	pFactory->SetParameterTexture(pDescData->paramTexture, 2, nTexture);
    
	bd->DrawFullscreenPolygon(pDescData->lVectorCount, pDescData->ppVectorInfo);
    
	pFactory->UnbindPrograms();
	return true;
    
DisplayError:
	if (pFactory)
	{
		if (bFactoryBound)
			pFactory->UnbindPrograms();
		pFactory->BindToView((BaseDraw*)nullptr);
		pFactory->UnlockFactory();
	}
	return false;
}

//----------------------------------------------------------------------------




// If you are developeing a new plugin based on this one
// be sure to use a unique ID obtained from www.plugincafe.com
// http://www.plugincafe.com/developer_plugid.asp
#define ID_SYPHON 1032312

Bool RegisterSyphonPlugin(void)
{
	_printf("READY!");
	return RegisterVideoPostPlugin(ID_SYPHON,GeLoadString(IDS_SYPHONVP),PLUGINFLAG_VIDEOPOST_MULTIPLE | PLUGINFLAG_VIDEOPOST_GL,SyphonVP::Alloc,"SyphonPlugin",0,0);
}


