// Copyright (C) 2002-2010 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h
// This device code is based on the original SDL device implementation
// contributed by Shane Parker (sirshane).

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_

#include "CIrrDeviceSDL.h"
#include "IEventReceiver.h"
#include "irrList.h"
#include "os.h"
#include "CTimer.h"
#include "irrString.h"
#include "Keycodes.h"
#include "COSOperator.h"
#include <stdio.h>
#include <stdlib.h>
#include "SIrrCreationParameters.h"
#include <SDL_syswm.h>
#include <SDL_video.h>

#if !SDL_VERSION_ATLEAST(2,0,0)
#define SDLK_PRINTSCREEN SDLK_PRINT
#define SDLK_NUMLOCKCLEAR SDLK_NUMLOCK
#define SDLK_SCROLLLOCK SDLK_SCROLLOCK
#define SDLK_LGUI SDLK_LSUPER
#define SDLK_RGUI SDLK_RSUPER
#define SDLK_KP_0 SDLK_KP0
#define SDLK_KP_1 SDLK_KP1
#define SDLK_KP_2 SDLK_KP2
#define SDLK_KP_3 SDLK_KP3
#define SDLK_KP_4 SDLK_KP4
#define SDLK_KP_5 SDLK_KP5
#define SDLK_KP_6 SDLK_KP6
#define SDLK_KP_7 SDLK_KP7
#define SDLK_KP_8 SDLK_KP8
#define SDLK_KP_9 SDLK_KP9
#endif


#ifdef _MSC_VER
#pragma comment(lib, "SDL.lib")
#endif // _MSC_VER

namespace irr
{
	namespace video
	{

		#ifdef _IRR_COMPILE_WITH_DIRECT3D_8_
		IVideoDriver* createDirectX8Driver(const core::dimension2d<u32>& screenSize, HWND window,
			u32 bits, bool fullscreen, bool stencilbuffer, io::IFileSystem* io,
			bool pureSoftware, bool highPrecisionFPU, bool vsync, u8 antiAlias);
		#endif

		#ifdef _IRR_COMPILE_WITH_DIRECT3D_9_
		IVideoDriver* createDirectX9Driver(const core::dimension2d<u32>& screenSize, HWND window,
			u32 bits, bool fullscreen, bool stencilbuffer, io::IFileSystem* io,
			bool pureSoftware, bool highPrecisionFPU, bool vsync, u8 antiAlias);
		#endif

		#ifdef _IRR_COMPILE_WITH_OPENGL_
		IVideoDriver* createOpenGLDriver(const SIrrlichtCreationParameters& params,
				io::IFileSystem* io, CIrrDeviceSDL* device);
		#endif
	} // end namespace video

} // end namespace irr


namespace irr
{

//! constructor
CIrrDeviceSDL::CIrrDeviceSDL(const SIrrlichtCreationParameters& param)
	: CIrrDeviceStub(param),
#if SDL_VERSION_ATLEAST(2,0,0)
	Window(0), GLContext(0),
#endif
	Screen(0), SDL_Flags(0),
	MouseX(0), MouseY(0), MouseButtonStates(0),
	Width(param.WindowSize.Width), Height(param.WindowSize.Height),
	Resizable(false), WindowHasFocus(true), WindowMinimized(false)
{
	#ifdef _DEBUG
	setDebugName("CIrrDeviceSDL");
	#endif

	// Initialize SDL... Timer for sleep, video for the obvious, and
	// noparachute prevents SDL from catching fatal errors.
	if (SDL_Init( SDL_INIT_TIMER|SDL_INIT_VIDEO|
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
				SDL_INIT_JOYSTICK|
#endif
				SDL_INIT_NOPARACHUTE ) < 0)
	{
		os::Printer::log( "Unable to initialize SDL!", SDL_GetError());
		Close = true;
	}

	SDL_version ver;
#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_GetVersion(&ver);
#else
	SDL_VERSION(&ver);
#endif
	core::stringc sdlversion = "SDL Version ";
	sdlversion += (int)ver.major;
	sdlversion += ".";
	sdlversion += (int)ver.minor;
	sdlversion += ".";
	sdlversion += (int)ver.patch;

	Operator = new COSOperator(sdlversion.c_str());
	os::Printer::log(sdlversion.c_str(), ELL_INFORMATION);

	// create keymap
	createKeyMap();

	// create window
	if (CreationParams.DriverType != video::EDT_NULL)
	{
		// create the window, only if we do not use the null device
		createWindow();
	}

	// create cursor control
	CursorControl = new CCursorControl(this);

	// create driver
	createDriver();

	if (VideoDriver)
		createGUIAndScene();
}


////! destructor
CIrrDeviceSDL::~CIrrDeviceSDL()
{
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
	const u32 numJoysticks = Joysticks.size();
	for (u32 i=0; i<numJoysticks; ++i)
		SDL_JoystickClose(Joysticks[i]);
#endif
#if SDL_VERSION_ATLEAST(2,0,0)
	if (GLContext)
		SDL_GL_DeleteContext(GLContext);
	if (Window)
		SDL_DestroyWindow(Window);
#endif
	SDL_Quit();
}


bool CIrrDeviceSDL::createWindow()
{
	if ( Close )
		return false;

#if SDL_VERSION_ATLEAST(2,0,0)
	Uint32 flags = SDL_WINDOW_SHOWN;
	if (CreationParams.Fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN;
	if (CreationParams.DriverType == video::EDT_OPENGL)
	{
		flags |= SDL_WINDOW_OPENGL;
		if (CreationParams.Bits == 16)
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, CreationParams.WithAlphaChannel ? 1 : 0 );
		}
		else
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, CreationParams.WithAlphaChannel ? 8 : 0 );
		}
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, CreationParams.ZBufferBits );
		if (CreationParams.Doublebuffer)
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		if (CreationParams.Stereobuffer)
			SDL_GL_SetAttribute( SDL_GL_STEREO, 1 );
		if (CreationParams.AntiAlias > 1)
		{
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, CreationParams.AntiAlias );
		}
	}

	if (Resizable)
		flags |= SDL_WINDOW_RESIZABLE;

	if (CreationParams.WindowId)
	{
		Window = SDL_CreateWindowFrom(CreationParams.WindowId);
	}
	else
	{
		Window = SDL_CreateWindow("Irrlicht Engine",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			Width, Height, flags);
	}

	if (!Window)
	{
		os::Printer::log("Could not create SDL window!", SDL_GetError(), ELL_ERROR);
		return false;
	}

	if (CreationParams.DriverType == video::EDT_OPENGL)
	{
		GLContext = SDL_GL_CreateContext(Window);
		if (!GLContext)
		{
			os::Printer::log("Could not create SDL OpenGL Context!", SDL_GetError(), ELL_ERROR);
			return false;
		}
		SDL_GL_SetSwapInterval(CreationParams.Vsync ? 1 : 0);
	}
	else
	{
		Screen = SDL_GetWindowSurface(Window);
	}

	SDL_VERSION(&Info.version);
	SDL_GetWindowWMInfo(Window, &Info);

	return true;
