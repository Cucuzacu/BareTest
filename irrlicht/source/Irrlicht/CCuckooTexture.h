#ifndef __C_CUCKOO_TEXTURE_H_INCLUDED__
#define __C_CUCKOO_TEXTURE_H_INCLUDED__

#include "ITexture.h"
#include "CImage.h"

namespace irr
{
namespace video
{

class CCuckooTexture : public ITexture
{
public:

	//! constructor
	CCuckooTexture(IImage* surface, const io::path& name,
			bool renderTarget=false, void* mipmapData=0);

	//! destructor
	virtual ~CCuckooTexture();

	//! lock function
	virtual void* lock(bool readOnly = false, u32 mipmapLevel=0);

	//! unlock function
	virtual void unlock();

	//! returns original size of texture
	virtual const core::dimension2d<u32>& getOriginalSize() const;

	//! returns size of texture
	virtual const core::dimension2d<u32>& getSize() const;

	//! returns unoptimized surface
	virtual CImage* getImage();

	//! returns texture surface
	virtual CImage* getTexture();

	//! returns driver type of texture
	virtual E_DRIVER_TYPE getDriverType() const;

	//! returns color format of texture
	virtual ECOLOR_FORMAT getColorFormat() const;

	//! returns pitch of texture (in bytes)
	virtual u32 getPitch() const;

	//! regenerates the mip map levels of the texture
	virtual void regenerateMipMapLevels(void* mipmapData=0);

	//! is it a render target?
	virtual bool isRenderTarget() const;

private:
	CImage* Image;
	core::dimension2d<u32> OrigSize;
	bool IsRenderTarget;
};

} // end namespace video
} // end namespace irr

#endif // __C_CUCKOO_TEXTURE_H_INCLUDED__
