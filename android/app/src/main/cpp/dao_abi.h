// The version of the interface between the app's Java side (GameActivity.DAO_ABI) and the game
// library libdaodroid.so: the JNI methods, the SDL build, the command-line options. A game
// package built for another version is refused by the launcher instead of crashing. Raise it
// whenever one of those changes.
#pragma once

#define DAO_ABI_VERSION 1