#else
	SDL_Flags = SDL_ANYFORMAT;
	if (CreationParams.Fullscreen)
		SDL_Flags |= SDL_FULLSCREEN;
	if (CreationParams.DriverType == video::EDT_OPENGL)
	{
		SDL_Flags |= SDL_OPENGL;
		if (CreationParams.Bits == 16)
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 4 );
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, CreationParams.WithAlphaChannel ? 1 : 0 );
		}
		else
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, CreationParams.WithAlphaChannel ? 8 : 0 );
		}
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, CreationParams.ZBufferBits );
		if (CreationParams.Doublebuffer)
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		if (CreationParams.Stereobuffer)
			SDL_GL_SetAttribute( SDL_GL_STEREO, 1 );
	}
	if (Resizable)
		SDL_Flags |= SDL_RESIZABLE;

	Screen = SDL_SetVideoMode(Width, Height, CreationParams.Bits, SDL_Flags);
	if (!Screen)
	{
		os::Printer::log("Could not set SDL video mode!", SDL_GetError(), ELL_ERROR);
		return false;
	}

	SDL_VERSION(&Info.version);
	SDL_GetWMInfo(&Info);

	return true;
#endif
}


//! create the driver
void CIrrDeviceSDL::createDriver()
{
	switch(CreationParams.DriverType)
	{
	case video::EDT_DIRECT3D8:
		#ifdef _IRR_COMPILE_WITH_DIRECT3D_8_
		#ifdef _IRR_WINDOWS_API_
		#if SDL_VERSION_ATLEAST(2,0,0)
		HWND hwnd8 = Info.info.win.window;
#else
		HWND hwnd8 = Info.window;
#endif
		VideoDriver = video::createDirectX8Driver(CreationParams.WindowSize, hwnd8,
			CreationParams.Bits, CreationParams.Fullscreen, CreationParams.Stencilbuffer,
			FileSystem, false, CreationParams.HighPrecisionFPU, CreationParams.Vsync,
			CreationParams.AntiAlias);
		#endif
		if (!VideoDriver)
		{
			os::Printer::log("Could not create DIRECT3D8 Driver.", ELL_ERROR);
		}
		#else
		os::Printer::log("DIRECT3D8 Driver was not compiled into this dll. Try another one.", ELL_ERROR);
		#endif // _IRR_COMPILE_WITH_DIRECT3D_8_

		break;

	case video::EDT_DIRECT3D9:
		#ifdef _IRR_COMPILE_WITH_DIRECT3D_9_
		#ifdef _IRR_WINDOWS_API_
		#if SDL_VERSION_ATLEAST(2,0,0)
		HWND hwnd9 = Info.info.win.window;
#else
		HWND hwnd9 = Info.window;
#endif
		VideoDriver = video::createDirectX9Driver(CreationParams.WindowSize, hwnd9,
			CreationParams.Bits, CreationParams.Fullscreen, CreationParams.Stencilbuffer,
			FileSystem, false, CreationParams.HighPrecisionFPU, CreationParams.Vsync,
			CreationParams.AntiAlias);
		#endif
		if (!VideoDriver)
		{
			os::Printer::log("Could not create DIRECT3D9 Driver.", ELL_ERROR);
		}
		#else
		os::Printer::log("DIRECT3D9 Driver was not compiled into this dll. Try another one.", ELL_ERROR);
		#endif // _IRR_COMPILE_WITH_DIRECT3D_9_

		break;

	case video::EDT_SOFTWARE:
		#ifdef _IRR_COMPILE_WITH_SOFTWARE_
		VideoDriver = video::createSoftwareDriver(CreationParams.WindowSize, CreationParams.Fullscreen, FileSystem, this);
		#else
		os::Printer::log("No Software driver support compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_BURNINGSVIDEO:
		#ifdef _IRR_COMPILE_WITH_BURNINGSVIDEO_
		VideoDriver = video::createSoftwareDriver2(CreationParams.WindowSize, CreationParams.Fullscreen, FileSystem, this);
		#else
		os::Printer::log("Burning's video driver was not compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_CUCKOOVIDEO:
		#ifdef _IRR_COMPILE_WITH_CUCKOOVIDEO_
		VideoDriver = video::createCuckooDriver(CreationParams.WindowSize, CreationParams.Fullscreen, FileSystem, this);
		#else
		os::Printer::log("CuckooVideo driver was not compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_OPENGL:
		#ifdef _IRR_COMPILE_WITH_OPENGL_
		VideoDriver = video::createOpenGLDriver(CreationParams, FileSystem, this);
		#else
		os::Printer::log("No OpenGL support compiled in.", ELL_ERROR);
		#endif
		break;

	case video::EDT_NULL:
		VideoDriver = video::createNullDriver(FileSystem, CreationParams.WindowSize);
		break;

	default:
		os::Printer::log("Unable to create video driver of unknown type.", ELL_ERROR);
		break;
	}
}


//! runs the device. Returns false if device wants to be deleted
bool CIrrDeviceSDL::run()
{
	os::Timer::tick();

	SEvent irrevent;
	SDL_Event SDL_event;

	while ( !Close && SDL_PollEvent( &SDL_event ) )
	{
		switch ( SDL_event.type )
		{
		case SDL_MOUSEMOTION:
			irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
			irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;
			MouseX = irrevent.MouseInput.X = SDL_event.motion.x;
			MouseY = irrevent.MouseInput.Y = SDL_event.motion.y;
			irrevent.MouseInput.ButtonStates = MouseButtonStates;

			postEventFromUser(irrevent);
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:

			irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
			irrevent.MouseInput.X = SDL_event.button.x;
			irrevent.MouseInput.Y = SDL_event.button.y;

			irrevent.MouseInput.Event = irr::EMIE_MOUSE_MOVED;

			switch(SDL_event.button.button)
			{
			case SDL_BUTTON_LEFT:
				if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
				{
					irrevent.MouseInput.Event = irr::EMIE_LMOUSE_PRESSED_DOWN;
					MouseButtonStates |= irr::EMBSM_LEFT;
				}
				else
				{
					irrevent.MouseInput.Event = irr::EMIE_LMOUSE_LEFT_UP;
					MouseButtonStates &= ~irr::EMBSM_LEFT;
				}
				break;

			case SDL_BUTTON_RIGHT:
				if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
				{
					irrevent.MouseInput.Event = irr::EMIE_RMOUSE_PRESSED_DOWN;
					MouseButtonStates |= irr::EMBSM_RIGHT;
				}
				else
				{
					irrevent.MouseInput.Event = irr::EMIE_RMOUSE_LEFT_UP;
					MouseButtonStates &= ~irr::EMBSM_RIGHT;
				}
				break;

			case SDL_BUTTON_MIDDLE:
				if (SDL_event.type == SDL_MOUSEBUTTONDOWN)
				{
					irrevent.MouseInput.Event = irr::EMIE_MMOUSE_PRESSED_DOWN;
					MouseButtonStates |= irr::EMBSM_MIDDLE;
				}
				else
				{
					irrevent.MouseInput.Event = irr::EMIE_MMOUSE_LEFT_UP;
					MouseButtonStates &= ~irr::EMBSM_MIDDLE;
				}
				break;
			}

			irrevent.MouseInput.ButtonStates = MouseButtonStates;

			if (irrevent.MouseInput.Event != irr::EMIE_MOUSE_MOVED)
			{
				postEventFromUser(irrevent);

				if ( irrevent.MouseInput.Event >= EMIE_LMOUSE_PRESSED_DOWN && irrevent.MouseInput.Event <= EMIE_MMOUSE_PRESSED_DOWN )
				{
					u32 clicks = checkSuccessiveClicks(irrevent.MouseInput.X, irrevent.MouseInput.Y, irrevent.MouseInput.Event);
					if ( clicks == 2 )
					{
						irrevent.MouseInput.Event = (EMOUSE_INPUT_EVENT)(EMIE_LMOUSE_DOUBLE_CLICK + irrevent.MouseInput.Event-EMIE_LMOUSE_PRESSED_DOWN);
						postEventFromUser(irrevent);
					}
					else if ( clicks == 3 )
					{
						irrevent.MouseInput.Event = (EMOUSE_INPUT_EVENT)(EMIE_LMOUSE_TRIPLE_CLICK + irrevent.MouseInput.Event-EMIE_LMOUSE_PRESSED_DOWN);
						postEventFromUser(irrevent);
					}
				}
			}
			break;

#if SDL_VERSION_ATLEAST(2,0,0)
		case SDL_MOUSEWHEEL:
			irrevent.EventType = irr::EET_MOUSE_INPUT_EVENT;
			irrevent.MouseInput.Event = irr::EMIE_MOUSE_WHEEL;
			irrevent.MouseInput.X = MouseX;
			irrevent.MouseInput.Y = MouseY;
			irrevent.MouseInput.Wheel = (f32)SDL_event.wheel.y;
			irrevent.MouseInput.ButtonStates = MouseButtonStates;
			postEventFromUser(irrevent);
			break;
#endif
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			{
				SKeyMap mp;
				mp.SDLKey = SDL_event.key.keysym.sym;
				s32 idx = KeyMap.binary_search(mp);

				EKEY_CODE key;
				if (idx == -1)
					key = (EKEY_CODE)0;
				else
					key = (EKEY_CODE)KeyMap[idx].Win32Key;

#ifdef _IRR_WINDOWS_API_
				// handle alt+f4 in Windows
				if ( (SDL_event.key.keysym.mod & KMOD_LALT) && key == KEY_F4)
				{
					Close = true;
					break;
				}
#endif
				irrevent.EventType = irr::EET_KEY_INPUT_EVENT;
				irrevent.KeyInput.Key = key;
				irrevent.KeyInput.PressedDown = (SDL_event.type == SDL_KEYDOWN);
				irrevent.KeyInput.Shift = (SDL_event.key.keysym.mod & KMOD_SHIFT) != 0;
				irrevent.KeyInput.Control = (SDL_event.key.keysym.mod & KMOD_CTRL ) != 0;

				wchar_t ch = 0;
				if (SDL_event.key.keysym.sym >= 32 && SDL_event.key.keysym.sym <= 126)
				{
					ch = (wchar_t)SDL_event.key.keysym.sym;
					if (irrevent.KeyInput.Shift && ch >= L'a' && ch <= L'z')
						ch -= 32;
				}
				irrevent.KeyInput.Char = ch;

				postEventFromUser(irrevent);
			}
			break;

		case SDL_QUIT:
			Close = true;
			break;

#if SDL_VERSION_ATLEAST(2,0,0)
		case SDL_WINDOWEVENT:
			switch (SDL_event.window.event)
			{
			case SDL_WINDOWEVENT_SHOWN:
			case SDL_WINDOWEVENT_RESTORED:
				WindowMinimized = false;
				break;
			case SDL_WINDOWEVENT_HIDDEN:
			case SDL_WINDOWEVENT_MINIMIZED:
				WindowMinimized = true;
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				WindowHasFocus = true;
				break;
			case SDL_WINDOWEVENT_FOCUS_LOST:
				WindowHasFocus = false;
				break;
			case SDL_WINDOWEVENT_RESIZED:
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				Width = (u32)SDL_event.window.data1;
				Height = (u32)SDL_event.window.data2;
				if (Window && CreationParams.DriverType != video::EDT_OPENGL)
					Screen = SDL_GetWindowSurface(Window);
				if (VideoDriver)
					VideoDriver->OnResize(core::dimension2d<u32>(Width, Height));
				break;
			default:
				break;
			}
			break;
#else
		case SDL_ACTIVEEVENT:
			if (SDL_event.active.state & SDL_APPINPUTFOCUS)
				WindowHasFocus = (SDL_event.active.gain != 0);
			if (SDL_event.active.state & SDL_APPACTIVE)
				WindowMinimized = (SDL_event.active.gain == 0);
			break;

		case SDL_VIDEORESIZE:
			Width = (u32)SDL_event.resize.w;
			Height = (u32)SDL_event.resize.h;
			Screen = SDL_SetVideoMode(Width, Height, CreationParams.Bits, SDL_Flags);
			if (VideoDriver)
				VideoDriver->OnResize(core::dimension2d<u32>(Width, Height));
			break;
#endif

		case SDL_USEREVENT:
			irrevent.EventType = irr::EET_USER_EVENT;
			irrevent.UserEvent.UserData1 = *(reinterpret_cast<s32*>(&SDL_event.user.data1));
			irrevent.UserEvent.UserData2 = *(reinterpret_cast<s32*>(&SDL_event.user.data2));

			postEventFromUser(irrevent);
			break;

		default:
			break;
		} // end switch

	} // end while

#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
	// update joystick states manually
	SDL_JoystickUpdate();
	// we'll always send joystick input events...
	SEvent joyevent;
	joyevent.EventType = EET_JOYSTICK_INPUT_EVENT;
	for (u32 i=0; i<Joysticks.size(); ++i)
	{
		SDL_Joystick* joystick = Joysticks[i];
		if (joystick)
		{
			int j;
			// query all buttons
			const int numButtons = core::min_(SDL_JoystickNumButtons(joystick), 32);
			joyevent.JoystickEvent.ButtonStates=0;
			for (j=0; j<numButtons; ++j)
				joyevent.JoystickEvent.ButtonStates |= (SDL_JoystickGetButton(joystick, j)<<j);

			// query all axes, already in correct range
			const int numAxes = core::min_((int)SDL_JoystickNumAxes(joystick), (int)SEvent::SJoystickEvent::NUMBER_OF_AXES);
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_X]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_Y]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_Z]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_R]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_U]=0;
			joyevent.JoystickEvent.Axis[SEvent::SJoystickEvent::AXIS_V]=0;
			for (j=0; j<numAxes; ++j)
				joyevent.JoystickEvent.Axis[j] = SDL_JoystickGetAxis(joystick, j);

			// we can only query one hat, SDL only supports 8 directions
			if (SDL_JoystickNumHats(joystick)>0)
			{
				switch (SDL_JoystickGetHat(joystick, 0))
				{
					case SDL_HAT_UP:
						joyevent.JoystickEvent.POV=0;
						break;
					case SDL_HAT_RIGHTUP:
						joyevent.JoystickEvent.POV=4500;
						break;
					case SDL_HAT_RIGHT:
						joyevent.JoystickEvent.POV=9000;
						break;
					case SDL_HAT_RIGHTDOWN:
						joyevent.JoystickEvent.POV=13500;
						break;
					case SDL_HAT_DOWN:
						joyevent.JoystickEvent.POV=18000;
						break;
					case SDL_HAT_LEFTDOWN:
						joyevent.JoystickEvent.POV=22500;
						break;
					case SDL_HAT_LEFT:
						joyevent.JoystickEvent.POV=27000;
						break;
					case SDL_HAT_LEFTUP:
						joyevent.JoystickEvent.POV=31500;
						break;
					case SDL_HAT_CENTERED:
					default:
						joyevent.JoystickEvent.POV=65535;
						break;
				}
			}
			else
			{
				joyevent.JoystickEvent.POV=65535;
			}

			// we map the number directly
			joyevent.JoystickEvent.Joystick=static_cast<u8>(i);

			// now post the event
			postEventFromUser(joyevent);
			// and close the joystick
		}
	}
