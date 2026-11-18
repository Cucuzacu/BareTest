cd irrlicht
rmdir /s /q build
mkdir build
cd ..
cmake -B irrlicht/build -S irrlicht -A Win32
cmake --build irrlicht/build --config Release