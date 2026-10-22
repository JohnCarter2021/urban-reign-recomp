// The PNG handlers of compat/GSTextureReplacementLoaders.cpp (tools/make_loaders.py): the Windows Imaging Component on
// Windows (no libpng to build there), PCSX2's own libpng handlers elsewhere.
#if defined(_WIN32)
#include "PngWic.inl"
#else
#include "PngLibpng.inl"
#endif