#endif
	return !Close;
}

//! Activate any joysticks, and generate events for them.
bool CIrrDeviceSDL::activateJoysticks(core::array<SJoystickInfo> & joystickInfo)
{
#if defined(_IRR_COMPILE_WITH_JOYSTICK_EVENTS_)
	joystickInfo.clear();

	// we can name up to 256 different joysticks
	const int numJoysticks = core::min_(SDL_NumJoysticks(), 256);
	Joysticks.reallocate(numJoysticks);
	joystickInfo.reallocate(numJoysticks);

	int joystick = 0;
	for (; joystick<numJoysticks; ++joystick)
	{
		Joysticks.push_back(SDL_JoystickOpen(joystick));
		SJoystickInfo info;

		info.Joystick = joystick;
		info.Axes = SDL_JoystickNumAxes(Joysticks[joystick]);
		info.Buttons = SDL_JoystickNumButtons(Joysticks[joystick]);
		#if SDL_VERSION_ATLEAST(2,0,0)
		info.Name = SDL_JoystickName(Joysticks[joystick]);
#else
		info.Name = SDL_JoystickName(joystick);
#endif
		info.PovHat = (SDL_JoystickNumHats(Joysticks[joystick]) > 0)
						? SJoystickInfo::POV_HAT_PRESENT : SJoystickInfo::POV_HAT_ABSENT;

		joystickInfo.push_back(info);
	}

	for(joystick = 0; joystick < (int)joystickInfo.size(); ++joystick)
	{
		char logString[256];
		(void)sprintf(logString, "Found joystick %d, %d axes, %d buttons '%s'",
		 joystick, joystickInfo[joystick].Axes,
   joystickInfo[joystick].Buttons, joystickInfo[joystick].Name.c_str());
		os::Printer::log(logString, ELL_INFORMATION);
	}

	return true;

#endif // _IRR_COMPILE_WITH_JOYSTICK_EVENTS_

	return false;
}



