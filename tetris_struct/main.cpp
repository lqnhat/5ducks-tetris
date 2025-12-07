#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

using namespace std;
#define H 20
#define W 15
char board[H][W] = {};

// --- [NEW] Add variables for next block and score ---
int x, y, b, next_b; 
int score = 0;
// ----------------------------------------------------

char blocks[][4][4] ={
    {{' ','I',' ',' '},
     {' ','I',' ',' '},
     {' ','I',' ',' '},
     {' ','I',' ',' '}},
    {{' ',' ',' ',' '},
     {'I','I','I','I'},
     {' ',' ',' ',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {' ','O','O',' '},
     {' ','O','O',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {' ','O','O',' '},
     {' ','O','O',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {' ','O','O',' '},
     {' ','O','O',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {' ','O','O',' '},
     {' ','O','O',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {' ','T',' ',' '},
     {'T','T','T',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {' ','S','S',' '},
     {'S','S',' ',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {'Z','Z',' ',' '},
     {' ','Z','Z',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {'J',' ',' ',' '},
     {'J','J','J',' '},
     {' ',' ',' ',' '}},
    {{' ',' ',' ',' '},
     {' ',' ','L',' '},
     {'L','L','L',' '},
     {' ',' ',' ',' '}}
};

// macOS-compatible kbhit() replacement
int kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if(ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }

    return 0;
}

// macOS-compatible getch() replacement
char getch() {
    char buf = 0;
    struct termios old = {0};
    fflush(stdout);
    if(tcgetattr(0, &old) < 0)
        perror("tcsetattr()");
    old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;
    if(tcsetattr(0, TCSANOW, &old) < 0)
        perror("tcsetattr ICANON");
    if(read(0, &buf, 1) < 0)
        perror("read()");
    old.c_lflag |= ICANON;
    old.c_lflag |= ECHO;
    if(tcsetattr(0, TCSADRAIN, &old) < 0)
        perror("tcsetattr ~ICANON");
    return buf;
}

bool canMove(int dx, int dy){
    for (int i = 0; i < 4; i++ )
        for (int j = 0; j < 4; j++ )
            if (blocks[b][i][j] != ' ') {
                int xt = x + j + dx;
                int yt = y + i + dy;
                if (xt < 1 || xt >= W-1 || yt >= H-1 ) return false;
                if (board[yt][xt] != ' ') return false;
            }
    return true;
}

void block2Board(){
    for (int i = 0; i < 4; i++ )
        for (int j = 0; j < 4; j++ )
            if (blocks[b][i][j] != ' ')
                board[y+i][x+j] = blocks[b][i][j];
}

void boardDelBlock(){
    for (int i = 0; i < 4; i++ )
        for (int j = 0; j < 4; j++ )
            if (blocks[b][i][j] != ' ')
                board[y+i][x+j] = ' ';
}

void initBoard(){
    for (int i = 0 ; i < H ; i++)
        for (int j = 0 ; j < W ; j++)
            if (i == 0 || i == H-1 || j ==0 || j == W-1) board[i][j] = '#';
            else board[i][j] = ' ';
}

// --- [UPDATED] Draw function to display Next Block and Score ---
void draw(){
    // Clear screen (macOS compatible)
    cout << "\033[2J\033[1;1H";

    // Display score at the top
    cout << "Score: " << score << endl;

    for (int i = 0 ; i < H ; i++){
        // 1. Draw the current row of the board
        for (int j = 0 ; j < W ; j++) cout << board[i][j];
        
        // 2. Draw the information panel on the side (Next Block)
        // Row 2: Print "Next:" label
        if (i == 2) cout << "\tNext:";
        
        // Rows 3 to 6: Print the shape of the next block (next_b)
        else if (i >= 3 && i < 7) {
            cout << "\t"; // Tab for alignment
            for (int k = 0; k < 4; k++) {
                cout << blocks[next_b][i-3][k];
            }
        }
        
        // Newline after drawing one horizontal row
        cout << endl;
    }
}
// ----------------------------------------------------------

// --- [UPDATED] Function to remove lines and update score ---
void removeLine(){
    // Check and remove full lines (Line Clearance)
    int i,j;
    for (i = H-2 ; i > 0 ; i-- ){
        for (j = 0 ; j < W ; j++)
            if (board[i][j] == ' ') break;
        if (j == W){
            // If line is full, shift lines down
            for (int ii = i ; ii > 0 ; ii--)
                for (int jj = 0; jj < W; jj++)
                    board[ii][jj] = board[ii-1][jj];
            
            score += 100; // Add 100 points for each cleared line
            
            i++; // Re-check current line (since upper lines dropped down)
            draw(); // Redraw to create effect
            usleep(200000); 
        }
    }
}
// ----------------------------------------------------------

int main()
{
    srand(time(0));
    
    // --- [UPDATED] Initialize first block and next block ---
    next_b = rand() % 7; // Randomize next block
    b = rand() % 7;      // Randomize current block
    // ----------------------------------------------------------
    
    x = 5; y = 0; 
    initBoard();

    cout << "Tetris Game - Controls: a=left, d=right, x=down, q=quit" << endl;
    usleep(1000000); 

    while (1){
        boardDelBlock();
        if (kbhit()){
            char c = getch();
            if (c == 'a' && canMove(-1,0)) x--;
            if (c == 'd' && canMove( 1,0)) x++;
            if (c == 'x' && canMove( 0,1)) y++;
            if (c == 'q') break;
        }
        
        if (canMove(0,1)) y++;
        else{
            block2Board();
            removeLine();
            
            // --- [UPDATED] Logic to bring next block into play ---
            b = next_b;        // Move next block to current
            next_b = rand()%7; // Generate new next block
            x = 5; y = 0;      // Reset starting position
            // ----------------------------------------------------
        }
        
        block2Board();
        draw();
        usleep(300000); // Increase game speed slightly (300ms) for smoothness
    }

    cout << "Game Over! Final Score: " << score << endl;
    return 0;
}
