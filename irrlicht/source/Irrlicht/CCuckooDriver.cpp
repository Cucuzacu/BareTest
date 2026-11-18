#include "IrrCompileConfig.h"
#ifdef _IRR_COMPILE_WITH_CUCKOOVIDEO_

#include "CCuckooDriver.h"
#include "CCuckooTexture.h"
#include "CSoftwareTexture.h"
#include "os.h"
#include <cmath>
#include <algorithm>

// i'm amazed by how relatively simple this renderer can be, yet it can fully render complex 3d scenes. yay.
// only if it would be faster...

namespace irr
{
namespace video
{

inline u32 blendPixel32(u32 src, u32 dst)
{
	u32 sa = (src >> 24) & 0xFF;
	if (sa == 255) return src;
	if (sa == 0)   return dst;

	u32 invSa = 255 - sa;

	u32 sr = (src >> 16) & 0xFF;
	u32 sg = (src >> 8)  & 0xFF;
	u32 sb = src         & 0xFF;

	u32 dr = (dst >> 16) & 0xFF;
	u32 dg = (dst >> 8)  & 0xFF;
	u32 db = dst         & 0xFF;

	u32 r = (sr * sa + dr * invSa + 128);
	r = (r + (r >> 8)) >> 8;
	u32 g = (sg * sa + dg * invSa + 128);
	g = (g + (g >> 8)) >> 8;
	u32 b = (sb * sa + db * invSa + 128);
	b = (b + (b >> 8)) >> 8;

	return (255 << 24) | (r << 16) | (g << 8) | b;
}

inline u32 modulateColorFast(u32 c0, u32 c1)
{
	if (c1 == 0xFFFFFFFF) return c0;
	if (c0 == 0) return 0;

	u32 rb0 = c0 & 0x00FF00FF;
	u32 g0  = (c0 >> 8) & 0x000000FF;
	u32 a0  = (c0 >> 24) & 0x000000FF;

	u32 rb1 = c1 & 0x00FF00FF;
	u32 g1  = (c1 >> 8) & 0x000000FF;
	u32 a1  = (c1 >> 24) & 0x000000FF;

	u32 r = ((rb0 >> 16) * (rb1 >> 16) + 128);
	r = (r + (r >> 8)) >> 8;
	u32 b = ((rb0 & 0xFF) * (rb1 & 0xFF) + 128);
	b = (b + (b >> 8)) >> 8;
	u32 g = (g0 * g1 + 128);
	g = (g + (g >> 8)) >> 8;
	u32 a = (a0 * a1 + 128);
	a = (a + (a >> 8)) >> 8;

	return (a << 24) | (r << 16) | (g << 8) | b;
}

inline u32 modulateColor(u32 colorA, u32 colorB)
{
	return modulateColorFast(colorA, colorB);
}

//! constructor
CCuckooDriver::CCuckooDriver(const core::dimension2d<u32>& windowSize, bool fullscreen,
		io::IFileSystem* io, video::IImagePresenter* presenter)
: CNullDriver(io, windowSize), BackBuffer(0), Presenter(presenter), WindowId(0),
  SceneSourceRect(0), RenderTargetTexture(0), RenderTargetSurface(0),
  CurrentTexture(0), CurrentTextureImage(0), DepthBuffer(0), DepthBufferSize(0),
  FogColor(255, 255, 255, 255), FogStart(50.0f), FogEnd(300.0f), FogEnable(false)
{
	#ifdef _DEBUG
	setDebugName("CCuckooDriver");
	#endif

	core::dimension2d<u32> realSize(windowSize);
	if (realSize.Width < 1) realSize.Width = 1;
	if (realSize.Height < 1) realSize.Height = 1;

	ScreenSize = realSize;
	BackBuffer = new CImage(ECF_A8R8G8B8, realSize);

	setRenderTargetSurface(BackBuffer);

	TransformationMatrix[ETS_VIEW].makeIdentity();
	TransformationMatrix[ETS_WORLD].makeIdentity();
	TransformationMatrix[ETS_PROJECTION].makeIdentity();
}

//! destructor
CCuckooDriver::~CCuckooDriver()
{
	if (BackBuffer)
		BackBuffer->drop();

	if (RenderTargetTexture)
		RenderTargetTexture->drop();

	if (RenderTargetSurface)
		RenderTargetSurface->drop();

	if (CurrentTexture)
		CurrentTexture->drop();

	if (DepthBuffer)
		delete[] DepthBuffer;
}

bool CCuckooDriver::queryFeature(E_VIDEO_DRIVER_FEATURE feature) const
{
	switch (feature)
	{
	case EVDF_RENDER_TO_TARGET:
	case EVDF_TEXTURE_NSQUARE:
	case EVDF_TEXTURE_NPOT:
	case EVDF_BILINEAR_FILTER:
		return true;
	default:
		return FeatureEnabled[feature];
	}
}

void CCuckooDriver::setTransform(E_TRANSFORMATION_STATE state, const core::matrix4& mat)
{
	TransformationMatrix[state] = mat;
}

const core::matrix4& CCuckooDriver::getTransform(E_TRANSFORMATION_STATE state) const
{
	return TransformationMatrix[state];
}

void CCuckooDriver::setMaterial(const SMaterial& material)
{
	Material = material;
	OverrideMaterial.apply(Material);

	if (CurrentTexture)
	{
		CurrentTexture->drop();
		CurrentTexture = 0;
		CurrentTextureImage = 0;
	}

	ITexture* tex = Material.getTexture(0);
	if (tex && tex->getDriverType() == EDT_CUCKOOVIDEO)
	{
		CurrentTexture = tex;
		CurrentTexture->grab();
		CurrentTextureImage = ((CCuckooTexture*)CurrentTexture)->getImage();
	}
}

bool CCuckooDriver::setRenderTarget(video::ITexture* texture, bool clearBackBuffer,
									bool clearZ, SColor color)
{
	if (texture && texture->getDriverType() != EDT_CUCKOOVIDEO)
	{
		os::Printer::log("Fatal Error: Tried to set a render target texture not owned by CuckooDriver.", ELL_ERROR);
		return false;
	}

	if (RenderTargetTexture)
		RenderTargetTexture->drop();

	RenderTargetTexture = texture;

	if (RenderTargetTexture)
	{
		RenderTargetTexture->grab();
		setRenderTargetSurface(((CCuckooTexture*)RenderTargetTexture)->getImage());
	}
	else
	{
		setRenderTargetSurface(BackBuffer);
	}

	if (RenderTargetSurface && (clearBackBuffer || clearZ))
	{
		if (clearZ)
			clearZBuffer();

		if (clearBackBuffer)
			RenderTargetSurface->fill(color);
	}

	return true;
}

void CCuckooDriver::setRenderTargetSurface(video::CImage* image)
{
	if (RenderTargetSurface)
		RenderTargetSurface->drop();

	RenderTargetSurface = image;

	if (RenderTargetSurface)
	{
		RenderTargetSurface->grab();
		RenderTargetSize = RenderTargetSurface->getDimension();
	}
	else
	{
		RenderTargetSize.Width = 0;
		RenderTargetSize.Height = 0;
	}

	u32 neededDepthSize = RenderTargetSize.Width * RenderTargetSize.Height;
	if (neededDepthSize > DepthBufferSize)
	{
		if (DepthBuffer)
			delete[] DepthBuffer;
		DepthBufferSize = neededDepthSize;
		DepthBuffer = new f32[DepthBufferSize];
	}

	setViewPort(core::rect<s32>(0, 0, RenderTargetSize.Width, RenderTargetSize.Height));
}

void CCuckooDriver::setViewPort(const core::rect<s32>& area)
{
	ViewPort = area;
	core::rect<s32> rendert(0, 0, RenderTargetSize.Width, RenderTargetSize.Height);
	ViewPort.clipAgainst(rendert);
}

bool CCuckooDriver::beginScene(bool backBuffer, bool zBuffer, SColor color,
		const SExposedVideoData& videoData, core::rect<s32>* sourceRect)
{
	CNullDriver::beginScene(backBuffer, zBuffer, color, videoData, sourceRect);
	WindowId = videoData.D3D9.HWnd;
	SceneSourceRect = sourceRect;

	if (backBuffer && RenderTargetSurface)
		RenderTargetSurface->fill(color);

	if (zBuffer)
		clearZBuffer();

	return true;
}

bool CCuckooDriver::endScene()
{
	CNullDriver::endScene();

	if (Presenter && BackBuffer)
		return Presenter->present(BackBuffer, WindowId, SceneSourceRect);

	return true;
}

void CCuckooDriver::OnResize(const core::dimension2d<u32>& size)
{
	core::dimension2d<u32> realSize(size);
	if (realSize.Width < 1) realSize.Width = 1;
	if (realSize.Height < 1) realSize.Height = 1;

	if (ScreenSize != realSize)
	{
		ScreenSize = realSize;
		bool resetRT = (RenderTargetSurface == BackBuffer);

		if (BackBuffer)
			BackBuffer->drop();
		BackBuffer = new CImage(ECF_A8R8G8B8, realSize);

		if (resetRT)
			setRenderTargetSurface(BackBuffer);
	}
}

const core::dimension2d<u32>& CCuckooDriver::getCurrentRenderTargetSize() const
{
	return RenderTargetSize;
}

void CCuckooDriver::clearZBuffer()
{
	if (DepthBuffer && DepthBufferSize > 0)
	{
		std::fill(DepthBuffer, DepthBuffer + DepthBufferSize, 1000000.0f);
	}
}

IImage* CCuckooDriver::createScreenShot()
{
	if (BackBuffer)
	{
		CImage* tmp = new CImage(BackBuffer->getColorFormat(), BackBuffer->getDimension());
		BackBuffer->copyTo(tmp);
		return tmp;
	}
	return 0;
}

u32 CCuckooDriver::getMaximalPrimitiveCount() const
{
	return 0x00FFFFFF;
}

const wchar_t* CCuckooDriver::getName() const
{
	return L"Cucuzacu's Cuckoo Software Renderer";
}

E_DRIVER_TYPE CCuckooDriver::getDriverType() const
{
	return EDT_CUCKOOVIDEO;
}

ECOLOR_FORMAT CCuckooDriver::getColorFormat() const
{
	if (BackBuffer)
		return BackBuffer->getColorFormat();
	return ECF_A8R8G8B8;
}

ITexture* CCuckooDriver::createDeviceDependentTexture(IImage* surface, const io::path& name, void* mipmapData)
{
	return new CCuckooTexture(surface, name, false, mipmapData);
}

ITexture* CCuckooDriver::addRenderTargetTexture(const core::dimension2d<u32>& size,
												const io::path& name,
												const ECOLOR_FORMAT format)
{
	CImage* img = new CImage(video::ECF_A8R8G8B8, size);
	ITexture* tex = new CCuckooTexture(img, name, true);
	img->drop();
	addTexture(tex);
	tex->drop();
	return tex;
}

void CCuckooDriver::drawPixel(u32 x, u32 y, const SColor & color)
{
	if (!RenderTargetSurface)
		return;

	if (x < (u32)ViewPort.UpperLeftCorner.X || x >= (u32)ViewPort.LowerRightCorner.X ||
	    y < (u32)ViewPort.UpperLeftCorner.Y || y >= (u32)ViewPort.LowerRightCorner.Y)
		return;

	u32* dstData = (u32*)RenderTargetSurface->lock();
	if (dstData)
	{
		u32 pitch = RenderTargetSurface->getPitch() / 4;
		u32 idx = y * pitch + x;
		dstData[idx] = blendPixel32(color.color, dstData[idx]);
		RenderTargetSurface->unlock();
	}
}

void CCuckooDriver::draw2DRectangle(SColor color, const core::rect<s32>& pos, const core::rect<s32>* clip)
{
	if (!RenderTargetSurface)
		return;

	core::rect<s32> r = pos;
	if (clip)
		r.clipAgainst(*clip);
	r.clipAgainst(ViewPort);

	if (!r.isValid() || r.getWidth() <= 0 || r.getHeight() <= 0)
		return;

	u32* dstData = (u32*)RenderTargetSurface->lock();
	if (!dstData) return;

	u32 pitch = RenderTargetSurface->getPitch() / 4;
	u32 col = color.color;
	u32 alpha = color.getAlpha();

	if (alpha == 255)
	{
		s32 width = r.getWidth();
		for (s32 y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y)
		{
			u32* row = dstData + y * pitch + r.UpperLeftCorner.X;
			std::fill(row, row + width, col);
		}
	}
	else
	{
		for (s32 y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y)
		{
			u32* row = dstData + y * pitch;
			for (s32 x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x)
			{
				row[x] = blendPixel32(col, row[x]);
			}
		}
	}

	RenderTargetSurface->unlock();
}

void CCuckooDriver::draw2DRectangle(const core::rect<s32>& pos,
	SColor colorLeftUp, SColor colorRightUp, SColor colorLeftDown, SColor colorRightDown,
	const core::rect<s32>* clip)
{
	if (!RenderTargetSurface)
		return;

	core::rect<s32> r = pos;
	if (clip)
		r.clipAgainst(*clip);
	r.clipAgainst(ViewPort);

	if (!r.isValid() || r.getWidth() <= 0 || r.getHeight() <= 0)
		return;

	u32* dstData = (u32*)RenderTargetSurface->lock();
	if (!dstData) return;

	u32 pitch = RenderTargetSurface->getPitch() / 4;
	f32 invW = 1.0f / (f32)pos.getWidth();
	f32 invH = 1.0f / (f32)pos.getHeight();

	for (s32 y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y)
	{
		f32 fy = (f32)(y - pos.UpperLeftCorner.Y) * invH;
		SColor colLeft = colorLeftUp.getInterpolated(colorLeftDown, 1.0f - fy);
		SColor colRight = colorRightUp.getInterpolated(colorRightDown, 1.0f - fy);

		u32* row = dstData + y * pitch;
		for (s32 x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x)
		{
			f32 fx = (f32)(x - pos.UpperLeftCorner.X) * invW;
			SColor c = colLeft.getInterpolated(colRight, 1.0f - fx);
			row[x] = blendPixel32(c.color, row[x]);
		}
	}

	RenderTargetSurface->unlock();
}

void CCuckooDriver::draw2DLine(const core::position2d<s32>& start,
							  const core::position2d<s32>& end,
							  SColor color)
{
	if (!RenderTargetSurface)
		return;

	s32 x0 = start.X;
	s32 y0 = start.Y;
	s32 x1 = end.X;
	s32 y1 = end.Y;

	s32 dx = abs(x1 - x0);
	s32 dy = abs(y1 - y0);
	s32 sx = (x0 < x1) ? 1 : -1;
	s32 sy = (y0 < y1) ? 1 : -1;
	s32 err = dx - dy;

	while (true)
	{
		drawPixel((u32)x0, (u32)y0, color);
		if (x0 == x1 && y0 == y1)
			break;

		s32 e2 = 2 * err;
		if (e2 > -dy)
		{
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx)
		{
			err += dx;
			y0 += sy;
		}
	}
}

void CCuckooDriver::draw2DImage(const video::ITexture* texture,
							   const core::position2d<s32>& destPos,
							   const core::rect<s32>& sourceRect,
							   const core::rect<s32>* clipRect,
							   SColor color,
							   bool useAlphaChannelOfTexture)
{
	if (!texture || !RenderTargetSurface)
		return;

	CImage* srcImg = 0;
	if (texture->getDriverType() == EDT_CUCKOOVIDEO)
		srcImg = ((CCuckooTexture*)texture)->getImage();
	else
		srcImg = ((CSoftwareTexture*)texture)->getImage();

	if (!srcImg) return;

	core::rect<s32> sRect = sourceRect;
	core::rect<s32> dRect(destPos, sRect.getSize());

	core::rect<s32> clip = ViewPort;
	if (clipRect)
		clip.clipAgainst(*clipRect);

	core::rect<s32> clippedDest = dRect;
	clippedDest.clipAgainst(clip);

	if (!clippedDest.isValid() || clippedDest.getWidth() <= 0 || clippedDest.getHeight() <= 0)
		return;

	s32 xOffset = clippedDest.UpperLeftCorner.X - dRect.UpperLeftCorner.X;
	s32 yOffset = clippedDest.UpperLeftCorner.Y - dRect.UpperLeftCorner.Y;

	u32* srcData = (u32*)srcImg->lock();
	u32* dstData = (u32*)RenderTargetSurface->lock();

	if (srcData && dstData)
	{
		u32 srcPitch = srcImg->getPitch() / 4;
		u32 dstPitch = RenderTargetSurface->getPitch() / 4;
		u32 tintColor = color.color;
		bool hasTint = (tintColor != 0xFFFFFFFF);

		if (!useAlphaChannelOfTexture && !hasTint)
		{
			s32 width = clippedDest.getWidth();
			for (s32 y = 0; y < clippedDest.getHeight(); ++y)
			{
				s32 srcY = sRect.UpperLeftCorner.Y + yOffset + y;
				s32 dstY = clippedDest.UpperLeftCorner.Y + y;
				u32* sRow = srcData + srcY * srcPitch + (sRect.UpperLeftCorner.X + xOffset);
				u32* dRow = dstData + dstY * dstPitch + clippedDest.UpperLeftCorner.X;
				memcpy(dRow, sRow, width * sizeof(u32));
			}
		}
		else
		{
			for (s32 y = 0; y < clippedDest.getHeight(); ++y)
			{
				s32 srcY = sRect.UpperLeftCorner.Y + yOffset + y;
				s32 dstY = clippedDest.UpperLeftCorner.Y + y;

				u32* sRow = srcData + srcY * srcPitch;
				u32* dRow = dstData + dstY * dstPitch;

				for (s32 x = 0; x < clippedDest.getWidth(); ++x)
				{
					s32 srcX = sRect.UpperLeftCorner.X + xOffset + x;
					s32 dstX = clippedDest.UpperLeftCorner.X + x;

					u32 pixel = sRow[srcX];
					if (hasTint)
						pixel = modulateColor(pixel, tintColor);

					if (useAlphaChannelOfTexture)
						dRow[dstX] = blendPixel32(pixel, dRow[dstX]);
					else
						dRow[dstX] = pixel;
				}
			}
		}
	}

	srcImg->unlock();
	RenderTargetSurface->unlock();
}

void CCuckooDriver::draw2DImage(const video::ITexture* texture,
							   const core::rect<s32>& destRect,
							   const core::rect<s32>& sourceRect,
							   const core::rect<s32>* clipRect,
							   const video::SColor* const colors,
							   bool useAlphaChannelOfTexture)
{
	if (!texture || !RenderTargetSurface)
		return;

	CImage* srcImg = 0;
	if (texture->getDriverType() == EDT_CUCKOOVIDEO)
		srcImg = ((CCuckooTexture*)texture)->getImage();
	else
		srcImg = ((CSoftwareTexture*)texture)->getImage();

	if (!srcImg) return;

	core::rect<s32> clip = ViewPort;
	if (clipRect)
		clip.clipAgainst(*clipRect);

	core::rect<s32> clippedDest = destRect;
	clippedDest.clipAgainst(clip);

	if (!clippedDest.isValid() || clippedDest.getWidth() <= 0 || clippedDest.getHeight() <= 0)
		return;

	u32* srcData = (u32*)srcImg->lock();
	u32* dstData = (u32*)RenderTargetSurface->lock();

	if (srcData && dstData)
	{
		u32 srcPitch = srcImg->getPitch() / 4;
		u32 dstPitch = RenderTargetSurface->getPitch() / 4;

		f32 scaleX = (f32)sourceRect.getWidth() / (f32)destRect.getWidth();
		f32 scaleY = (f32)sourceRect.getHeight() / (f32)destRect.getHeight();

		u32 tintColor = (colors) ? colors[0].color : 0xFFFFFFFF;
		bool hasTint = (tintColor != 0xFFFFFFFF);

		for (s32 y = clippedDest.UpperLeftCorner.Y; y < clippedDest.LowerRightCorner.Y; ++y)
		{
			f32 sy = (f32)(y - destRect.UpperLeftCorner.Y) * scaleY + (f32)sourceRect.UpperLeftCorner.Y;
			s32 srcY = (s32)sy;
			if (srcY < 0) srcY = 0;
			if (srcY >= (s32)srcImg->getDimension().Height) srcY = srcImg->getDimension().Height - 1;

			u32* sRow = srcData + srcY * srcPitch;
			u32* dRow = dstData + y * dstPitch;

			for (s32 x = clippedDest.UpperLeftCorner.X; x < clippedDest.LowerRightCorner.X; ++x)
			{
				f32 sx = (f32)(x - destRect.UpperLeftCorner.X) * scaleX + (f32)sourceRect.UpperLeftCorner.X;
				s32 srcX = (s32)sx;
				if (srcX < 0) srcX = 0;
				if (srcX >= (s32)srcImg->getDimension().Width) srcX = srcImg->getDimension().Width - 1;

				u32 pixel = sRow[srcX];
				if (hasTint)
					pixel = modulateColor(pixel, tintColor);

				if (useAlphaChannelOfTexture)
					dRow[x] = blendPixel32(pixel, dRow[x]);
				else
					dRow[x] = pixel;
			}
		}
	}

	srcImg->unlock();
	RenderTargetSurface->unlock();
}

void CCuckooDriver::draw3DLineTransformed(const STransformedVertex& v0, const STransformedVertex& v1, SColor color)
{
	if (!RenderTargetSurface)
		return;

	s32 x0 = (s32)v0.x;
	s32 y0 = (s32)v0.y;
	f32 z0 = v0.z;

	s32 x1 = (s32)v1.x;
	s32 y1 = (s32)v1.y;
	f32 z1 = v1.z;

	s32 dx = abs(x1 - x0);
	s32 dy = abs(y1 - y0);
	s32 sx = (x0 < x1) ? 1 : -1;
	s32 sy = (y0 < y1) ? 1 : -1;
	s32 err = dx - dy;

	s32 steps = std::max(dx, dy);
	f32 invSteps = steps > 0 ? (1.0f / (f32)steps) : 0.0f;
	f32 dz = (z1 - z0) * invSteps;
	f32 curZ = z0;

	u32* dstData = (u32*)RenderTargetSurface->lock();
	if (!dstData) return;
	u32 dstPitch = RenderTargetSurface->getPitch() / 4;

	while (true)
	{
		if (x0 >= ViewPort.UpperLeftCorner.X && x0 < ViewPort.LowerRightCorner.X &&
		    y0 >= ViewPort.UpperLeftCorner.Y && y0 < ViewPort.LowerRightCorner.Y)
		{
			f32* zPtr = DepthBuffer + y0 * RenderTargetSize.Width + x0;
			if (curZ <= *zPtr + 0.001f && curZ >= 0.0f)
			{
				dstData[y0 * dstPitch + x0] = blendPixel32(color.color, dstData[y0 * dstPitch + x0]);
			}
		}

		if (x0 == x1 && y0 == y1)
			break;

		s32 e2 = 2 * err;
		if (e2 > -dy)
		{
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx)
		{
			err += dx;
			y0 += sy;
		}
		curZ += dz;
	}

	RenderTargetSurface->unlock();
}

void CCuckooDriver::draw3DLine(const core::vector3df& start, const core::vector3df& end, SColor color)
{
	core::matrix4 m(TransformationMatrix[ETS_PROJECTION]);
	m *= TransformationMatrix[ETS_VIEW];
	m *= TransformationMatrix[ETS_WORLD];

	f32 p0[4] = { start.X, start.Y, start.Z, 1.0f };
	f32 p1[4] = { end.X, end.Y, end.Z, 1.0f };

	m.multiplyWith1x4Matrix(p0);
	m.multiplyWith1x4Matrix(p1);

	if (p0[3] <= 0.001f || p1[3] <= 0.001f)
		return;

	f32 invW0 = 1.0f / p0[3];
	f32 invW1 = 1.0f / p1[3];

	s32 halfW = ViewPort.getWidth() / 2;
	s32 halfH = ViewPort.getHeight() / 2;
	s32 midX = ViewPort.UpperLeftCorner.X + halfW;
	s32 midY = ViewPort.UpperLeftCorner.Y + halfH;

	STransformedVertex v0, v1;
	v0.x = (f32)midX + (p0[0] * invW0 * (f32)halfW);
	v0.y = (f32)midY - (p0[1] * invW0 * (f32)halfH);
	v0.z = p0[2] * invW0;
	v0.w = p0[3];

	v1.x = (f32)midX + (p1[0] * invW1 * (f32)halfW);
	v1.y = (f32)midY - (p1[1] * invW1 * (f32)halfH);
	v1.z = p1[2] * invW1;
	v1.w = p1[3];

	draw3DLineTransformed(v0, v1, color);
}

CCuckooDriver::STransformedVertex CCuckooDriver::projectClipVertex(
	const CCuckooDriver::SClipVertex& cv,
	s32 midX, s32 midY, s32 halfW, s32 halfH)
{
	CCuckooDriver::STransformedVertex tv;
	tv.invW = 1.0f / cv.w;
	tv.x = (f32)midX + (cv.x * tv.invW * (f32)halfW);
	tv.y = (f32)midY - (cv.y * tv.invW * (f32)halfH);
	tv.z = cv.z * tv.invW;
	tv.w = cv.w;
	tv.u = cv.u * tv.invW;
	tv.v = cv.v * tv.invW;
	tv.color = cv.color;
	tv.lightFactor = cv.lightFactor;
	return tv;
}

CCuckooDriver::SClipVertex CCuckooDriver::clipEdge(
	const CCuckooDriver::SClipVertex& vIn,
	const CCuckooDriver::SClipVertex& vOut,
	f32 wNear)
{
	f32 denom = vOut.w - vIn.w;
	f32 t = (fabsf(denom) > 1e-6f) ? ((wNear - vIn.w) / denom) : 0.0f;
	if (t < 0.0f) t = 0.0f;
	else if (t > 1.0f) t = 1.0f;

	CCuckooDriver::SClipVertex res;
	res.x = vIn.x + t * (vOut.x - vIn.x);
	res.y = vIn.y + t * (vOut.y - vIn.y);
	res.z = vIn.z + t * (vOut.z - vIn.z);
	res.w = wNear;
	res.u = vIn.u + t * (vOut.u - vIn.u);
	res.v = vIn.v + t * (vOut.v - vIn.v);
	res.color = vIn.color.getInterpolated(vOut.color, 1.0f - t);
	res.lightFactor = vIn.lightFactor + t * (vOut.lightFactor - vIn.lightFactor);
	return res;
}

void CCuckooDriver::renderTriangle3D(const STransformedVertex& v0,
                                     const STransformedVertex& v1,
                                     const STransformedVertex& v2,
                                     u32* dstData, u32 dstPitch,
                                     u32* srcData, u32 texWidth, u32 texHeight, u32 srcPitch)
{
	// 2d bounding box
	s32 minX = (s32)floorf(std::min(v0.x, std::min(v1.x, v2.x)));
	s32 maxX = (s32)ceilf(std::max(v0.x, std::max(v1.x, v2.x)));
	s32 minY = (s32)floorf(std::min(v0.y, std::min(v1.y, v2.y)));
	s32 maxY = (s32)ceilf(std::max(v0.y, std::max(v1.y, v2.y)));

	minX = std::max(minX, ViewPort.UpperLeftCorner.X);
	maxX = std::min(maxX, ViewPort.LowerRightCorner.X);
	minY = std::max(minY, ViewPort.UpperLeftCorner.Y);
	maxY = std::min(maxY, ViewPort.LowerRightCorner.Y);

	if (minX >= maxX || minY >= maxY)
		return;

	f32 area = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
	if (Material.BackfaceCulling && area <= 0.0f)
		return;
	if (Material.FrontfaceCulling && area >= 0.0f)
		return;
	if (fabsf(area) < 0.0001f)
		return;

	f32 invArea = 1.0f / area;

	f32 dw0_dx = (v1.y - v2.y) * invArea;
	f32 dw0_dy = (v2.x - v1.x) * invArea;
	f32 dw1_dx = (v2.y - v0.y) * invArea;
	f32 dw1_dy = (v0.x - v2.x) * invArea;
	f32 dw2_dx = -dw0_dx - dw1_dx;
	f32 dw2_dy = -dw0_dy - dw1_dy;

	f32 dz_dx = dw0_dx * v0.z + dw1_dx * v1.z + dw2_dx * v2.z;
	f32 dz_dy = dw0_dy * v0.z + dw1_dy * v1.z + dw2_dy * v2.z;

	f32 dinvW_dx = dw0_dx * v0.invW + dw1_dx * v1.invW + dw2_dx * v2.invW;
	f32 dinvW_dy = dw0_dy * v0.invW + dw1_dy * v1.invW + dw2_dy * v2.invW;

	f32 dU_dx = dw0_dx * v0.u + dw1_dx * v1.u + dw2_dx * v2.u;
	f32 dU_dy = dw0_dy * v0.u + dw1_dy * v1.u + dw2_dy * v2.u;

	f32 dV_dx = dw0_dx * v0.v + dw1_dx * v1.v + dw2_dx * v2.v;
	f32 dV_dy = dw0_dy * v0.v + dw1_dy * v1.v + dw2_dy * v2.v;

	bool hasTexture = (srcData && texWidth > 0 && texHeight > 0);
	bool isPow2 = hasTexture && ((texWidth & (texWidth - 1)) == 0) && ((texHeight & (texHeight - 1)) == 0);
	u32 maskX = texWidth - 1;
	u32 maskY = texHeight - 1;
	f32 fTexWidth = (f32)texWidth;
	f32 fTexHeight = (f32)texHeight;
	bool isAlphaTest = (Material.MaterialType == EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
	bool zWriteEnable = Material.ZWriteEnable;
	bool isTransparentMaterial = Material.isTransparent();

	bool isFlatColor = (v0.color == v1.color && v1.color == v2.color &&
	                    v0.lightFactor == v1.lightFactor && v1.lightFactor == v2.lightFactor);
	u32 flatVertCol = 0xFFFFFFFF;
	if (isFlatColor)
	{
		f32 light = v0.lightFactor;
		u32 r = (u32)(v0.color.getRed() * light + 0.5f);
		u32 g = (u32)(v0.color.getGreen() * light + 0.5f);
		u32 b = (u32)(v0.color.getBlue() * light + 0.5f);
		u32 a = v0.color.getAlpha();
		if (r > 255) r = 255;
		if (g > 255) g = 255;
		if (b > 255) b = 255;
		flatVertCol = (a << 24) | (r << 16) | (g << 8) | b;
	}

	f32 dr_dx = 0, dr_dy = 0, dg_dx = 0, dg_dy = 0, db_dx = 0, db_dy = 0, da_dx = 0, da_dy = 0;
	f32 c0r = 0, c1r = 0, c2r = 0, c0g = 0, c1g = 0, c2g = 0, c0b = 0, c1b = 0, c2b = 0, c0a = 0, c1a = 0, c2a = 0;
	if (!isFlatColor)
	{
		c0r = v0.color.getRed() * v0.lightFactor;
		c1r = v1.color.getRed() * v1.lightFactor;
		c2r = v2.color.getRed() * v2.lightFactor;
		c0g = v0.color.getGreen() * v0.lightFactor;
		c1g = v1.color.getGreen() * v1.lightFactor;
		c2g = v2.color.getGreen() * v2.lightFactor;
		c0b = v0.color.getBlue() * v0.lightFactor;
		c1b = v1.color.getBlue() * v1.lightFactor;
		c2b = v2.color.getBlue() * v2.lightFactor;
		c0a = (f32)v0.color.getAlpha();
		c1a = (f32)v1.color.getAlpha();
		c2a = (f32)v2.color.getAlpha();

		dr_dx = dw0_dx * c0r + dw1_dx * c1r + dw2_dx * c2r;
		dr_dy = dw0_dy * c0r + dw1_dy * c1r + dw2_dy * c2r;
		dg_dx = dw0_dx * c0g + dw1_dx * c1g + dw2_dx * c2g;
		dg_dy = dw0_dy * c0g + dw1_dy * c1g + dw2_dy * c2g;
		db_dx = dw0_dx * c0b + dw1_dx * c1b + dw2_dx * c2b;
		db_dy = dw0_dy * c0b + dw1_dy * c1b + dw2_dy * c2b;
		da_dx = dw0_dx * c0a + dw1_dx * c1a + dw2_dx * c2a;
		da_dy = dw0_dy * c0a + dw1_dy * c1a + dw2_dy * c2a;
	}

	bool fogActive = (FogEnable && Material.FogEnable);
	f32 invFogRange = 0.0f;
	u32 fogColorRed = 0, fogColorGreen = 0, fogColorBlue = 0;
	if (fogActive)
	{
		f32 diff = FogEnd - FogStart;
		invFogRange = (diff != 0.0f) ? (1.0f / diff) : 0.0f;
		fogColorRed = FogColor.getRed();
		fogColorGreen = FogColor.getGreen();
		fogColorBlue = FogColor.getBlue();
	}

	f32 startX = (f32)minX + 0.5f;
	f32 startY = (f32)minY + 0.5f;

	f32 row_w0 = ((v1.x - startX) * (v2.y - startY) - (v1.y - startY) * (v2.x - startX)) * invArea;
	f32 row_w1 = ((v2.x - startX) * (v0.y - startY) - (v2.y - startY) * (v0.x - startX)) * invArea;

	f32 row_z = row_w0 * v0.z + row_w1 * v1.z + (1.0f - row_w0 - row_w1) * v2.z;
	f32 row_invW = row_w0 * v0.invW + row_w1 * v1.invW + (1.0f - row_w0 - row_w1) * v2.invW;
	f32 row_U = row_w0 * v0.u + row_w1 * v1.u + (1.0f - row_w0 - row_w1) * v2.u;
	f32 row_V = row_w0 * v0.v + row_w1 * v1.v + (1.0f - row_w0 - row_w1) * v2.v;

	f32 row_cr = 0, row_cg = 0, row_cb = 0, row_ca = 0;
	if (!isFlatColor)
	{
		f32 row_w2 = 1.0f - row_w0 - row_w1;
		row_cr = row_w0 * c0r + row_w1 * c1r + row_w2 * c2r;
		row_cg = row_w0 * c0g + row_w1 * c1g + row_w2 * c2g;
		row_cb = row_w0 * c0b + row_w1 * c1b + row_w2 * c2b;
		row_ca = row_w0 * c0a + row_w1 * c1a + row_w2 * c2a;
	}

	for (s32 y = minY; y < maxY; ++y)
	{
		u32* dRow = dstData + y * dstPitch;
		f32* zRow = DepthBuffer + y * RenderTargetSize.Width;

		f32 w0 = row_w0;
		f32 w1 = row_w1;
		f32 z = row_z;
		f32 invW = row_invW;
		f32 U = row_U;
		f32 V = row_V;

		f32 cr = row_cr;
		f32 cg = row_cg;
		f32 cb = row_cb;
		f32 ca = row_ca;

		for (s32 x = minX; x < maxX; ++x)
		{
			f32 w2 = 1.0f - w0 - w1;

			if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
			{
				if (z < zRow[x] && z >= 0.0f)
				{
					u32 vertCol = flatVertCol;
					if (!isFlatColor)
					{
						u32 r = (u32)(cr + 0.5f);
						u32 g = (u32)(cg + 0.5f);
						u32 b = (u32)(cb + 0.5f);
						u32 a = (u32)(ca + 0.5f);
						if (r > 255) r = 255;
						if (g > 255) g = 255;
						if (b > 255) b = 255;
						if (a > 255) a = 255;
						vertCol = (a << 24) | (r << 16) | (g << 8) | b;
					}

					u32 pixel = vertCol;

					if (hasTexture)
					{
						f32 recipW = (invW != 0.0f) ? (1.0f / invW) : 1.0f;
						f32 fu = U * recipW * fTexWidth;
						f32 fv = V * recipW * fTexHeight;
						s32 tx = (s32)floorf(fu);
						s32 ty = (s32)floorf(fv);

						if (isPow2)
						{
							tx &= maskX;
							ty &= maskY;
						}
						else
						{
							tx = (tx % (s32)texWidth + (s32)texWidth) % (s32)texWidth;
							ty = (ty % (s32)texHeight + (s32)texHeight) % (s32)texHeight;
						}

						u32 texColor = srcData[ty * srcPitch + tx];

						if (isAlphaTest && ((texColor >> 24) & 0xFF) < 128)
						{
							goto step_pixel;
						}

						if (vertCol == 0xFFFFFFFF)
							pixel = texColor;
						else
							pixel = modulateColorFast(texColor, vertCol);
					}

					if (zWriteEnable)
						zRow[x] = z;

					if (fogActive)
					{
						f32 recipW = (invW != 0.0f) ? (1.0f / invW) : 1.0f;
						f32 fogFactor = (FogEnd - recipW) * invFogRange;
						if (fogFactor < 0.0f) fogFactor = 0.0f;
						else if (fogFactor > 1.0f) fogFactor = 1.0f;

						u32 pr = (pixel >> 16) & 0xFF;
						u32 pg = (pixel >> 8)  & 0xFF;
						u32 pb = pixel         & 0xFF;

						u32 fr = (u32)(pr * fogFactor + fogColorRed   * (1.0f - fogFactor) + 0.5f);
						u32 fg = (u32)(pg * fogFactor + fogColorGreen * (1.0f - fogFactor) + 0.5f);
						u32 fb = (u32)(pb * fogFactor + fogColorBlue  * (1.0f - fogFactor) + 0.5f);

						pixel = (pixel & 0xFF000000) | (fr << 16) | (fg << 8) | fb;
					}

					if (isTransparentMaterial || ((pixel >> 24) & 0xFF) < 255)
						dRow[x] = blendPixel32(pixel, dRow[x]);
					else
						dRow[x] = pixel;
				}
			}

step_pixel:
			w0 += dw0_dx;
			w1 += dw1_dx;
			z += dz_dx;
			invW += dinvW_dx;
			U += dU_dx;
			V += dV_dx;
			if (!isFlatColor)
			{
				cr += dr_dx;
				cg += dg_dx;
				cb += db_dx;
				ca += da_dx;
			}
		}

		row_w0 += dw0_dy;
		row_w1 += dw1_dy;
		row_z += dz_dy;
		row_invW += dinvW_dy;
		row_U += dU_dy;
		row_V += dV_dy;
		if (!isFlatColor)
		{
			row_cr += dr_dy;
			row_cg += dg_dy;
			row_cb += db_dy;
			row_ca += da_dy;
		}
	}
}

void CCuckooDriver::drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
											const void* indexList, u32 primitiveCount,
											E_VERTEX_TYPE vType, scene::E_PRIMITIVE_TYPE pType,
											E_INDEX_TYPE iType)
{
	if (!RenderTargetSurface || !vertices || !indexList || primitiveCount == 0)
		return;

	CNullDriver::drawVertexPrimitiveList(vertices, vertexCount, indexList, primitiveCount, vType, pType, iType);

	core::matrix4 m(TransformationMatrix[ETS_PROJECTION]);
	m *= TransformationMatrix[ETS_VIEW];
	m *= TransformationMatrix[ETS_WORLD];

	s32 halfW = ViewPort.getWidth() / 2;
	s32 halfH = ViewPort.getHeight() / 2;
	s32 midX = ViewPort.UpperLeftCorner.X + halfW;
	s32 midY = ViewPort.UpperLeftCorner.Y + halfH;

	if (TransformedVerts.allocated_size() < vertexCount)
		TransformedVerts.reallocate(vertexCount);
	TransformedVerts.set_used(vertexCount);

	if (ClipVerts.allocated_size() < vertexCount)
		ClipVerts.reallocate(vertexCount);
	ClipVerts.set_used(vertexCount);

	core::vector3df lightDir(-0.3f, 0.8f, -0.5f);
	lightDir.normalize();

	const core::matrix4& worldMat = TransformationMatrix[ETS_WORLD];

	f32 pos[4];
	for (u32 i = 0; i < vertexCount; ++i)
	{
		SClipVertex cv;
		if (vType == EVT_STANDARD)
		{
			const S3DVertex* v = &((const S3DVertex*)vertices)[i];
			pos[0] = v->Pos.X; pos[1] = v->Pos.Y; pos[2] = v->Pos.Z; pos[3] = 1.0f;
			cv.u = v->TCoords.X; cv.v = v->TCoords.Y;
			cv.color = v->Color;
			if (Material.Lighting)
			{
				core::vector3df worldNorm = v->Normal;
				worldMat.rotateVect(worldNorm);
				worldNorm.normalize();
				f32 dot = worldNorm.dotProduct(lightDir);
				cv.lightFactor = std::max(0.2f, std::min(1.0f, dot));
			}
			else
			{
				cv.lightFactor = 1.0f;
			}
		}
		else if (vType == EVT_2TCOORDS)
		{
			const S3DVertex2TCoords* v = &((const S3DVertex2TCoords*)vertices)[i];
			pos[0] = v->Pos.X; pos[1] = v->Pos.Y; pos[2] = v->Pos.Z; pos[3] = 1.0f;
			cv.u = v->TCoords.X; cv.v = v->TCoords.Y;
			cv.color = v->Color;
			if (Material.Lighting)
			{
				core::vector3df worldNorm = v->Normal;
				worldMat.rotateVect(worldNorm);
				worldNorm.normalize();
				f32 dot = worldNorm.dotProduct(lightDir);
				cv.lightFactor = std::max(0.2f, std::min(1.0f, dot));
			}
			else
			{
				cv.lightFactor = 1.0f;
			}
		}
		else
		{
			const S3DVertexTangents* v = &((const S3DVertexTangents*)vertices)[i];
			pos[0] = v->Pos.X; pos[1] = v->Pos.Y; pos[2] = v->Pos.Z; pos[3] = 1.0f;
			cv.u = v->TCoords.X; cv.v = v->TCoords.Y;
			cv.color = v->Color;
			if (Material.Lighting)
			{
				core::vector3df worldNorm = v->Normal;
				worldMat.rotateVect(worldNorm);
				worldNorm.normalize();
				f32 dot = worldNorm.dotProduct(lightDir);
				cv.lightFactor = std::max(0.2f, std::min(1.0f, dot));
			}
			else
			{
				cv.lightFactor = 1.0f;
			}
		}

		m.multiplyWith1x4Matrix(pos);

		cv.x = pos[0];
		cv.y = pos[1];
		cv.z = pos[2];
		cv.w = pos[3];
		ClipVerts[i] = cv;

		if (pos[3] > 0.001f)
		{
			TransformedVerts[i] = projectClipVertex(cv, midX, midY, halfW, halfH);
		}
		else
		{
			TransformedVerts[i].w = -1.0f;
		}
	}

	u32* dstData = (u32*)RenderTargetSurface->lock();
	if (!dstData) return;
	u32 dstPitch = RenderTargetSurface->getPitch() / 4;

	u32* srcData = 0;
	u32 texWidth = 0, texHeight = 0, srcPitch = 0;
	if (CurrentTextureImage)
	{
		srcData = (u32*)CurrentTextureImage->lock();
		texWidth = CurrentTextureImage->getDimension().Width;
		texHeight = CurrentTextureImage->getDimension().Height;
		srcPitch = CurrentTextureImage->getPitch() / 4;
	}

	const u16* indices16 = (const u16*)indexList;
	const u32* indices32 = (const u32*)indexList;

	if (pType == scene::EPT_TRIANGLES)
	{
		for (u32 p = 0; p < primitiveCount; ++p)
		{
			u32 idx0 = (iType == EIT_16BIT) ? indices16[p * 3 + 0] : indices32[p * 3 + 0];
			u32 idx1 = (iType == EIT_16BIT) ? indices16[p * 3 + 1] : indices32[p * 3 + 1];
			u32 idx2 = (iType == EIT_16BIT) ? indices16[p * 3 + 2] : indices32[p * 3 + 2];

			const SClipVertex& c0 = ClipVerts[idx0];
			const SClipVertex& c1 = ClipVerts[idx1];
			const SClipVertex& c2 = ClipVerts[idx2];

			bool in0 = (c0.w > 0.001f);
			bool in1 = (c1.w > 0.001f);
			bool in2 = (c2.w > 0.001f);

			int inCount = (in0 ? 1 : 0) + (in1 ? 1 : 0) + (in2 ? 1 : 0);

			if (inCount == 3)
			{
				renderTriangle3D(TransformedVerts[idx0], TransformedVerts[idx1], TransformedVerts[idx2],
				                 dstData, dstPitch, srcData, texWidth, texHeight, srcPitch);
			}
			else if (inCount == 1)
			{
				const SClipVertex* vA;
				const SClipVertex* vB;
				const SClipVertex* vC;
				if (in0)      { vA = &c0; vB = &c1; vC = &c2; }
				else if (in1) { vA = &c1; vB = &c2; vC = &c0; }
				else          { vA = &c2; vB = &c0; vC = &c1; }

				SClipVertex i0 = clipEdge(*vA, *vB, 0.001f);
				SClipVertex i1 = clipEdge(*vA, *vC, 0.001f);

				STransformedVertex tA = projectClipVertex(*vA, midX, midY, halfW, halfH);
				STransformedVertex t0 = projectClipVertex(i0, midX, midY, halfW, halfH);
				STransformedVertex t1 = projectClipVertex(i1, midX, midY, halfW, halfH);

				renderTriangle3D(tA, t0, t1,
				                 dstData, dstPitch, srcData, texWidth, texHeight, srcPitch);
			}
			else if (inCount == 2)
			{
				const SClipVertex* vA;
				const SClipVertex* vB;
				const SClipVertex* vC;
				if (!in2)      { vA = &c0; vB = &c1; vC = &c2; }
				else if (!in0) { vA = &c1; vB = &c2; vC = &c0; }
				else           { vA = &c2; vB = &c0; vC = &c1; }

				SClipVertex i0 = clipEdge(*vB, *vC, 0.001f);
				SClipVertex i1 = clipEdge(*vA, *vC, 0.001f);

				STransformedVertex tA = projectClipVertex(*vA, midX, midY, halfW, halfH);
				STransformedVertex tB = projectClipVertex(*vB, midX, midY, halfW, halfH);
				STransformedVertex t0 = projectClipVertex(i0, midX, midY, halfW, halfH);
				STransformedVertex t1 = projectClipVertex(i1, midX, midY, halfW, halfH);

				renderTriangle3D(tA, tB, t0,
				                 dstData, dstPitch, srcData, texWidth, texHeight, srcPitch);
				renderTriangle3D(tA, t0, t1,
				                 dstData, dstPitch, srcData, texWidth, texHeight, srcPitch);
			}
		}
	}
	else if (pType == scene::EPT_LINES)
	{
		for (u32 p = 0; p < primitiveCount; ++p)
		{
			u32 idx0 = (iType == EIT_16BIT) ? indices16[p * 2 + 0] : indices32[p * 2 + 0];
			u32 idx1 = (iType == EIT_16BIT) ? indices16[p * 2 + 1] : indices32[p * 2 + 1];

			const SClipVertex& c0 = ClipVerts[idx0];
			const SClipVertex& c1 = ClipVerts[idx1];

			bool in0 = (c0.w > 0.001f);
			bool in1 = (c1.w > 0.001f);

			if (in0 && in1)
			{
				draw3DLineTransformed(TransformedVerts[idx0], TransformedVerts[idx1], c0.color);
			}
			else if (in0 && !in1)
			{
				SClipVertex i1 = clipEdge(c0, c1, 0.001f);
				STransformedVertex t1 = projectClipVertex(i1, midX, midY, halfW, halfH);
				draw3DLineTransformed(TransformedVerts[idx0], t1, c0.color);
			}
			else if (!in0 && in1)
			{
				SClipVertex i0 = clipEdge(c1, c0, 0.001f);
				STransformedVertex t0 = projectClipVertex(i0, midX, midY, halfW, halfH);
				draw3DLineTransformed(t0, TransformedVerts[idx1], c1.color);
			}
		}
	}

	if (CurrentTextureImage)
		CurrentTextureImage->unlock();
	RenderTargetSurface->unlock();
}

void CCuckooDriver::setFog(SColor color, E_FOG_TYPE fogType,
                          f32 start, f32 end, f32 density,
                          bool pixelFog, bool rangeFog)
{
    CNullDriver::setFog(color, fogType, start, end, density, pixelFog, rangeFog);
    FogColor = color;
    FogStart = start;
    FogEnd = end;
    FogEnable = true;
}

} // end namespace video
} // end namespace irr

namespace irr
{
namespace video
{

//! creates a cuckoovideo video driver
IVideoDriver* createCuckooDriver(const core::dimension2d<u32>& windowSize,
								bool fullscreen,
								io::IFileSystem* io,
								video::IImagePresenter* presenter)
{
	#ifdef _IRR_COMPILE_WITH_CUCKOOVIDEO_
	return new CCuckooDriver(windowSize, fullscreen, io, presenter);
	#else
	return 0;
	#endif
}

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_CUCKOOVIDEO_
