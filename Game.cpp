#include <sstream>
#include "GameManager.h"
#include "Game.h"
#include "GameState.h"
#include "ErrorScreen.h"
#include "Texture.h"
#include "utility.h"

Game::Game(GameManager *m)
    : GameState(m),
      ball(m->WIDTH/2, m->HEIGHT/2-10, 20, 20, rand() % 2 * 4 - 2, rand() % 2 * 4 - 2),
      player(20, m->HEIGHT/2-30, 20, 80), opponent(m->WIDTH-40, m->HEIGHT/2-40, 20, 80),
      score1(0), score2(0), server(NULL), socketSet(NULL) {
}

Game::~Game() {
    if (networked) {
        if (server != NULL)
            SDLNet_TCP_Close(server);
        if (socketSet != NULL)
            SDLNet_FreeSocketSet(socketSet);
    }

    delete playerInput;
    delete opponentInput;
}

// Currently, having an opponent input component and running a networked
// game are mutually exclusive. It'd be neat to have a subclass of
// PaddleInput for networked input, but I don't see how that'd be
// possible, currently (though networking needs a huge overhaul anyway
// so who knows what might happen).
bool Game::init(PaddleInput *p1input, PaddleInput *p2input) {
    playerInput = p1input;
    opponentInput = p2input;
    networked = false;
    return true;
}

bool Game::init(PaddleInput *p1input, const char *host) {
    playerInput = p1input;
    opponentInput = NULL;

    networked = true;
    
    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, host, 5556) != 0) {
        SDLerror("SDLNet_ResolveHost");
        std::stringstream ss;
        ss << "Failed to resolve host: " << host;
        return errorScreen(m, ss.str().c_str());
    }

    server = SDLNet_TCP_Open(&ip);
    if (server == NULL) {
        SDLerror("SDLNet_TCP_Open");
        return errorScreen(m, "Failed to open connection.");
    }

    socketSet = SDLNet_AllocSocketSet(1);
    if (socketSet == NULL) {
        SDLerror("SDLNet_AllocSocketSet");
        return errorScreen(m, "Failed to allocate socket set.");
    }

    SDLNet_TCP_AddSocket(socketSet, server);

    return netWait();
}

bool Game::netWait() {
    if (SDLNet_CheckSockets(socketSet, 10000) < 1)
        return errorWithScreen(m, "Connection to server timed out.");

    char buffer[2];
    int recvLen = SDLNet_TCP_Recv(server, buffer, 2);
    if (recvLen < 2)
        return errorWithScreen(m, "Server disconnected.");

    std::stringstream ss;
    ss << "This client is player " << buffer << ".";
    debug(ss.str().c_str());

    char *msg = netReadLine(server);
    if (msg == NULL)
        return errorWithScreen(m, "Server disconnected.");

    int seed = atoi(msg);
    srand(seed);
    ball.dX = rand() % 2 * 4 - 2;
    ball.dY = rand() % 2 * 4 - 2;

    if (buffer[0] == '2') {
        ball.dX *= -1;
        SDLNet_TCP_Send(server, "hi\n", 4);
        return true;
    }

    while (SDLNet_CheckSockets(socketSet, 10000) < 1)
        debug("Waiting for opponent to join...");

    char buffer2[4];
    recvLen = SDLNet_TCP_Recv(server, buffer2, 4);
    if (recvLen < 1)
        return errorWithScreen(m, "Server disconnected.");
    else if (recvLen < 4)
        return errorWithScreen(m, "Opponent disconnected.");
    else if (strcmp(buffer2, "hi\n") != 0)
        return errorWithScreen(m, "Incorrect greeting.");

    return true;
}

void Game::handleEvent(SDL_Event &event) {
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
        m->revertState();
}

void Game::handleInput() {
    Entity *paddles[] = { &player, &opponent };
    PaddleInput *inputs[] = { playerInput, opponentInput };

    for (int i = 0; i < 2; i++) {
        if (inputs[i] == NULL)
            continue;
        int change = inputs[i]->update(*paddles[i]);
        int min = -10, max = 10;
        if (abs(change + paddles[i]->dY) < abs(paddles[i]->dY)) {
            change *= 2;
            if (paddles[i]->dY > 0)
                min = 0;
            else
                max = 0;
        }
        paddles[i]->dY = clamp(paddles[i]->dY + change, min, max);
    }
}

