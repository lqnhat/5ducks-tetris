#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <random>

using namespace std;

constexpr int BOARD_HEIGHT     = 20;
constexpr int BOARD_WIDTH      = 15;
constexpr int BLOCK_SIZE       = 4;
constexpr int NUM_BLOCK_TYPES  = 7;

// gameplay tuning
constexpr long BASE_DROP_SPEED_US   = 500000; // base drop speed (µs)
constexpr int  DROP_INTERVAL_TICKS  = 5;      // logic steps per drop

struct Position {
    int x{}, y{};
    Position() = default;
    Position(int _x, int _y) : x(_x), y(_y) {}
};

struct GameState {
    bool running = true;
    bool gameOver = false;
    bool paused = false;
    int score = 0;
    int level = 1;
    int linesCleared = 0;
};

struct Piece {
    int type{0};
    int rotation{0};
    Position pos{5, 0};
};

void clearScreen() {
    std::cout << "\033[2J\033[H";
}

struct Board {
    char grid[BOARD_HEIGHT][BOARD_WIDTH]{};

    void init() {
        for (int i = 0; i < BOARD_HEIGHT; ++i) {
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                grid[i][j] = ' ';  // Empty space
            }
        }
    }

    void draw(const GameState& state) const {
        clearScreen();

        // Calculate widths for the board and next piece sections
        int boardWidth = BOARD_WIDTH * 2 + 2;  // 2 chars per cell + 2 for spaces
        int nextPieceWidth = 14;  // Width of next piece section

        // Top border
        cout << "╔";
        for (int i = 0; i < boardWidth; i++) cout << "═";
        cout << "╦";
        for (int i = 0; i < nextPieceWidth; i++) cout << "═";
        cout << "╗\n";

        // Title row
        cout << "║";
        string titleText = " TETRIS GAME";
        int titleLen = titleText.length();
        int totalPadding = boardWidth - titleLen;
        int leftPad = totalPadding / 2;
        int rightPad = totalPadding - leftPad;

        for (int i = 0; i < leftPad; i++) cout << " ";
        cout << titleText;
        for (int i = 0; i < rightPad; i++) cout << " ";
        cout << "║   NEXT PIECE ║\n";

        // Divider
        cout << "╠";
        for (int i = 0; i < boardWidth; i++) cout << "═";
        cout << "╬";
        for (int i = 0; i < nextPieceWidth; i++) cout << "═";
        cout << "╣\n";

        // Draw board rows
        for (int i = 0; i < BOARD_HEIGHT; ++i) {
            cout << "║ ";

            // Draw board cells (each cell is 2 chars wide)
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                cout << grid[i][j] << grid[i][j];  // Print char twice for 2-char width
            }

            cout << " ║";

            // Draw next piece section (simplified - just empty for now)
            if (i < 6) {
                for (int k = 0; k < nextPieceWidth; k++) cout << " ";
                cout << "║";
            } else if (i == 7) {
                for (int k = 0; k < nextPieceWidth; k++) cout << "─";
                cout << "║";
            } else if (i == 8) {
                cout << " SCORE:       ║";
            } else if (i == 9) {
                char scoreStr[20];
                snprintf(scoreStr, sizeof(scoreStr), " %-13d║", state.score);
                cout << scoreStr;
            } else if (i == 10) {
                for (int k = 0; k < nextPieceWidth; k++) cout << " ";
                cout << "║";
            } else if (i == 11) {
                cout << " LEVEL:       ║";
            } else if (i == 12) {
                char levelStr[20];
                snprintf(levelStr, sizeof(levelStr), " %-13d║", state.level);
                cout << levelStr;
            } else if (i == 13) {
                for (int k = 0; k < nextPieceWidth; k++) cout << " ";
                cout << "║";
            } else if (i == 14) {
                cout << " LINES:       ║";
            } else if (i == 15) {
                char linesStr[20];
                snprintf(linesStr, sizeof(linesStr), " %-13d║", state.linesCleared);
                cout << linesStr;
            } else {
                for (int k = 0; k < nextPieceWidth; k++) cout << " ";
                cout << "║";
            }

            cout << "\n";
        }

        // Bottom border
        cout << "╚";
        for (int i = 0; i < boardWidth; i++) cout << "═";
        cout << "╩";
        for (int i = 0; i < nextPieceWidth; i++) cout << "═";
        cout << "╝\n";

        cout << "\nControls: ←→ or A/D (Move)  ↑/W (Rotate)  ↓/S (Soft Drop)  SPACE (Hard Drop)  P (Pause)  Q (Quit)\n";

        // Ensure output is flushed and hide cursor
        cout << "\033[?25l" << flush;
    }

    int clearLines() {
        int linesCleared = 0;
        int writeRow = BOARD_HEIGHT - 1;

        // Scan from bottom to top
        for (int readRow = BOARD_HEIGHT - 1; readRow >= 0; --readRow) {
            bool full = true;
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                if (grid[readRow][j] == ' ') {
                    full = false;
                    break;
                }
            }

            // Keep non-full rows, skip full ones
            if (!full) {
                if (writeRow != readRow) {
                    for (int j = 0; j < BOARD_WIDTH; ++j) {
                        grid[writeRow][j] = grid[readRow][j];
                    }
                }
                --writeRow;
            } else {
                ++linesCleared;
            }
        }

        // Clear remaining top rows
        while (writeRow >= 0) {
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                grid[writeRow][j] = ' ';
            }
            --writeRow;
        }

        return linesCleared;
    }
};