//! pause execution temporarily
void CIrrDeviceSDL::yield()
{
	SDL_Delay(0);
}


//! pause execution for a specified time
void CIrrDeviceSDL::sleep(u32 timeMs, bool pauseTimer)
{
	const bool wasStopped = Timer ? Timer->isStopped() : true;
	if (pauseTimer && !wasStopped)
		Timer->stop();

	SDL_Delay(timeMs);

	if (pauseTimer && !wasStopped)
		Timer->start();
}


//! sets the caption of the window
void CIrrDeviceSDL::setWindowCaption(const wchar_t* text)
{
	core::stringc textc = text;
#if SDL_VERSION_ATLEAST(2,0,0)
	if (Window)
		SDL_SetWindowTitle( Window, textc.c_str() );
#else
	SDL_WM_SetCaption( textc.c_str(), NULL );
#endif
}


//! presents a surface in the client area
bool CIrrDeviceSDL::present(video::IImage* surface, void* windowId, core::rect<s32>* srcClip)
{
	SDL_Surface *sdlSurface = SDL_CreateRGBSurfaceFrom(
			surface->lock(), surface->getDimension().Width, surface->getDimension().Height,
			surface->getBitsPerPixel(), surface->getPitch(),
			surface->getRedMask(), surface->getGreenMask(), surface->getBlueMask(), surface->getAlphaMask());
	if (!sdlSurface)
		return false;

#if SDL_VERSION_ATLEAST(2,0,0)
	if (Window)
	{
		Screen = SDL_GetWindowSurface(Window);
		if (Screen)
		{
			if (srcClip)
			{
				SDL_Rect sdlsrcClip;
				sdlsrcClip.x = srcClip->UpperLeftCorner.X;
				sdlsrcClip.y = srcClip->UpperLeftCorner.Y;
				sdlsrcClip.w = srcClip->getWidth();
				sdlsrcClip.h = srcClip->getHeight();
				SDL_BlitSurface(sdlSurface, &sdlsrcClip, Screen, NULL);
			}
			else
			{
				SDL_BlitSurface(sdlSurface, NULL, Screen, NULL);
			}
			SDL_UpdateWindowSurface(Window);
		}
	}
#else
	if (Screen)
	{
		if (srcClip)
			{
				SDL_Rect sdlsrcClip;
				sdlsrcClip.x = srcClip->UpperLeftCorner.X;
				sdlsrcClip.y = srcClip->UpperLeftCorner.Y;
				sdlsrcClip.w = srcClip->getWidth();
				sdlsrcClip.h = srcClip->getHeight();
				SDL_BlitSurface(sdlSurface, &sdlsrcClip, Screen, NULL);
			}
			else
			{
				SDL_BlitSurface(sdlSurface, NULL, Screen, NULL);
			}
		SDL_Flip(Screen);
	}
#endif

	SDL_FreeSurface(sdlSurface);
	surface->unlock();
	return (Screen != 0);
}