void Game::update() {
    handleInput();

    int s1 = 0, s2 = 0;
    while (networked && SDLNet_CheckSockets(socketSet, 0)) {
        char *msg = netReadLine(server);
        if (msg == NULL) {
            // TODO: Perhaps this should show a dialog or
            // semi-transparent screen instead? Might be nice to see
            // the final score.
            errorWithScreen(m, "Host disconnected.");
            return;
        } else {
            std::stringstream ss(msg);
            double bX;
            double bdX;
            ss >> bX >> ball.y >> bdX >> ball.dY >> opponent.y >> opponent.dY >> s1 >> s2;
            ball.x = m->WIDTH - bX - ball.w;
            ball.dX = -bdX;
            free(msg);
        }
    }

    player.y += player.dY;
    if (player.y < 0 || player.y + player.h > m->HEIGHT) {
        player.y = clamp(player.y, 0, m->HEIGHT-player.h);
        player.dY = 0;
    }
    opponent.y += opponent.dY;
    if (opponent.y < 0 || opponent.y + opponent.h > m->HEIGHT) {
        opponent.y = clamp(opponent.y, 0, m->HEIGHT-opponent.h);
        opponent.dY = 0;
    }

    double oldX = ball.x, oldY = ball.y;
    ball.x += ball.dX;
    ball.y += ball.dY;
    if (ball.y < 0 || ball.y + ball.h > m->HEIGHT) {
        ball.dY *= -1;
        // Like with the paddle bouncing code below, extra distance is
        // re-applied, so the ball travels at a consistent rate
        // despite bouncing.
        if (ball.y < 0)
            ball.y = ball.dY - oldY;
        else
            ball.y = m->HEIGHT - ball.h + ball.dY + (m->HEIGHT - ball.h) - oldY;
        Mix_PlayChannel(-1, m->bounceSound, 0);
    }

    bool which, collisionPlayer = checkCollision(ball, player), collisionOpponent = checkCollision(ball, opponent);
    // This is in case the ball's movement speed is greater than the
    // width of a paddle, which would otherwise allow the ball to
    // phase through it. It checks if the ball went completely through
    // the paddle's area, and if so "moves" the ball along its path to
    // the paddle's x axis and checks for collision there.
    if ((which = (oldX > player.x + player.w && ball.x < player.x)) || (oldX < opponent.x && ball.x > opponent.x + opponent.w)) {
        if (which) {
            double intersectY = ball.dY/ball.dX * (player.x + player.w - oldX) + ball.y;
            Entity testBall(player.x + player.w, intersectY, ball.w, ball.h, 0, 0);
            collisionPlayer |= checkCollision(testBall, player);
        } else {
            double intersectY = ball.dY/ball.dX * (opponent.x - ball.w - oldX) + ball.y;
            Entity testBall(opponent.x - ball.w, intersectY, ball.w, ball.h, 0, 0);
            collisionOpponent |= checkCollision(testBall, opponent);
        }
    }

    // The collided flag prevents the ball from colliding with the
    // same paddle multiple times in one hit; this is an issue, for
    // instance, when a paddle hits a ball with one of its smaller
    // side edges (as the ball may not move out of the way quickly enough).
    if (!collided && (collisionPlayer || collisionOpponent)) {
        ball.dX = clamp(ball.dX * -1.1, -923, 923);
        if (collisionPlayer) {
            ball.dY = clamp(ball.dY + player.dY / 2, -747, 747);
            if (ball.x + ball.w >= player.x && ball.x <= player.x + player.w) {
                collided = true;
            } else {
                // If the ball hits the paddle fast enough to go
                // through it, the extra distance (that it would have
                // gone if it hadn't hit the paddle) is reapplied in
                // the ball's new direction.
                double dXremainder = ball.dX - oldX + player.x + player.w;
                ball.x = player.x + player.w + dXremainder;
            }
        } else {
            ball.dY = clamp(ball.dY + opponent.dY / 2, -747, 747);
            if (ball.x + ball.w >= opponent.x && ball.x <= opponent.x + opponent.w) {
                collided = true;
            } else {
                double dXremainder = ball.dX + opponent.x - ball.w - oldX;
                ball.x = opponent.x - ball.w + dXremainder;
            }
        }
        Mix_PlayChannel(-1, m->hitSound, 0);
    } else if (collided && !collisionPlayer && !collisionOpponent) {
        collided = false;
    }

    if (player.dY > 0)
        player.dY = clamp(player.dY - .1, 0, 10);
    else
        player.dY = clamp(player.dY + .1, -10, 0);

    if (opponent.dY > 0)
        opponent.dY = clamp(opponent.dY - .1, 0, 10);
    else
        opponent.dY = clamp(opponent.dY + .1, -10, 0);

    if (ball.x + ball.w < 0 || ball.x > m->WIDTH) {
        if (ball.x + ball.w < 0) {
            score2++;
            ball.dX = 2;
        } else {
            score1++;
            ball.dX = -2;
        }
        ball.dY = rand() % 2 * 4 - 2;
        ball.x = m->WIDTH / 2 - ball.w / 2;
        ball.y = m->HEIGHT / 2 - ball.h / 2;
    }

    if (networked) {
        if (s2 > score1)
            score1 = s2;
        if (s1 > score2)
            score2 = s1;

        std::stringstream ss;
        ss << ball.x << " " << ball.y << " " << ball.dX << " " << ball.dY << " "
           << player.y << " " << player.dY << " " << score1 << " " << score2 << "\n";
        std::string s = ss.str();
        SDLNet_TCP_Send(server, s.c_str(), s.length()+1);
    }
}

void Game::render(double lag) {
    m->background->render(m->renderer, 0, 0);

    SDL_SetRenderDrawColor(m->renderer, 0xff, 0xff, 0xff, 0xff);
    player.render(m->renderer, lag);
    opponent.render(m->renderer, lag);
    ball.render(m->renderer, lag);

    char buf[21]; // Max number of characters for a 64-bit int in base 10.
    Texture *tScore1 = Texture::fromText(m->renderer, m->font48, itoa(score1, buf, 21), 0xff, 0xff, 0xff);
    Texture *tScore2 = Texture::fromText(m->renderer, m->font48, itoa(score2, buf, 21), 0xff, 0xff, 0xff);
    tScore1->render(m->renderer, m->WIDTH/4 - tScore1->w/2, 40);
    tScore2->render(m->renderer, m->WIDTH*3/4 - tScore2->w/2, 40);
    delete tScore1;
    delete tScore2;
}
