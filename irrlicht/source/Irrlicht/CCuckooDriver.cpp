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

	u32 invSa = 256 - sa;
	sa += 1;

	u32 s_rb = src & 0x00FF00FF;
	u32 d_rb = dst & 0x00FF00FF;
	u32 rb = (((s_rb * sa + d_rb * invSa) >> 8) & 0x00FF00FF);

	u32 s_g = src & 0x0000FF00;
	u32 d_g = dst & 0x0000FF00;
	u32 g = (((s_g * sa + d_g * invSa) >> 8) & 0x0000FF00);

	return 0xFF000000 | rb | g;
}

inline u32 modulateColorFast(u32 c0, u32 c1)
{
	if (c1 == 0xFFFFFFFF) return c0;
	if (c0 == 0) return 0;

	u32 rb0 = c0 & 0x00FF00FF;
	u32 rb1 = c1 & 0x00FF00FF;
	u32 r = ((rb0 >> 16) * (rb1 >> 16) + 128);
	r = (r + (r >> 8)) >> 8;
	u32 b = ((rb0 & 0xFF) * (rb1 & 0xFF) + 128);
	b = (b + (b >> 8)) >> 8;

	u32 g0  = (c0 >> 8) & 0xFF;
	u32 g1  = (c1 >> 8) & 0xFF;
	u32 g = (g0 * g1 + 128);
	g = (g + (g >> 8)) >> 8;

	u32 a0  = (c0 >> 24) & 0xFF;
	u32 a1  = (c1 >> 24) & 0xFF;
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
		DepthBuffer = new u32[DepthBufferSize];
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
		memset(DepthBuffer, 0xFF, DepthBufferSize * sizeof(u32));
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
	s32 rectW = pos.getWidth();
	s32 rectH = pos.getHeight();
	if (rectW <= 0 || rectH <= 0) return;

	f32 invH = 1.0f / (f32)rectH;

	for (s32 y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y)
	{
		f32 fy = (f32)(y - pos.UpperLeftCorner.Y) * invH;
		SColor colLeft = colorLeftUp.getInterpolated(colorLeftDown, 1.0f - fy);
		SColor colRight = colorRightUp.getInterpolated(colorRightDown, 1.0f - fy);

		s32 leftR = colLeft.getRed() << 16;
		s32 leftG = colLeft.getGreen() << 16;
		s32 leftB = colLeft.getBlue() << 16;
		s32 leftA = colLeft.getAlpha() << 16;

		s32 rightR = colRight.getRed() << 16;
		s32 rightG = colRight.getGreen() << 16;
		s32 rightB = colRight.getBlue() << 16;
		s32 rightA = colRight.getAlpha() << 16;

		s32 stepR = (rightR - leftR) / rectW;
		s32 stepG = (rightG - leftG) / rectW;
		s32 stepB = (rightB - leftB) / rectW;
		s32 stepA = (rightA - leftA) / rectW;

		s32 offsetX = r.UpperLeftCorner.X - pos.UpperLeftCorner.X;
		s32 curR = leftR + stepR * offsetX;
		s32 curG = leftG + stepG * offsetX;
		s32 curB = leftB + stepB * offsetX;
		s32 curA = leftA + stepA * offsetX;

		u32* row = dstData + y * pitch;
		for (s32 x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x)
		{
			u32 a = curA >> 16;
			u32 red = curR >> 16;
			u32 green = curG >> 16;
			u32 blue = curB >> 16;
			u32 c = (a << 24) | (red << 16) | (green << 8) | blue;
			row[x] = blendPixel32(c, row[x]);

			curR += stepR;
			curG += stepG;
			curB += stepB;
			curA += stepA;
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

		s32 stepX = (s32)(scaleX * 65536.0f);
		s32 stepY = (s32)(scaleY * 65536.0f);

		s32 startSrcX = (s32)(((clippedDest.UpperLeftCorner.X - destRect.UpperLeftCorner.X) * scaleX + sourceRect.UpperLeftCorner.X) * 65536.0f);
		s32 curSrcY = (s32)(((clippedDest.UpperLeftCorner.Y - destRect.UpperLeftCorner.Y) * scaleY + sourceRect.UpperLeftCorner.Y) * 65536.0f);

		s32 maxSrcX = (s32)srcImg->getDimension().Width - 1;
		s32 maxSrcY = (s32)srcImg->getDimension().Height - 1;

		u32 tintColor = (colors) ? colors[0].color : 0xFFFFFFFF;
		bool hasTint = (tintColor != 0xFFFFFFFF);

		for (s32 y = clippedDest.UpperLeftCorner.Y; y < clippedDest.LowerRightCorner.Y; ++y)
		{
			s32 srcY = curSrcY >> 16;
			if (srcY < 0) srcY = 0;
			else if (srcY > maxSrcY) srcY = maxSrcY;

			u32* sRow = srcData + srcY * srcPitch;
			u32* dRow = dstData + y * dstPitch;

			s32 curSrcX = startSrcX;
			for (s32 x = clippedDest.UpperLeftCorner.X; x < clippedDest.LowerRightCorner.X; ++x)
			{
				s32 srcX = curSrcX >> 16;
				if (srcX < 0) srcX = 0;
				else if (srcX > maxSrcX) srcX = maxSrcX;

				u32 pixel = sRow[srcX];
				if (hasTint)
					pixel = modulateColor(pixel, tintColor);

				if (useAlphaChannelOfTexture)
					dRow[x] = blendPixel32(pixel, dRow[x]);
				else
					dRow[x] = pixel;

				curSrcX += stepX;
			}
			curSrcY += stepY;
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
	u32 z0 = (v0.z >= 0.0f) ? (u32)(v0.z * 16777216.0f) : 0;

	s32 x1 = (s32)v1.x;
	s32 y1 = (s32)v1.y;
	u32 z1 = (v1.z >= 0.0f) ? (u32)(v1.z * 16777216.0f) : 0;

	s32 dx = abs(x1 - x0);
	s32 dy = abs(y1 - y0);
	s32 sx = (x0 < x1) ? 1 : -1;
	s32 sy = (y0 < y1) ? 1 : -1;
	s32 err = dx - dy;

	s32 steps = std::max(dx, dy);
	s32 dz = steps > 0 ? (s32)(((long long)z1 - z0) / steps) : 0;
	u32 curZ = z0;

	u32* dstData = (u32*)RenderTargetSurface->lock();
	if (!dstData) return;
	u32 dstPitch = RenderTargetSurface->getPitch() / 4;

	while (true)
	{
		if (x0 >= ViewPort.UpperLeftCorner.X && x0 < ViewPort.LowerRightCorner.X &&
		    y0 >= ViewPort.UpperLeftCorner.Y && y0 < ViewPort.LowerRightCorner.Y)
		{
			u32* zPtr = DepthBuffer + y0 * RenderTargetSize.Width + x0;
			if (curZ <= *zPtr + 16777)
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

	const core::matrix4& proj = TransformationMatrix[ETS_PROJECTION];
	f32 wNear = 0.1f;
	if (proj[11] != 0.0f && fabsf(proj[10]) > 0.0001f)
	{
		f32 zNear = fabsf(proj[14] / proj[10]);
		if (zNear > 0.01f && zNear < 5.0f)
			wNear = zNear;
	}

	if (p0[3] < wNear || p1[3] < wNear)
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
	tv.invW = (cv.w > 0.0001f) ? (1.0f / cv.w) : 10000.0f;
	tv.x = (f32)midX + (cv.x * tv.invW * (f32)halfW);
	tv.y = (f32)midY - (cv.y * tv.invW * (f32)halfH);
	tv.z = cv.z * tv.invW;
	if (tv.z < 0.0f) tv.z = 0.0f;
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

	f32 dz_dx = dw0_dx * (v0.z - v2.z) + dw1_dx * (v1.z - v2.z);
	f32 dz_dy = dw0_dy * (v0.z - v2.z) + dw1_dy * (v1.z - v2.z);

	f32 dinvW_dx = dw0_dx * (v0.invW - v2.invW) + dw1_dx * (v1.invW - v2.invW);
	f32 dinvW_dy = dw0_dy * (v0.invW - v2.invW) + dw1_dy * (v1.invW - v2.invW);

	f32 dU_dx = dw0_dx * (v0.u - v2.u) + dw1_dx * (v1.u - v2.u);
	f32 dU_dy = dw0_dy * (v0.u - v2.u) + dw1_dy * (v1.u - v2.u);

	f32 dV_dx = dw0_dx * (v0.v - v2.v) + dw1_dx * (v1.v - v2.v);
	f32 dV_dy = dw0_dy * (v0.v - v2.v) + dw1_dy * (v1.v - v2.v);

	bool hasTexture = (srcData && texWidth > 0 && texHeight > 0);
	bool isPow2 = hasTexture && ((texWidth & (texWidth - 1)) == 0) && ((texHeight & (texHeight - 1)) == 0);
	u32 maskX = texWidth - 1;
	u32 maskY = texHeight - 1;
	u32 pitchShift = 0;
	if (isPow2 && ((srcPitch & (srcPitch - 1)) == 0))
	{
		u32 temp = srcPitch;
		while (temp > 1) { temp >>= 1; pitchShift++; }
	}
	f32 fTexWidth = (f32)texWidth;
	f32 fTexHeight = (f32)texHeight;
	bool isAlphaTest = (Material.MaterialType == EMT_TRANSPARENT_ALPHA_CHANNEL_REF);
	bool zWriteEnable = Material.ZWriteEnable;
	bool isTransparentMaterial = Material.isTransparent();

	bool isFlatColor = (v0.color == v1.color && v1.color == v2.color &&
	                    v0.lightFactor == v1.lightFactor && v1.lightFactor == v2.lightFactor);
	u32 flatVertCol = 0xFFFFFFFF;
	bool isMonoLight = false;
	u32 monoL = 255;
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
		if (flatVertCol != 0xFFFFFFFF && r == g && g == b)
		{
			isMonoLight = true;
			monoL = r;
		}
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

		dr_dx = dw0_dx * (c0r - c2r) + dw1_dx * (c1r - c2r);
		dr_dy = dw0_dy * (c0r - c2r) + dw1_dy * (c1r - c2r);
		dg_dx = dw0_dx * (c0g - c2g) + dw1_dx * (c1g - c2g);
		dg_dy = dw0_dy * (c0g - c2g) + dw1_dy * (c1g - c2g);
		db_dx = dw0_dx * (c0b - c2b) + dw1_dx * (c1b - c2b);
		db_dy = dw0_dy * (c0b - c2b) + dw1_dy * (c1b - c2b);
		da_dx = dw0_dx * (c0a - c2a) + dw1_dx * (c1a - c2a);
		da_dy = dw0_dy * (c0a - c2a) + dw1_dy * (c1a - c2a);
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

	const STransformedVertex* p0 = &v0;
	const STransformedVertex* p1 = &v1;
	const STransformedVertex* p2 = &v2;

	if (p0->y > p1->y) std::swap(p0, p1);
	if (p0->y > p2->y) std::swap(p0, p2);
	if (p1->y > p2->y) std::swap(p1, p2);

	s32 yMin = (s32)ceilf(p0->y);
	s32 yMax = (s32)ceilf(p2->y);

	yMin = std::max(yMin, ViewPort.UpperLeftCorner.Y);
	yMax = std::min(yMax, ViewPort.LowerRightCorner.Y);

	if (yMin >= yMax)
		return;

	f32 dy02 = p2->y - p0->y;
	if (dy02 <= 0.0f)
		return;

	f32 cross = (p1->x - p0->x) * (p2->y - p0->y) - (p1->y - p0->y) * (p2->x - p0->x);
	if (cross == 0.0f)
		return;

	bool longEdgeIsLeft = (cross > 0.0f);

	f32 dx02 = (p2->x - p0->x) / dy02;
	f32 dy01 = p1->y - p0->y;
	f32 dx01 = (dy01 > 0.0001f) ? ((p1->x - p0->x) / dy01) : 0.0f;
	f32 dy12 = p2->y - p1->y;
	f32 dx12 = (dy12 > 0.0001f) ? ((p2->x - p1->x) / dy12) : 0.0f;

	s32 yMid = (s32)ceilf(p1->y);

	long long cur_xLong = (long long)((p0->x + dx02 * ((f32)yMin + 0.5f - p0->y)) * 65536.0f);
	long long step_xLong = (long long)(dx02 * 65536.0f);

	long long cur_xShort = (long long)((p0->x + dx01 * ((f32)yMin + 0.5f - p0->y)) * 65536.0f);
	long long step_xShort = (long long)(dx01 * 65536.0f);
	bool switchedToBot = false;

	for (s32 y = yMin; y < yMax; ++y)
	{
		if (y >= yMid && !switchedToBot)
		{
			cur_xShort = (long long)((p1->x + dx12 * ((f32)y + 0.5f - p1->y)) * 65536.0f);
			step_xShort = (long long)(dx12 * 65536.0f);
			switchedToBot = true;
		}

		long long raw_xL = longEdgeIsLeft ? ((cur_xLong + 65535LL) >> 16) : ((cur_xShort + 65535LL) >> 16);
		long long raw_xR = longEdgeIsLeft ? ((cur_xShort + 65535LL) >> 16) : ((cur_xLong + 65535LL) >> 16);

		cur_xLong += step_xLong;
		cur_xShort += step_xShort;

		long long clamp_minX = (long long)ViewPort.UpperLeftCorner.X;
		long long clamp_maxX = (long long)ViewPort.LowerRightCorner.X;

		long long cStartX = std::max(raw_xL, clamp_minX);
		long long cEndX = std::min(raw_xR, clamp_maxX);

		if (cStartX >= cEndX)
			continue;

		s32 startX = (s32)cStartX;
		s32 endX = (s32)cEndX;

		u32* dRow = dstData + y * dstPitch;
		u32* zRow = DepthBuffer + y * RenderTargetSize.Width;

		f32 dx = (f32)startX + 0.5f - v0.x;
		f32 dy = (f32)y + 0.5f - v0.y;

		f32 invW = v0.invW + dinvW_dx * dx + dinvW_dy * dy;
		f32 U = v0.u + dU_dx * dx + dU_dy * dy;
		f32 V = v0.v + dV_dx * dx + dV_dy * dy;
		f32 curZ = v0.z + dz_dx * dx + dz_dy * dy;
		if (curZ < 0.0f) curZ = 0.0f;
		u32 curZ_fixed = (u32)(curZ * 16777216.0f);
		s32 dz_dx_fixed = (s32)(dz_dx * 16777216.0f);

		f32 cr = 0, cg = 0, cb = 0, ca = 0;
		if (!isFlatColor)
		{
			cr = c0r + dr_dx * dx + dr_dy * dy;
			cg = c0g + dg_dx * dx + dg_dy * dy;
			cb = c0b + db_dx * dx + db_dy * dy;
			ca = c0a + da_dx * dx + da_dy * dy;
		}

		s32 x = startX;
		s32 count = endX - startX;

		f32 W0 = (invW > 1e-7f) ? (1.0f / invW) : 1000000.0f;
		f32 u_0 = U * W0;
		f32 v_0 = V * W0;

		while (count > 0)
		{
			s32 k = (count >= 16) ? 16 : count;
			f32 fk = (f32)k;

			f32 invW_next = invW + fk * dinvW_dx;
			f32 W1 = (invW_next > 1e-7f) ? (1.0f / invW_next) : 1000000.0f;

			f32 U_next = U + fk * dU_dx;
			f32 V_next = V + fk * dV_dx;

			f32 u_1 = U_next * W1;
			f32 v_1 = V_next * W1;

			u32* zPtr = zRow + x;
			u32* dPtr = dRow + x;

			if (hasTexture && isPow2 && !isAlphaTest && !isTransparentMaterial && !fogActive && isFlatColor)
			{
				s32 u_fix = (s32)(u_0 * fTexWidth * 65536.0f);
				s32 v_fix = (s32)(v_0 * fTexHeight * 65536.0f);
				s32 u1_fix = (s32)(u_1 * fTexWidth * 65536.0f);
				s32 v1_fix = (s32)(v_1 * fTexHeight * 65536.0f);

				s32 du_fix = (k == 16) ? ((u1_fix - u_fix) >> 4) : ((k == 8) ? ((u1_fix - u_fix) >> 3) : ((u1_fix - u_fix) / k));
				s32 dv_fix = (k == 16) ? ((v1_fix - v_fix) >> 4) : ((k == 8) ? ((v1_fix - v_fix) >> 3) : ((v1_fix - v_fix) / k));

				if (pitchShift != 0)
				{
					if (flatVertCol == 0xFFFFFFFF)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								dPtr[i] = srcData[(ty << pitchShift) + tx];
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else if (isMonoLight)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[(ty << pitchShift) + tx];
								u32 rb = (((texColor & 0x00FF00FF) * monoL) >> 8) & 0x00FF00FF;
								u32 g  = (((texColor & 0x0000FF00) * monoL) >> 8) & 0x0000FF00;
								dPtr[i] = 0xFF000000 | rb | g;
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[(ty << pitchShift) + tx];
								dPtr[i] = modulateColorFast(texColor, flatVertCol);
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
				}
				else
				{
					if (flatVertCol == 0xFFFFFFFF)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								dPtr[i] = srcData[ty * srcPitch + tx];
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else if (isMonoLight)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[ty * srcPitch + tx];
								u32 rb = (((texColor & 0x00FF00FF) * monoL) >> 8) & 0x00FF00FF;
								u32 g  = (((texColor & 0x0000FF00) * monoL) >> 8) & 0x0000FF00;
								dPtr[i] = 0xFF000000 | rb | g;
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[ty * srcPitch + tx];
								dPtr[i] = modulateColorFast(texColor, flatVertCol);
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
				}
			}
			else if (hasTexture && !isPow2 && !isAlphaTest && !isTransparentMaterial && !fogActive && isFlatColor)
			{
				s32 u_fix = (s32)(u_0 * 65536.0f);
				s32 v_fix = (s32)(v_0 * 65536.0f);
				s32 u1_fix = (s32)(u_1 * 65536.0f);
				s32 v1_fix = (s32)(v_1 * 65536.0f);

				s32 du_fix = (k == 16) ? ((u1_fix - u_fix) >> 4) : ((k == 8) ? ((u1_fix - u_fix) >> 3) : ((u1_fix - u_fix) / k));
				s32 dv_fix = (k == 16) ? ((v1_fix - v_fix) >> 4) : ((k == 8) ? ((v1_fix - v_fix) >> 3) : ((v1_fix - v_fix) / k));

				if (flatVertCol == 0xFFFFFFFF)
				{
					for (s32 i = 0; i < k; ++i)
					{
						if (curZ_fixed < zPtr[i])
						{
							if (zWriteEnable) zPtr[i] = curZ_fixed;
							u32 u_frac = (u32)u_fix & 0xFFFF;
							u32 v_frac = (u32)v_fix & 0xFFFF;
							u32 tx = (u_frac * texWidth) >> 16;
							u32 ty = (v_frac * texHeight) >> 16;
							dPtr[i] = srcData[ty * srcPitch + tx];
						}
						curZ_fixed += dz_dx_fixed;
						u_fix += du_fix;
						v_fix += dv_fix;
					}
				}
				else if (isMonoLight)
				{
					for (s32 i = 0; i < k; ++i)
					{
						if (curZ_fixed < zPtr[i])
						{
							if (zWriteEnable) zPtr[i] = curZ_fixed;
							u32 u_frac = (u32)u_fix & 0xFFFF;
							u32 v_frac = (u32)v_fix & 0xFFFF;
							u32 tx = (u_frac * texWidth) >> 16;
							u32 ty = (v_frac * texHeight) >> 16;
							u32 texColor = srcData[ty * srcPitch + tx];
							u32 rb = (((texColor & 0x00FF00FF) * monoL) >> 8) & 0x00FF00FF;
							u32 g  = (((texColor & 0x0000FF00) * monoL) >> 8) & 0x0000FF00;
							dPtr[i] = 0xFF000000 | rb | g;
						}
						curZ_fixed += dz_dx_fixed;
						u_fix += du_fix;
						v_fix += dv_fix;
					}
				}
				else
				{
					for (s32 i = 0; i < k; ++i)
					{
						if (curZ_fixed < zPtr[i])
						{
							if (zWriteEnable) zPtr[i] = curZ_fixed;
							u32 u_frac = (u32)u_fix & 0xFFFF;
							u32 v_frac = (u32)v_fix & 0xFFFF;
							u32 tx = (u_frac * texWidth) >> 16;
							u32 ty = (v_frac * texHeight) >> 16;
							u32 texColor = srcData[ty * srcPitch + tx];
							dPtr[i] = modulateColorFast(texColor, flatVertCol);
						}
						curZ_fixed += dz_dx_fixed;
						u_fix += du_fix;
						v_fix += dv_fix;
					}
				}
			}
			else if (hasTexture && isPow2 && isAlphaTest && !isTransparentMaterial && !fogActive && isFlatColor)
			{
				s32 u_fix = (s32)(u_0 * fTexWidth * 65536.0f);
				s32 v_fix = (s32)(v_0 * fTexHeight * 65536.0f);
				s32 u1_fix = (s32)(u_1 * fTexWidth * 65536.0f);
				s32 v1_fix = (s32)(v_1 * fTexHeight * 65536.0f);

				s32 du_fix = (k == 16) ? ((u1_fix - u_fix) >> 4) : ((k == 8) ? ((u1_fix - u_fix) >> 3) : ((u1_fix - u_fix) / k));
				s32 dv_fix = (k == 16) ? ((v1_fix - v_fix) >> 4) : ((k == 8) ? ((v1_fix - v_fix) >> 3) : ((v1_fix - v_fix) / k));

				if (pitchShift != 0)
				{
					if (flatVertCol == 0xFFFFFFFF)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[(ty << pitchShift) + tx];
								if ((texColor >> 24) >= 128)
								{
									if (zWriteEnable) zPtr[i] = curZ_fixed;
									dPtr[i] = texColor;
								}
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else if (isMonoLight)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[(ty << pitchShift) + tx];
								if ((texColor >> 24) >= 128)
								{
									if (zWriteEnable) zPtr[i] = curZ_fixed;
									u32 rb = (((texColor & 0x00FF00FF) * monoL) >> 8) & 0x00FF00FF;
									u32 g  = (((texColor & 0x0000FF00) * monoL) >> 8) & 0x0000FF00;
									dPtr[i] = 0xFF000000 | rb | g;
								}
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[(ty << pitchShift) + tx];
								if ((texColor >> 24) >= 128)
								{
									if (zWriteEnable) zPtr[i] = curZ_fixed;
									dPtr[i] = modulateColorFast(texColor, flatVertCol);
								}
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
				}
				else
				{
					if (flatVertCol == 0xFFFFFFFF)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[ty * srcPitch + tx];
								if ((texColor >> 24) >= 128)
								{
									if (zWriteEnable) zPtr[i] = curZ_fixed;
									dPtr[i] = texColor;
								}
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else if (isMonoLight)
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[ty * srcPitch + tx];
								if ((texColor >> 24) >= 128)
								{
									if (zWriteEnable) zPtr[i] = curZ_fixed;
									u32 rb = (((texColor & 0x00FF00FF) * monoL) >> 8) & 0x00FF00FF;
									u32 g  = (((texColor & 0x0000FF00) * monoL) >> 8) & 0x0000FF00;
									dPtr[i] = 0xFF000000 | rb | g;
								}
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
					else
					{
						for (s32 i = 0; i < k; ++i)
						{
							if (curZ_fixed < zPtr[i])
							{
								u32 tx = (u_fix >> 16) & maskX;
								u32 ty = (v_fix >> 16) & maskY;
								u32 texColor = srcData[ty * srcPitch + tx];
								if ((texColor >> 24) >= 128)
								{
									if (zWriteEnable) zPtr[i] = curZ_fixed;
									dPtr[i] = modulateColorFast(texColor, flatVertCol);
								}
							}
							curZ_fixed += dz_dx_fixed;
							u_fix += du_fix;
							v_fix += dv_fix;
						}
					}
				}
			}
			else if (hasTexture && !isPow2 && isAlphaTest && !isTransparentMaterial && !fogActive && isFlatColor)
			{
				s32 u_fix = (s32)(u_0 * 65536.0f);
				s32 v_fix = (s32)(v_0 * 65536.0f);
				s32 u1_fix = (s32)(u_1 * 65536.0f);
				s32 v1_fix = (s32)(v_1 * 65536.0f);

				s32 du_fix = (k == 16) ? ((u1_fix - u_fix) >> 4) : ((k == 8) ? ((u1_fix - u_fix) >> 3) : ((u1_fix - u_fix) / k));
				s32 dv_fix = (k == 16) ? ((v1_fix - v_fix) >> 4) : ((k == 8) ? ((v1_fix - v_fix) >> 3) : ((v1_fix - v_fix) / k));

				if (flatVertCol == 0xFFFFFFFF)
				{
					for (s32 i = 0; i < k; ++i)
					{
						if (curZ_fixed < zPtr[i])
						{
							u32 u_frac = (u32)u_fix & 0xFFFF;
							u32 v_frac = (u32)v_fix & 0xFFFF;
							u32 tx = (u_frac * texWidth) >> 16;
							u32 ty = (v_frac * texHeight) >> 16;
							u32 texColor = srcData[ty * srcPitch + tx];

							if ((texColor >> 24) >= 128)
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								dPtr[i] = texColor;
							}
						}
						curZ_fixed += dz_dx_fixed;
						u_fix += du_fix;
						v_fix += dv_fix;
					}
				}
				else if (isMonoLight)
				{
					for (s32 i = 0; i < k; ++i)
					{
						if (curZ_fixed < zPtr[i])
						{
							u32 u_frac = (u32)u_fix & 0xFFFF;
							u32 v_frac = (u32)v_fix & 0xFFFF;
							u32 tx = (u_frac * texWidth) >> 16;
							u32 ty = (v_frac * texHeight) >> 16;
							u32 texColor = srcData[ty * srcPitch + tx];

							if ((texColor >> 24) >= 128)
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								u32 rb = (((texColor & 0x00FF00FF) * monoL) >> 8) & 0x00FF00FF;
								u32 g  = (((texColor & 0x0000FF00) * monoL) >> 8) & 0x0000FF00;
								dPtr[i] = 0xFF000000 | rb | g;
							}
						}
						curZ_fixed += dz_dx_fixed;
						u_fix += du_fix;
						v_fix += dv_fix;
					}
				}
				else
				{
					for (s32 i = 0; i < k; ++i)
					{
						if (curZ_fixed < zPtr[i])
						{
							u32 u_frac = (u32)u_fix & 0xFFFF;
							u32 v_frac = (u32)v_fix & 0xFFFF;
							u32 tx = (u_frac * texWidth) >> 16;
							u32 ty = (v_frac * texHeight) >> 16;
							u32 texColor = srcData[ty * srcPitch + tx];

							if ((texColor >> 24) >= 128)
							{
								if (zWriteEnable) zPtr[i] = curZ_fixed;
								dPtr[i] = modulateColorFast(texColor, flatVertCol);
							}
						}
						curZ_fixed += dz_dx_fixed;
						u_fix += du_fix;
						v_fix += dv_fix;
					}
				}
			}
			else if (!hasTexture && !fogActive && isFlatColor)
			{
				for (s32 i = 0; i < k; ++i)
				{
					if (curZ_fixed < zPtr[i])
					{
						if (zWriteEnable)
							zPtr[i] = curZ_fixed;

						if (isTransparentMaterial || ((flatVertCol >> 24) & 0xFF) < 255)
							dPtr[i] = blendPixel32(flatVertCol, dPtr[i]);
						else
							dPtr[i] = flatVertCol;
					}
					curZ_fixed += dz_dx_fixed;
				}
			}
			else
			{
				s32 u_fix = 0, v_fix = 0, du_fix = 0, dv_fix = 0;
				if (hasTexture)
				{
					if (isPow2)
					{
						u_fix = (s32)(u_0 * fTexWidth * 65536.0f);
						v_fix = (s32)(v_0 * fTexHeight * 65536.0f);
						s32 u1_fix = (s32)(u_1 * fTexWidth * 65536.0f);
						s32 v1_fix = (s32)(v_1 * fTexHeight * 65536.0f);
						du_fix = (k == 16) ? ((u1_fix - u_fix) >> 4) : ((k == 8) ? ((u1_fix - u_fix) >> 3) : ((u1_fix - u_fix) / k));
						dv_fix = (k == 16) ? ((v1_fix - v_fix) >> 4) : ((k == 8) ? ((v1_fix - v_fix) >> 3) : ((v1_fix - v_fix) / k));
					}
					else
					{
						u_fix = (s32)(u_0 * 65536.0f);
						v_fix = (s32)(v_0 * 65536.0f);
						s32 u1_fix = (s32)(u_1 * 65536.0f);
						s32 v1_fix = (s32)(v_1 * 65536.0f);
						du_fix = (k == 16) ? ((u1_fix - u_fix) >> 4) : ((k == 8) ? ((u1_fix - u_fix) >> 3) : ((u1_fix - u_fix) / k));
						dv_fix = (k == 16) ? ((v1_fix - v_fix) >> 4) : ((k == 8) ? ((v1_fix - v_fix) >> 3) : ((v1_fix - v_fix) / k));
					}
				}

				s32 cr_fix = 0, cg_fix = 0, cb_fix = 0, ca_fix = 0;
				s32 dcr_fix = 0, dcg_fix = 0, dcb_fix = 0, dca_fix = 0;
				if (!isFlatColor)
				{
					cr_fix = (s32)(cr * 256.0f);
					cg_fix = (s32)(cg * 256.0f);
					cb_fix = (s32)(cb * 256.0f);
					ca_fix = (s32)(ca * 256.0f);
					f32 cr_next = cr + fk * dr_dx;
					f32 cg_next = cg + fk * dg_dx;
					f32 cb_next = cb + fk * db_dx;
					f32 ca_next = ca + fk * da_dx;
					dcr_fix = (k == 16) ? (((s32)(cr_next * 256.0f) - cr_fix) >> 4) : ((k == 8) ? (((s32)(cr_next * 256.0f) - cr_fix) >> 3) : (((s32)(cr_next * 256.0f) - cr_fix) / k));
					dcg_fix = (k == 16) ? (((s32)(cg_next * 256.0f) - cg_fix) >> 4) : ((k == 8) ? (((s32)(cg_next * 256.0f) - cg_fix) >> 3) : (((s32)(cg_next * 256.0f) - cg_fix) / k));
					dcb_fix = (k == 16) ? (((s32)(cb_next * 256.0f) - cb_fix) >> 4) : ((k == 8) ? (((s32)(cb_next * 256.0f) - cb_fix) >> 3) : (((s32)(cb_next * 256.0f) - cb_fix) / k));
					dca_fix = (k == 16) ? (((s32)(ca_next * 256.0f) - ca_fix) >> 4) : ((k == 8) ? (((s32)(ca_next * 256.0f) - ca_fix) >> 3) : (((s32)(ca_next * 256.0f) - ca_fix) / k));
					cr = cr_next;
					cg = cg_next;
					cb = cb_next;
					ca = ca_next;
				}

				s32 fog0_fix = 0, dfog_fix = 0;
				if (fogActive)
				{
					f32 fog0 = (FogEnd - W0) * invFogRange;
					if (fog0 < 0.0f) fog0 = 0.0f; else if (fog0 > 1.0f) fog0 = 1.0f;
					fog0_fix = (s32)(fog0 * 256.0f);

					f32 fog1 = (FogEnd - W1) * invFogRange;
					if (fog1 < 0.0f) fog1 = 0.0f; else if (fog1 > 1.0f) fog1 = 1.0f;
					s32 fog1_fix = (s32)(fog1 * 256.0f);
					dfog_fix = (k == 16) ? ((fog1_fix - fog0_fix) >> 4) : ((k == 8) ? ((fog1_fix - fog0_fix) >> 3) : ((fog1_fix - fog0_fix) / k));
				}

				for (s32 i = 0; i < k; ++i)
				{
					if (curZ_fixed < zPtr[i])
					{
						u32 vertCol = flatVertCol;
						if (!isFlatColor)
						{
							u32 r = (cr_fix < 0) ? 0 : ((cr_fix > 65280) ? 255 : (u32)(cr_fix >> 8));
							u32 g = (cg_fix < 0) ? 0 : ((cg_fix > 65280) ? 255 : (u32)(cg_fix >> 8));
							u32 b = (cb_fix < 0) ? 0 : ((cb_fix > 65280) ? 255 : (u32)(cb_fix >> 8));
							u32 a = (ca_fix < 0) ? 0 : ((ca_fix > 65280) ? 255 : (u32)(ca_fix >> 8));
							vertCol = (a << 24) | (r << 16) | (g << 8) | b;
						}

						u32 pixel = vertCol;
						if (hasTexture)
						{
							u32 tx, ty;
							if (isPow2)
							{
								tx = (u_fix >> 16) & maskX;
								ty = (v_fix >> 16) & maskY;
							}
							else
							{
								u32 u_frac = (u32)u_fix & 0xFFFF;
								u32 v_frac = (u32)v_fix & 0xFFFF;
								tx = (u_frac * texWidth) >> 16;
								ty = (v_frac * texHeight) >> 16;
							}

							u32 texColor = srcData[ty * srcPitch + tx];
							if (isAlphaTest && ((texColor >> 24) & 0xFF) < 128)
							{
								goto next_generic_pixel;
							}

							if (vertCol == 0xFFFFFFFF)
								pixel = texColor;
							else
								pixel = modulateColorFast(texColor, vertCol);
						}

						if (zWriteEnable)
							zPtr[i] = curZ_fixed;

						if (fogActive)
						{
							u32 fFact = (fog0_fix < 0) ? 0 : ((fog0_fix > 256) ? 256 : (u32)fog0_fix);
							u32 invF = 256 - fFact;
							u32 pr = (pixel >> 16) & 0xFF;
							u32 pg = (pixel >> 8)  & 0xFF;
							u32 pb = pixel         & 0xFF;

							u32 fr = (pr * fFact + fogColorRed   * invF + 128) >> 8;
							u32 fg = (pg * fFact + fogColorGreen * invF + 128) >> 8;
							u32 fb = (pb * fFact + fogColorBlue  * invF + 128) >> 8;

							pixel = (pixel & 0xFF000000) | (fr << 16) | (fg << 8) | fb;
						}

						if (isTransparentMaterial || ((pixel >> 24) & 0xFF) < 255)
							dPtr[i] = blendPixel32(pixel, dPtr[i]);
						else
							dPtr[i] = pixel;
					}

next_generic_pixel:
					curZ_fixed += dz_dx_fixed;
					if (hasTexture)
					{
						u_fix += du_fix;
						v_fix += dv_fix;
					}
					if (!isFlatColor)
					{
						cr_fix += dcr_fix;
						cg_fix += dcg_fix;
						cb_fix += dcb_fix;
						ca_fix += dca_fix;
					}
					if (fogActive)
					{
						fog0_fix += dfog_fix;
					}
				}
			}

			invW = invW_next;
			U = U_next;
			V = V_next;
			W0 = W1;
			u_0 = u_1;
			v_0 = v_1;
			x += k;
			count -= k;
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

	const core::matrix4& proj = TransformationMatrix[ETS_PROJECTION];
	f32 wNear = 0.1f;
	if (proj[11] != 0.0f && fabsf(proj[10]) > 0.0001f)
	{
		f32 zNear = fabsf(proj[14] / proj[10]);
		if (zNear > 0.01f && zNear < 5.0f)
			wNear = zNear;
	}

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

		f32 px = pos[0], py = pos[1], pz = pos[2];
		cv.x = px * m[0] + py * m[4] + pz * m[8]  + m[12];
		cv.y = px * m[1] + py * m[5] + pz * m[9]  + m[13];
		cv.z = px * m[2] + py * m[6] + pz * m[10] + m[14];
		cv.w = px * m[3] + py * m[7] + pz * m[11] + m[15];
		ClipVerts[i] = cv;

		if (cv.w >= wNear)
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

			bool in0 = (c0.w >= wNear);
			bool in1 = (c1.w >= wNear);
			bool in2 = (c2.w >= wNear);

			int inCount = (in0 ? 1 : 0) + (in1 ? 1 : 0) + (in2 ? 1 : 0);

			if (inCount == 3)
			{
				const STransformedVertex& tv0 = TransformedVerts[idx0];
				const STransformedVertex& tv1 = TransformedVerts[idx1];
				const STransformedVertex& tv2 = TransformedVerts[idx2];

				f32 area = (tv1.x - tv0.x) * (tv2.y - tv0.y) - (tv1.y - tv0.y) * (tv2.x - tv0.x);
				if (Material.BackfaceCulling && area <= 0.0f)
					continue;
				if (Material.FrontfaceCulling && area >= 0.0f)
					continue;
				if (fabsf(area) < 0.0001f)
					continue;

				f32 minX = std::min(tv0.x, std::min(tv1.x, tv2.x));
				f32 maxX = std::max(tv0.x, std::max(tv1.x, tv2.x));
				f32 minY = std::min(tv0.y, std::min(tv1.y, tv2.y));
				f32 maxY = std::max(tv0.y, std::max(tv1.y, tv2.y));

				if (maxX < (f32)ViewPort.UpperLeftCorner.X || minX >= (f32)ViewPort.LowerRightCorner.X ||
				    maxY < (f32)ViewPort.UpperLeftCorner.Y || minY >= (f32)ViewPort.LowerRightCorner.Y)
					continue;

				renderTriangle3D(tv0, tv1, tv2,
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

				SClipVertex i0 = clipEdge(*vA, *vB, wNear);
				SClipVertex i1 = clipEdge(*vA, *vC, wNear);

				STransformedVertex tA = projectClipVertex(*vA, midX, midY, halfW, halfH);
				STransformedVertex t0 = projectClipVertex(i0, midX, midY, halfW, halfH);
				STransformedVertex t1 = projectClipVertex(i1, midX, midY, halfW, halfH);

				f32 area = (t0.x - tA.x) * (t1.y - tA.y) - (t0.y - tA.y) * (t1.x - tA.x);
				if (Material.BackfaceCulling && area <= 0.0f)
					continue;
				if (Material.FrontfaceCulling && area >= 0.0f)
					continue;
				if (fabsf(area) < 0.0001f)
					continue;

				f32 minX = std::min(tA.x, std::min(t0.x, t1.x));
				f32 maxX = std::max(tA.x, std::max(t0.x, t1.x));
				f32 minY = std::min(tA.y, std::min(t0.y, t1.y));
				f32 maxY = std::max(tA.y, std::max(t0.y, t1.y));

				if (maxX < (f32)ViewPort.UpperLeftCorner.X || minX >= (f32)ViewPort.LowerRightCorner.X ||
				    maxY < (f32)ViewPort.UpperLeftCorner.Y || minY >= (f32)ViewPort.LowerRightCorner.Y)
					continue;

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

				SClipVertex i0 = clipEdge(*vB, *vC, wNear);
				SClipVertex i1 = clipEdge(*vA, *vC, wNear);

				STransformedVertex tA = projectClipVertex(*vA, midX, midY, halfW, halfH);
				STransformedVertex tB = projectClipVertex(*vB, midX, midY, halfW, halfH);
				STransformedVertex t0 = projectClipVertex(i0, midX, midY, halfW, halfH);
				STransformedVertex t1 = projectClipVertex(i1, midX, midY, halfW, halfH);

				f32 area1 = (tB.x - tA.x) * (t0.y - tA.y) - (tB.y - tA.y) * (t0.x - tA.x);
				bool draw1 = true;
				if (Material.BackfaceCulling && area1 <= 0.0f) draw1 = false;
				else if (Material.FrontfaceCulling && area1 >= 0.0f) draw1 = false;
				else if (fabsf(area1) < 0.0001f) draw1 = false;
				else
				{
					f32 minX = std::min(tA.x, std::min(tB.x, t0.x));
					f32 maxX = std::max(tA.x, std::max(tB.x, t0.x));
					f32 minY = std::min(tA.y, std::min(tB.y, t0.y));
					f32 maxY = std::max(tA.y, std::max(tB.y, t0.y));
					if (maxX < (f32)ViewPort.UpperLeftCorner.X || minX >= (f32)ViewPort.LowerRightCorner.X ||
					    maxY < (f32)ViewPort.UpperLeftCorner.Y || minY >= (f32)ViewPort.LowerRightCorner.Y)
						draw1 = false;
				}
				if (draw1)
				{
					renderTriangle3D(tA, tB, t0,
					                 dstData, dstPitch, srcData, texWidth, texHeight, srcPitch);
				}

				f32 area2 = (t0.x - tA.x) * (t1.y - tA.y) - (t0.y - tA.y) * (t1.x - tA.x);
				bool draw2 = true;
				if (Material.BackfaceCulling && area2 <= 0.0f) draw2 = false;
				else if (Material.FrontfaceCulling && area2 >= 0.0f) draw2 = false;
				else if (fabsf(area2) < 0.0001f) draw2 = false;
				else
				{
					f32 minX = std::min(tA.x, std::min(t0.x, t1.x));
					f32 maxX = std::max(tA.x, std::max(t0.x, t1.x));
					f32 minY = std::min(tA.y, std::min(t0.y, t1.y));
					f32 maxY = std::max(tA.y, std::max(t0.y, t1.y));
					if (maxX < (f32)ViewPort.UpperLeftCorner.X || minX >= (f32)ViewPort.LowerRightCorner.X ||
					    maxY < (f32)ViewPort.UpperLeftCorner.Y || minY >= (f32)ViewPort.LowerRightCorner.Y)
						draw2 = false;
				}
				if (draw2)
				{
					renderTriangle3D(tA, t0, t1,
					                 dstData, dstPitch, srcData, texWidth, texHeight, srcPitch);
				}
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

			bool in0 = (c0.w >= wNear);
			bool in1 = (c1.w >= wNear);

			if (in0 && in1)
			{
				draw3DLineTransformed(TransformedVerts[idx0], TransformedVerts[idx1], c0.color);
			}
			else if (in0 && !in1)
			{
				SClipVertex i1 = clipEdge(c0, c1, wNear);
				STransformedVertex t1 = projectClipVertex(i1, midX, midY, halfW, halfH);
				draw3DLineTransformed(TransformedVerts[idx0], t1, c0.color);
			}
			else if (!in0 && in1)
			{
				SClipVertex i0 = clipEdge(c1, c0, wNear);
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