struct BlockTemplate {
    static char templates[NUM_BLOCK_TYPES][BLOCK_SIZE][BLOCK_SIZE];

    static void setBlockTemplate(int type,
                                 char symbol,
                                 const int shape[BLOCK_SIZE][BLOCK_SIZE]) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                templates[type][i][j] = shape[i][j] ? symbol : ' ';
            }
        }
    }

    static void initializeTemplates() {
        static const int TETROMINOES[7][4][4] = {
            // I
            {
                {0,1,0,0},
                {0,1,0,0},
                {0,1,0,0},
                {0,1,0,0}
            },
            // O
            {
                {0,0,0,0},
                {0,1,1,0},
                {0,1,1,0},
                {0,0,0,0}
            },
            // T
            {
                {0,0,0,0},
                {0,1,0,0},
                {1,1,1,0},
                {0,0,0,0}
            },
            // S
            {
                {0,0,0,0},
                {0,1,1,0},
                {1,1,0,0},
                {0,0,0,0}
            },
            // Z
            {
                {0,0,0,0},
                {1,1,0,0},
                {0,1,1,0},
                {0,0,0,0}
            },
            // J
            {
                {0,0,0,0},
                {1,0,0,0},
                {1,1,1,0},
                {0,0,0,0}
            },
            // L
            {
                {0,0,0,0},
                {0,0,1,0},
                {1,1,1,0},
                {0,0,0,0}
            }
        };

        static const char NAMES[7] = {'I','O','T','S','Z','J','L'};

        for (int i = 0; i < 7; i++) {
            setBlockTemplate(i, NAMES[i], TETROMINOES[i]);
        }
    }

    // rotation: 0-3 (90° steps clockwise)
    static char getCell(int type, int rotation, int row, int col) {
        int r = row;
        int c = col;

        for (int i = 0; i < rotation; ++i) {
            int temp = 3 - c;
            c = r;
            r = temp;
        }

        return templates[type][r][c];
    }
};

char BlockTemplate::templates[NUM_BLOCK_TYPES][BLOCK_SIZE][BLOCK_SIZE];

struct TetrisGame {
    Board board;
    GameState state;
    Piece currentPiece{};

    termios origTermios{};
    long dropSpeedUs{BASE_DROP_SPEED_US};
    int dropCounter{0};

    std::mt19937 rng;

    TetrisGame() {
        std::random_device rd;
        rng.seed(rd());
    }

    // ---------- terminal handling (POSIX) ----------

    void enableRawMode() {
        tcgetattr(STDIN_FILENO, &origTermios);

        termios raw = origTermios;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    void disableRawMode() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
    }

    char getInput() const {
        char ch = 0;
        ssize_t result = read(STDIN_FILENO, &ch, 1);
        return (result > 0) ? ch : 0;
    }

    void flushInput() const {
        char ch;
        tcflush(STDIN_FILENO, TCIFLUSH);
    }

    // ---------- helpers for spawn / movement ----------

    bool isInsidePlayfield(int x, int y) const {
        return x >= 0 && x < BOARD_WIDTH &&
               y >= 0 && y < BOARD_HEIGHT;
    }

    // used only when spawning: ignore existing blocks, check only bounds
    bool canSpawn(const Piece& piece) const {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(piece.type, piece.rotation, i, j);
                if (cell == ' ') continue;

                int xt = piece.pos.x + j;
                int yt = piece.pos.y + i;

                // blocks can start above the field (yt < 0), but must be in bounds once in field
                if (yt >= 0 && !isInsidePlayfield(xt, yt)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool canMove(int dx, int dy, int newRotation) const {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(
                    currentPiece.type, newRotation, i, j
                );
                if (cell == ' ') continue;

                int xt = currentPiece.pos.x + j + dx;
                int yt = currentPiece.pos.y + i + dy;

                // horizontal bounds (full playable area 0..BOARD_WIDTH-1)
                if (xt < 0 || xt >= BOARD_WIDTH) return false;

                // bottom collision with floor
                if (yt >= BOARD_HEIGHT) return false;

                // above top is allowed (yt < 0), but do not read grid there
                if (yt >= 0 && board.grid[yt][xt] != ' ') return false;
            }
        }
        return true;
    }

