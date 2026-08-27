#pragma once

// Build identity, defined by CMake — never hand-typed. The version used to be
// duplicated in main.cpp and drifted from 0.1 to 0.3 unnoticed.
//
// SHACKBOOK_BUILD_TAG is defined for any build that is NOT an official release
// (release CI configures with -DSHACKBOOK_RELEASE=ON). It exists so a developer
// build can SAY it is one: an untagged dev build looks identical on screen to
// the installed copy, and on 2026-08-05 a feature was tested against a
// months-old installed binary because nothing on the window said otherwise.

#include <QString>

#ifndef SHACKBOOK_VERSION
#  define SHACKBOOK_VERSION "0.0.0-unknown"
#endif

namespace ShackBook {

// "0.4.1" for a release, "0.4.1-dev" for a working build.
inline QString versionString()
{
#ifdef SHACKBOOK_BUILD_TAG
    return QStringLiteral(SHACKBOOK_VERSION "-" SHACKBOOK_BUILD_TAG);
#else
    return QStringLiteral(SHACKBOOK_VERSION);
#endif
}

// True when this binary is a working build rather than an installed release.
inline bool isDevBuild()
{
#ifdef SHACKBOOK_BUILD_TAG
    return true;
#else
    return false;
#endif
}

} // namespace ShackBook
