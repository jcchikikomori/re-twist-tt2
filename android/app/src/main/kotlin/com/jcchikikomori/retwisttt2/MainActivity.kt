package com.jcchikikomori.retwisttt2

import org.libsdl.app.SDLActivity

// SDLActivity (SDL3's Java class) creates the window surface, loads
// libSDL3.so and libmain.so (getLibraries()), and calls SDL_main in
// ui/src/main.cpp on its own thread. Nothing to override yet.
class MainActivity : SDLActivity()
