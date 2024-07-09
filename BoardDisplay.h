#ifndef BOARDDISPLAY_H
#define BOARDDISPLAY_H

#include <SFML/Graphics.hpp>
#include "chess.h"
#include <vector>

class BoardDisplay {
public:
    BoardDisplay();
    void loadTextures();
    void setupPieces(Board& board);
    void updatePieces(Board& board);
    void draw(sf::RenderWindow& window);
    const int tileSize = 60; // Assuming 60x60 pixel tiles
    bool handleMove(sf::RenderWindow& window, Board& board);

private:
    std::vector<sf::Sprite> pieces;
    sf::Texture whitePawnTexture, whiteKnightTexture, whiteBishopTexture, whiteRookTexture, whiteQueenTexture, whiteKingTexture;
    sf::Texture blackPawnTexture, blackKnightTexture, blackBishopTexture, blackRookTexture, blackQueenTexture, blackKingTexture;
    sf::Color lightColor = sf::Color(245, 245, 220); // Light cream color
    sf::Color darkColor = sf::Color(139, 69, 19); // Dark brown color

    std::vector<Move> legalMoves;
    void loadLegalMoves(Board& board);
};

#endif