    void placePiece(const Piece& piece, bool place) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(
                    piece.type, piece.rotation, i, j
                );
                if (cell == ' ') continue;

                int xt = piece.pos.x + j;
                int yt = piece.pos.y + i;

                if (yt < 0 || yt >= BOARD_HEIGHT ||
                    xt < 0 || xt >= BOARD_WIDTH) {
                    continue;
                }

                board.grid[yt][xt] = place ? cell : ' ';
            }
        }
    }

    void spawnNewPiece() {
        uniform_int_distribution<int> dist(0, NUM_BLOCK_TYPES - 1);
        currentPiece.type = dist(rng);
        currentPiece.rotation = 0;

        // spawn near horizontal center
        int spawnX = (BOARD_WIDTH / 2) - (BLOCK_SIZE / 2);
        currentPiece.pos = Position(spawnX, -1);

        // spawn-specific check: only bounds
        if (!canSpawn(currentPiece)) {
            state.running = false;
        }
    }

    bool lockPieceAndCheck() {
        // The current piece is already on the board, locked in place
        // Clear any full lines and update score
        int cleared = board.clearLines();

        if (cleared > 0) {
            state.linesCleared += cleared;
            // Scoring: 100 points per line * level, with combo bonuses
            int points[] = {0, 100, 250, 400, 800}; // 0, 1, 2, 3, 4 lines
            state.score += points[cleared] * state.level;
            // Level up every 10 lines
            state.level = 1 + state.linesCleared / 10;
        }

        spawnNewPiece();
        if (!state.running) {
            board.draw(state);
            cout << "\n*** GAME OVER ***\n";
            return false;
        }
        return true;
    }

    void softDrop() {
        if (canMove(0, 1, currentPiece.rotation)) {
            currentPiece.pos.y++;
        } else {
            // Piece can't move down - lock it
            // Note: piece was removed by main loop, so place it first
            placePiece(currentPiece, true);
            state.running = lockPieceAndCheck();
            dropCounter = 0;
        }
    }

    void hardDrop() {
        // Note: piece is already removed from board by main loop
        // Just calculate final position
        int distance = 0;
        while (canMove(0, 1, currentPiece.rotation)) {
            currentPiece.pos.y++;
            ++distance;
        }
        // Now piece is at landing position
        // Main loop will place it, then we lock it
        placePiece(currentPiece, true);  // Place at final position
        state.running = lockPieceAndCheck();
        dropCounter = 0;
    }

    void handleInput() {
        char c = getInput();
        if (c == 0) return;

        switch (c) {
            case 'a': // move left
                if (canMove(-1, 0, currentPiece.rotation)) {
                    currentPiece.pos.x--;
                }
                break;
            case 'd': // move right
                if (canMove(1, 0, currentPiece.rotation)) {
                    currentPiece.pos.x++;
                }
                break;
            case 'x': // soft drop one cell
                softDrop();
                break;
            case ' ': // hard drop
                hardDrop();
                flushInput(); // flush repeated spaces
                break;
            case 'w': { // rotate with basic wall kicks
                int newRot = (currentPiece.rotation + 1) % 4;
                int kicks[] = {0, -1, 1, -2, 2};
                for (int dx : kicks) {
                    if (canMove(dx, 0, newRot)) {
                        currentPiece.pos.x += dx;
                        currentPiece.rotation = newRot;
                        break;
                    }
                }
                break;
            }
            case 'q':
                state.running = false;
                break;
            default:
                break;
        }
    }

    void handleGravity() {
        if (!state.running) return;

        ++dropCounter;
        if (dropCounter < DROP_INTERVAL_TICKS) return;

        dropCounter = 0;

        if (canMove(0, 1, currentPiece.rotation)) {
            currentPiece.pos.y++;
        } else {
            // Piece can't move down - lock it
            // Note: piece was removed by main loop, so place it first
            placePiece(currentPiece, true);
            state.running = lockPieceAndCheck();
        }
    }

    void run() {
        BlockTemplate::initializeTemplates();
        board.init();

        // ensure the inner board is empty before starting
        for (int y = 1; y < BOARD_HEIGHT - 1; ++y) {
            for (int x = 1; x < BOARD_WIDTH - 1; ++x) {
                board.grid[y][x] = ' ';
            }
        }

        enableRawMode();
        spawnNewPiece();
        usleep(500000);

        while (state.running) {
            // Remove old piece position before moving
            placePiece(currentPiece, false);

            handleInput();
            handleGravity();

            // Place piece at new position for drawing
            placePiece(currentPiece, true);
            board.draw(state);

            usleep(dropSpeedUs / DROP_INTERVAL_TICKS);
        }

        disableRawMode();
        cout << "Thanks for playing!\n";
        usleep(2000000);
    }
};

int main() {
    TetrisGame game;
    game.run();
    return 0;
}