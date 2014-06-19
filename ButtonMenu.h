// -*- c++ -*-
#ifndef PING_BUTTON_MENU_H
#define PING_BUTTON_MENU_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "Texture.h"

class ButtonMenu {
public:
    ButtonMenu();
    ~ButtonMenu();
    void init(SDL_Renderer *renderer, TTF_Font *font, const char *labels[], int numLabels, int x, int y, int spacing, bool horizCenter=false);
    void render(SDL_Renderer *renderer);
    int getButton(int x, int y);
    void selectButton(int x, int y);
    int getSelected();

private:
    Texture **selected, **unselected;
    int selectedIndex, fontHeight, numButtons, menuX, menuY, spacing;
    bool centered;
};

#endif
