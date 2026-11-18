rmdir /s /q build
mkdir build
cd build
cmake .. -A Win32 -DIRRLICHT_INCLUDE_DIR="../irrlicht/include" -DIRRLICHT_LIBRARY="../irrlicht/lib/Win32-visualstudio/Irrlicht.lib"
cd ..
cmake --build build --config Release