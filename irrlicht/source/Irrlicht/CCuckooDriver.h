#ifndef __C_CUCKOO_DRIVER_H_INCLUDED__
#define __C_CUCKOO_DRIVER_H_INCLUDED__

#include "CNullDriver.h"
#include "CImage.h"
#include "CCuckooTexture.h"

namespace irr
{
namespace video
{

	class CCuckooDriver : public CNullDriver
	{
	public:

		//! constructor
		CCuckooDriver(const core::dimension2d<u32>& windowSize, bool fullscreen,
				io::IFileSystem* io, video::IImagePresenter* presenter);

		//! destructor
		virtual ~CCuckooDriver();

		//! queries the features of the driver, returns true if feature is available
		virtual bool queryFeature(E_VIDEO_DRIVER_FEATURE feature) const;

		//! sets transformation
		virtual void setTransform(E_TRANSFORMATION_STATE state, const core::matrix4& mat);

		//! sets a material
		virtual void setMaterial(const SMaterial& material);

		//! sets render target
		virtual bool setRenderTarget(video::ITexture* texture, bool clearBackBuffer=true,
						bool clearZ=true, SColor color=SColor(0,0,0,0));

		//! sets a viewport
		virtual void setViewPort(const core::rect<s32>& area);

		//! clears the zbuffer and backbuffer
		virtual bool beginScene(bool backBuffer=true, bool zBuffer=true,
				SColor color=SColor(255,0,0,0),
				const SExposedVideoData& videoData=SExposedVideoData(),
				core::rect<s32>* sourceRect=0);

		//! presents the rendered scene on the screen
		virtual bool endScene();

		//! window resize callback
		virtual void OnResize(const core::dimension2d<u32>& size);

		//! returns size of current render target
		virtual const core::dimension2d<u32>& getCurrentRenderTargetSize() const;

		//! draws a vertex primitive list
		virtual void drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
				const void* indexList, u32 primitiveCount,
				E_VERTEX_TYPE vType, scene::E_PRIMITIVE_TYPE pType, E_INDEX_TYPE iType);

		//! draws a 3d line
		virtual void draw3DLine(const core::vector3df& start,
			const core::vector3df& end, SColor color = SColor(255,255,255,255));

		//! draws a 2d image
		virtual void draw2DImage(const video::ITexture* texture, const core::position2d<s32>& destPos,
			const core::rect<s32>& sourceRect, const core::rect<s32>* clipRect = 0,
			SColor color=SColor(255,255,255,255), bool useAlphaChannelOfTexture=false);

		//! draws a 2d image with scaling and optional color tinting per vertex
		virtual void draw2DImage(const video::ITexture* texture, const core::rect<s32>& destRect,
			const core::rect<s32>& sourceRect, const core::rect<s32>* clipRect = 0,
			const video::SColor* const colors=0, bool useAlphaChannelOfTexture=false);

		//! draw a 2d rectangle
		virtual void draw2DRectangle(SColor color, const core::rect<s32>& pos,
			const core::rect<s32>* clip = 0);

		//! draws a 2d rectangle with a gradient
		virtual void draw2DRectangle(const core::rect<s32>& pos,
			SColor colorLeftUp, SColor colorRightUp, SColor colorLeftDown, SColor colorRightDown,
			const core::rect<s32>* clip = 0);

		//! draws a 2d line
		virtual void draw2DLine(const core::position2d<s32>& start,
								const core::position2d<s32>& end,
								SColor color=SColor(255,255,255,255));

		//! draws a single pixel
		virtual void drawPixel(u32 x, u32 y, const SColor & color);

		//! returns name of video driver
		virtual const wchar_t* getName() const;

		//! returns type of video driver
		virtual E_DRIVER_TYPE getDriverType() const;

		//! get color format of current color buffer
		virtual ECOLOR_FORMAT getColorFormat() const;

		//! returns transformation set by setTransform
		virtual const core::matrix4& getTransform(E_TRANSFORMATION_STATE state) const;

		//! returns a device dependent texture from a software surface
		virtual video::ITexture* createDeviceDependentTexture(IImage* surface, const io::path& name, void* mipmapData=0);

		//! creates a render target texture
		virtual ITexture* addRenderTargetTexture(const core::dimension2d<u32>& size,
				const io::path& name, const ECOLOR_FORMAT format = ECF_UNKNOWN);

		//! clears the ZBuffer
		virtual void clearZBuffer();

		//! returns screenshot image
		virtual IImage* createScreenShot();

		//! returns maximum primitives
		virtual u32 getMaximalPrimitiveCount() const;

		//! sets the fog. very good explanation.
		virtual void setFog(SColor color = SColor(0,255,255,255), E_FOG_TYPE fogType = EFT_FOG_LINEAR,  f32 start = 50.0f, f32 end = 300.0f, f32 density = 0.01f, bool pixelFog = false, bool rangeFog = false) override;

	public:

		struct STransformedVertex
		{
			f32 x, y, z, w;
			f32 u, v;
			f32 invW;
			SColor color;
			f32 lightFactor;
		};

		struct SClipVertex
		{
			f32 x, y, z, w;
			f32 u, v;
			SColor color;
			f32 lightFactor;
		};

	protected:

		SColor FogColor;
    		f32 FogStart, FogEnd;
    		bool FogEnable;

		static STransformedVertex projectClipVertex(
			const SClipVertex& cv,
			s32 midX, s32 midY, s32 halfW, s32 halfH);

		static SClipVertex clipEdge(
			const SClipVertex& vIn,
			const SClipVertex& vOut,
			f32 wNear = 0.001f);

		void renderTriangle3D(const STransformedVertex& v0,
		                      const STransformedVertex& v1,
		                      const STransformedVertex& v2,
		                      u32* dstData, u32 dstPitch,
		                      u32* srcData, u32 texWidth, u32 texHeight, u32 srcPitch);

		void draw3DLineTransformed(const STransformedVertex& v0,
		                          const STransformedVertex& v1,
		                          SColor color);

		void setRenderTargetSurface(video::CImage* image);

		video::CImage* BackBuffer;
		video::IImagePresenter* Presenter;
		void* WindowId;
		core::rect<s32>* SceneSourceRect;

		video::ITexture* RenderTargetTexture;
		video::CImage* RenderTargetSurface;
		core::dimension2d<u32> RenderTargetSize;

		core::matrix4 TransformationMatrix[ETS_COUNT];

		video::ITexture* CurrentTexture;
		video::CImage* CurrentTextureImage;
		SMaterial Material;

		f32* DepthBuffer;
		u32 DepthBufferSize;

		core::array<STransformedVertex> TransformedVerts;
		core::array<SClipVertex> ClipVerts;
	};

} // end namespace video
} // end namespace irr

#endif // __C_CUCKOO_DRIVER_H_INCLUDED__