//! notifies the device that it should close itself
void CIrrDeviceSDL::closeDevice()
{
	Close = true;
}


//! \return Pointer to a list with all video modes supported
video::IVideoModeList* CIrrDeviceSDL::getVideoModeList()
{
#if SDL_VERSION_ATLEAST(2,0,0)
	if (!VideoModeList.getVideoModeCount())
	{
		int numModes = SDL_GetNumDisplayModes(0);
		for (int i = 0; i < numModes; ++i)
		{
			SDL_DisplayMode mode;
			if (SDL_GetDisplayMode(0, i, &mode) == 0)
			{
				VideoModeList.addMode(core::dimension2d<u32>(mode.w, mode.h), SDL_BITSPERPIXEL(mode.format));
			}
		}
	}
	return &VideoModeList;
#else
	if (!VideoModeList.getVideoModeCount())
	{
		SDL_Rect **modes = SDL_ListModes(NULL, SDL_Flags);
		if (modes && modes != (SDL_Rect **)-1)
		{
			for (size_t i = 0; modes[i]; ++i)
				VideoModeList.addMode(core::dimension2d<u32>(modes[i]->w, modes[i]->h), 16);
		}
		else
		{
			VideoModeList.addMode(core::dimension2d<u32>(Width, Height), 16);
		}
	}
	return &VideoModeList;
#endif
}


