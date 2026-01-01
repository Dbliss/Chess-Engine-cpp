#pragma once

#ifndef BOARDDISPLAY_H
#define BOARDDISPLAY_H

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include "chess.h"
#include <vector>
#include "engine2.h"

class BoardDisplay {
public:
    BoardDisplay();
    void loadTextures();
    void setupPieces(Board& board);
    void updatePieces(sf::RenderWindow& window, Board& board);
    void draw(sf::RenderWindow& window);
    void draw(sf::RenderWindow& window, Board& board);
    const int tileSize = 80;
    bool handleMove(sf::RenderWindow& window, Board& board);
    void loadSounds();

private:
    std::vector<sf::Sprite> pieces;
    sf::Texture whitePawnTexture, whiteKnightTexture, whiteBishopTexture, whiteRookTexture, whiteQueenTexture, whiteKingTexture;
    sf::Texture blackPawnTexture, blackKnightTexture, blackBishopTexture, blackRookTexture, blackQueenTexture, blackKingTexture;
    sf::Color lightColor = sf::Color(245, 245, 220); // Light cream color
    sf::Color darkColor = sf::Color(139, 69, 19); // Dark brown color
    sf::Color lastMoveColor = sf::Color(170, 170, 255);

    std::vector<Move> legalMoves;
    void loadLegalMoves(Board& board);

    const float scaleFactor = tileSize / 60.0f; // Assuming original size is 60x60

    // Sound buffers and sounds
    sf::SoundBuffer moveBuffer;
    sf::SoundBuffer checkBuffer;
    sf::SoundBuffer checkmateBuffer;
    sf::SoundBuffer captureBuffer;

    sf::Sound moveSound{moveBuffer};
    sf::Sound checkSound{checkBuffer};
    sf::Sound checkmateSound{checkmateBuffer};
    sf::Sound captureSound{captureBuffer};
};

#endif
