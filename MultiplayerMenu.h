// -*- c++ -*-
#ifndef PING_MULTIPLAYER_MENU
#define PING_MULTIPLAYER_MENU

#include "GameState.h"
#include "Texture.h"

class MultiplayerMenu: public GameState {
public:
    MultiplayerMenu(GameManager *m);
    virtual bool init();
    virtual void handleEvent(SDL_Event &event);
    virtual void render();
    void cleanup();

private:
    // Max domain name length is 253 characters, plus one for a ":"
    // and up to 5 characters for the port (max 65535).
    char inputText[260];
    Texture *inputTexture, *prompt;
};

#endif