//! Sets if the window should be resizable in windowed mode.
void CIrrDeviceSDL::setResizable(bool resize)
{
	if (resize != Resizable)
	{
		Resizable = resize;
#if SDL_VERSION_ATLEAST(2,0,0)
		if (Window)
			SDL_SetWindowResizable(Window, resize ? SDL_TRUE : SDL_FALSE);
#endif
	}
}


//! Minimizes window if possible
void CIrrDeviceSDL::minimizeWindow()
{
#if SDL_VERSION_ATLEAST(2,0,0)
	if (Window)
		SDL_MinimizeWindow(Window);
#else
	SDL_WM_IconifyWindow();
#endif
}


//! Maximize window
void CIrrDeviceSDL::maximizeWindow()
{
#if SDL_VERSION_ATLEAST(2,0,0)
	if (Window)
		SDL_MaximizeWindow(Window);
#endif
}


//! Restore original window size
void CIrrDeviceSDL::restoreWindow()
{
#if SDL_VERSION_ATLEAST(2,0,0)
	if (Window)
		SDL_RestoreWindow(Window);
#endif
}


//! returns if window is active. if not, nothing need to be drawn
bool CIrrDeviceSDL::isWindowActive() const
{
	return (WindowHasFocus && !WindowMinimized);
}


//! returns if window has focus.
bool CIrrDeviceSDL::isWindowFocused() const
{
	return WindowHasFocus;
}


//! returns if window is minimized.
bool CIrrDeviceSDL::isWindowMinimized() const
{
	return WindowMinimized;
}


//! Set the current Gamma Value for the Display
bool CIrrDeviceSDL::setGammaRamp( f32 red, f32 green, f32 blue, f32 brightness, f32 contrast )
{
	return false;
}

//! Get the current Gamma Value for the Display
bool CIrrDeviceSDL::getGammaRamp( f32 &red, f32 &green, f32 &blue, f32 &brightness, f32 &contrast )
{
	return false;
}

//! returns color format of the window.
video::ECOLOR_FORMAT CIrrDeviceSDL::getColorFormat() const
{
	if (Screen)
	{
		if (Screen->format->BitsPerPixel==16)
		{
			if (Screen->format->Amask != 0)
				return video::ECF_A1R5G5B5;
			else
				return video::ECF_R5G6B5;
		}
		else
		{
			if (Screen->format->Amask != 0)
				return video::ECF_A8R8G8B8;
			else
				return video::ECF_R8G8B8;
		}
	}
	else
		return CIrrDeviceStub::getColorFormat();
}


