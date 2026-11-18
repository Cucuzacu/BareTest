#include "IrrCompileConfig.h"
#ifdef _IRR_COMPILE_WITH_CUCKOOVIDEO_

#include "CCuckooTexture.h"
#include "os.h"

namespace irr
{
namespace video
{

//! constructor
CCuckooTexture::CCuckooTexture(IImage* image, const io::path& name,
		bool renderTarget, void* mipmapData)
: ITexture(name), Image(0), IsRenderTarget(renderTarget)
{
	#ifdef _DEBUG
	setDebugName("CCuckooTexture");
	#endif

	if (image)
	{
		OrigSize = image->getDimension();

		Image = new CImage(ECF_A8R8G8B8, OrigSize);
		image->copyTo(Image);
	}
}

//! destructor
CCuckooTexture::~CCuckooTexture()
{
	if (Image)
		Image->drop();
}

//! lock function
void* CCuckooTexture::lock(bool readOnly, u32 mipmapLevel)
{
	if (Image)
		return Image->lock();
	return 0;
}

//! unlock function
void CCuckooTexture::unlock()
{
	if (Image)
		Image->unlock();
}

//! returns original size of texture
const core::dimension2d<u32>& CCuckooTexture::getOriginalSize() const
{
	return OrigSize;
}

//! returns size of texture
const core::dimension2d<u32>& CCuckooTexture::getSize() const
{
	if (Image)
		return Image->getDimension();
	return OrigSize;
}

//! returns unoptimized surface
CImage* CCuckooTexture::getImage()
{
	return Image;
}

//! returns texture surface
CImage* CCuckooTexture::getTexture()
{
	return Image;
}

//! returns driver type of texture
E_DRIVER_TYPE CCuckooTexture::getDriverType() const
{
	return EDT_CUCKOOVIDEO;
}

//! returns color format of texture
ECOLOR_FORMAT CCuckooTexture::getColorFormat() const
{
	return ECF_A8R8G8B8;
}

//! returns pitch of texture (in bytes)
u32 CCuckooTexture::getPitch() const
{
	if (Image)
		return Image->getPitch();
	return OrigSize.Width * 4;
}

//! regenerates the mip map levels of the texture
void CCuckooTexture::regenerateMipMapLevels(void* mipmapData)
{
}

bool CCuckooTexture::isRenderTarget() const
{
	return IsRenderTarget;
}

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_CUCKOOVIDEO_
