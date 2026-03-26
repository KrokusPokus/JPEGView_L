cd <bit7z folder>
mkdir build && cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release -DBIT7Z_AUTO_FORMAT=ON -DBIT7Z_7ZIP_VERSION="26.00" -DBIT7Z_STATIC_RUNTIME=ON
cmake --build . -j --config Release


For some reason, using "-DBIT7Z_USE_NATIVE_STRING=ON" will cause linker errors.
