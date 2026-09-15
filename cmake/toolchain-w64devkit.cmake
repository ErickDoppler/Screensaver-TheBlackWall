# Toolchain file for the portable GCC/MinGW-w64 kit in C:\workenv\w64devkit.
# Everything is open source: GCC (GPL, runtime exception), mingw-w64 (permissive).
set(W64DEVKIT_ROOT "C:/workenv/w64devkit" CACHE PATH "w64devkit location")
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER   "${W64DEVKIT_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${W64DEVKIT_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER  "${W64DEVKIT_ROOT}/bin/windres.exe")
set(CMAKE_AR           "${W64DEVKIT_ROOT}/bin/ar.exe")
set(CMAKE_RANLIB       "${W64DEVKIT_ROOT}/bin/ranlib.exe")