void CIrrDeviceSDL::createKeyMap()
{
	KeyMap.reallocate(120);

	KeyMap.push_back(SKeyMap(SDLK_BACKSPACE, KEY_BACK));
	KeyMap.push_back(SKeyMap(SDLK_TAB, KEY_TAB));
	KeyMap.push_back(SKeyMap(SDLK_CLEAR, KEY_CLEAR));
	KeyMap.push_back(SKeyMap(SDLK_RETURN, KEY_RETURN));

	KeyMap.push_back(SKeyMap(SDLK_PAUSE, KEY_PAUSE));
	KeyMap.push_back(SKeyMap(SDLK_CAPSLOCK, KEY_CAPITAL));

	KeyMap.push_back(SKeyMap(SDLK_ESCAPE, KEY_ESCAPE));

	KeyMap.push_back(SKeyMap(SDLK_SPACE, KEY_SPACE));
	KeyMap.push_back(SKeyMap(SDLK_PAGEUP, KEY_PRIOR));
	KeyMap.push_back(SKeyMap(SDLK_PAGEDOWN, KEY_NEXT));
	KeyMap.push_back(SKeyMap(SDLK_END, KEY_END));
	KeyMap.push_back(SKeyMap(SDLK_HOME, KEY_HOME));
	KeyMap.push_back(SKeyMap(SDLK_LEFT, KEY_LEFT));
	KeyMap.push_back(SKeyMap(SDLK_UP, KEY_UP));
	KeyMap.push_back(SKeyMap(SDLK_RIGHT, KEY_RIGHT));
	KeyMap.push_back(SKeyMap(SDLK_DOWN, KEY_DOWN));

	KeyMap.push_back(SKeyMap(SDLK_PRINTSCREEN, KEY_SNAPSHOT));

	KeyMap.push_back(SKeyMap(SDLK_INSERT, KEY_INSERT));
	KeyMap.push_back(SKeyMap(SDLK_DELETE, KEY_DELETE));
	KeyMap.push_back(SKeyMap(SDLK_HELP, KEY_HELP));

	KeyMap.push_back(SKeyMap(SDLK_0, KEY_KEY_0));
	KeyMap.push_back(SKeyMap(SDLK_1, KEY_KEY_1));
	KeyMap.push_back(SKeyMap(SDLK_2, KEY_KEY_2));
	KeyMap.push_back(SKeyMap(SDLK_3, KEY_KEY_3));
	KeyMap.push_back(SKeyMap(SDLK_4, KEY_KEY_4));
	KeyMap.push_back(SKeyMap(SDLK_5, KEY_KEY_5));
	KeyMap.push_back(SKeyMap(SDLK_6, KEY_KEY_6));
	KeyMap.push_back(SKeyMap(SDLK_7, KEY_KEY_7));
	KeyMap.push_back(SKeyMap(SDLK_8, KEY_KEY_8));
	KeyMap.push_back(SKeyMap(SDLK_9, KEY_KEY_9));

	KeyMap.push_back(SKeyMap(SDLK_a, KEY_KEY_A));
	KeyMap.push_back(SKeyMap(SDLK_b, KEY_KEY_B));
	KeyMap.push_back(SKeyMap(SDLK_c, KEY_KEY_C));
	KeyMap.push_back(SKeyMap(SDLK_d, KEY_KEY_D));
	KeyMap.push_back(SKeyMap(SDLK_e, KEY_KEY_E));
	KeyMap.push_back(SKeyMap(SDLK_f, KEY_KEY_F));
	KeyMap.push_back(SKeyMap(SDLK_g, KEY_KEY_G));
	KeyMap.push_back(SKeyMap(SDLK_h, KEY_KEY_H));
	KeyMap.push_back(SKeyMap(SDLK_i, KEY_KEY_I));
	KeyMap.push_back(SKeyMap(SDLK_j, KEY_KEY_J));
	KeyMap.push_back(SKeyMap(SDLK_k, KEY_KEY_K));
	KeyMap.push_back(SKeyMap(SDLK_l, KEY_KEY_L));
	KeyMap.push_back(SKeyMap(SDLK_m, KEY_KEY_M));
	KeyMap.push_back(SKeyMap(SDLK_n, KEY_KEY_N));
	KeyMap.push_back(SKeyMap(SDLK_o, KEY_KEY_O));
	KeyMap.push_back(SKeyMap(SDLK_p, KEY_KEY_P));
	KeyMap.push_back(SKeyMap(SDLK_q, KEY_KEY_Q));
	KeyMap.push_back(SKeyMap(SDLK_r, KEY_KEY_R));
	KeyMap.push_back(SKeyMap(SDLK_s, KEY_KEY_S));
	KeyMap.push_back(SKeyMap(SDLK_t, KEY_KEY_T));
	KeyMap.push_back(SKeyMap(SDLK_u, KEY_KEY_U));
	KeyMap.push_back(SKeyMap(SDLK_v, KEY_KEY_V));
	KeyMap.push_back(SKeyMap(SDLK_w, KEY_KEY_W));
	KeyMap.push_back(SKeyMap(SDLK_x, KEY_KEY_X));
	KeyMap.push_back(SKeyMap(SDLK_y, KEY_KEY_Y));
	KeyMap.push_back(SKeyMap(SDLK_z, KEY_KEY_Z));

	KeyMap.push_back(SKeyMap(SDLK_LGUI, KEY_LWIN));
	KeyMap.push_back(SKeyMap(SDLK_RGUI, KEY_RWIN));

	KeyMap.push_back(SKeyMap(SDLK_KP_0, KEY_NUMPAD0));
	KeyMap.push_back(SKeyMap(SDLK_KP_1, KEY_NUMPAD1));
	KeyMap.push_back(SKeyMap(SDLK_KP_2, KEY_NUMPAD2));
	KeyMap.push_back(SKeyMap(SDLK_KP_3, KEY_NUMPAD3));
	KeyMap.push_back(SKeyMap(SDLK_KP_4, KEY_NUMPAD4));
	KeyMap.push_back(SKeyMap(SDLK_KP_5, KEY_NUMPAD5));
	KeyMap.push_back(SKeyMap(SDLK_KP_6, KEY_NUMPAD6));
	KeyMap.push_back(SKeyMap(SDLK_KP_7, KEY_NUMPAD7));
	KeyMap.push_back(SKeyMap(SDLK_KP_8, KEY_NUMPAD8));
	KeyMap.push_back(SKeyMap(SDLK_KP_9, KEY_NUMPAD9));
	KeyMap.push_back(SKeyMap(SDLK_KP_MULTIPLY, KEY_MULTIPLY));
	KeyMap.push_back(SKeyMap(SDLK_KP_PLUS, KEY_ADD));
	KeyMap.push_back(SKeyMap(SDLK_KP_MINUS, KEY_SUBTRACT));
	KeyMap.push_back(SKeyMap(SDLK_KP_PERIOD, KEY_DECIMAL));
	KeyMap.push_back(SKeyMap(SDLK_KP_DIVIDE, KEY_DIVIDE));
	KeyMap.push_back(SKeyMap(SDLK_KP_ENTER, KEY_RETURN));

	KeyMap.push_back(SKeyMap(SDLK_F1,  KEY_F1));
	KeyMap.push_back(SKeyMap(SDLK_F2,  KEY_F2));
	KeyMap.push_back(SKeyMap(SDLK_F3,  KEY_F3));
	KeyMap.push_back(SKeyMap(SDLK_F4,  KEY_F4));
	KeyMap.push_back(SKeyMap(SDLK_F5,  KEY_F5));
	KeyMap.push_back(SKeyMap(SDLK_F6,  KEY_F6));
	KeyMap.push_back(SKeyMap(SDLK_F7,  KEY_F7));
	KeyMap.push_back(SKeyMap(SDLK_F8,  KEY_F8));
	KeyMap.push_back(SKeyMap(SDLK_F9,  KEY_F9));
	KeyMap.push_back(SKeyMap(SDLK_F10, KEY_F10));
	KeyMap.push_back(SKeyMap(SDLK_F11, KEY_F11));
	KeyMap.push_back(SKeyMap(SDLK_F12, KEY_F12));
	KeyMap.push_back(SKeyMap(SDLK_F13, KEY_F13));
	KeyMap.push_back(SKeyMap(SDLK_F14, KEY_F14));
	KeyMap.push_back(SKeyMap(SDLK_F15, KEY_F15));

	KeyMap.push_back(SKeyMap(SDLK_NUMLOCKCLEAR, KEY_NUMLOCK));
	KeyMap.push_back(SKeyMap(SDLK_SCROLLLOCK, KEY_SCROLL));
	KeyMap.push_back(SKeyMap(SDLK_LSHIFT, KEY_LSHIFT));
	KeyMap.push_back(SKeyMap(SDLK_RSHIFT, KEY_RSHIFT));
	KeyMap.push_back(SKeyMap(SDLK_LCTRL,  KEY_LCONTROL));
	KeyMap.push_back(SKeyMap(SDLK_RCTRL,  KEY_RCONTROL));
	KeyMap.push_back(SKeyMap(SDLK_LALT,  KEY_LMENU));
	KeyMap.push_back(SKeyMap(SDLK_RALT,  KEY_RMENU));

	KeyMap.push_back(SKeyMap(SDLK_PLUS,   KEY_PLUS));
	KeyMap.push_back(SKeyMap(SDLK_COMMA,  KEY_COMMA));
	KeyMap.push_back(SKeyMap(SDLK_MINUS,  KEY_MINUS));
	KeyMap.push_back(SKeyMap(SDLK_PERIOD, KEY_PERIOD));
	KeyMap.push_back(SKeyMap(SDLK_SLASH,  KEY_DIVIDE));
	KeyMap.push_back(SKeyMap(SDLK_SEMICOLON, KEY_OEM_1));
	KeyMap.push_back(SKeyMap(SDLK_EQUALS, KEY_PLUS));
	KeyMap.push_back(SKeyMap(SDLK_LEFTBRACKET, KEY_OEM_4));
	KeyMap.push_back(SKeyMap(SDLK_BACKSLASH, KEY_OEM_5));
	KeyMap.push_back(SKeyMap(SDLK_RIGHTBRACKET, KEY_OEM_6));
	KeyMap.push_back(SKeyMap(SDLK_BACKQUOTE, KEY_OEM_3));
	KeyMap.push_back(SKeyMap(SDLK_QUOTE, KEY_OEM_7));

	KeyMap.sort();
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_SDL_DEVICE_

