#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/times.h>
#include <time.h>
#include "myprog.h"
#include <float.h>
#include <pthread.h>
#include <math.h>
 
#ifndef CLK_TCK
#define CLK_TCK CLOCKS_PER_SEC
#endif

#define CENTER_BONUS 1.0
#define HASH_SIZE 524287

float SecPerMove;
char board[8][8];
char bestmove[12];
int me,cutoff,endgame;
long NumNodes;
int MaxDepth;

/*** For timing ***/
clock_t start;
struct tms bff;

/*** For the jump list ***/
int jumpptr = 0;
int jumplist[48][12];

/*** For the move list ***/
int numLegalMoves = 0;
int movelist[48][12];

int depthHit;

/* Hash storage */

int hashHits = 0;
int hashMisses = 0;

typedef struct HashEntry {
    char board[64];
    float score;
    int depth;
    float alpha, beta;
    struct HashEntry *next;
} HashEntry;

HashEntry *hashTable[HASH_SIZE];

unsigned int hash(char board[64]) {
    unsigned int hash = 0;
    for (int i = 0; i < 64; i++) {
        hash = (hash * 31 + board[i]) % HASH_SIZE;
    }
    return hash;
}

void insertToHash(char board[64], float score, int depth, float alpha, float beta) {
    if (depth < 2 || depth == MaxDepth) {
        unsigned int index = hash(board);
        HashEntry *entry = (HashEntry *)malloc(sizeof(HashEntry));
        memcpy(entry->board, board, 64);
        entry->score = score;
        entry->depth = depth;
        entry->alpha = alpha;
        entry->beta = beta;
        entry->next = hashTable[index];
        hashTable[index] = entry;
    }
}

int getFromHash(char board[64], int depth, float alpha, float beta, float *score) {
    unsigned int index = hash(board);
    HashEntry *entry = hashTable[index];
    while (entry) {
        if (memcmp(entry->board, board, 64) == 0 && entry->depth >= depth) {
            *score = entry->score;
            return 1;
        }
        entry = entry->next;
    }
    return 0;
}

void ClearHash() {
    for (int i = 0; i < HASH_SIZE; i++) {
        HashEntry *entry = hashTable[i];
        while (entry) {
            HashEntry *temp = entry;
            entry = entry->next;
            free(temp);
        }
        hashTable[i] = NULL;
    }
    hashHits = hashMisses = 0;
}


/* Hosh stoarage */

/* Print the amount of time passed since my turn began */
void PrintTime(void)
{
    clock_t current;
    float total;

    current = times(&bff);
    total = (float) ((float)current - (float)start)/CLK_TCK;
    fprintf(stderr, "Time = %f\n", total);
}

int LowOnTime(void) 
{
    clock_t current;
    float total;

    current = times(&bff);
    total = (float) ((float)current-(float)start)/CLK_TCK;
    if(total >= (SecPerMove-1.0)) return 1; else return 0;
}

/* Copy a square state */
void CopyState(char *dest, char src)
{
    char state;
    
    *dest &= Clear;
    state = src & 0xE0;
    *dest |= state;
}

/* Reset board to initial configuration */
void ResetBoard(void)
{
        int x,y;
    char pos;

        pos = 0;
        for(y=0; y<8; y++)
        for(x=0; x<8; x++)
        {
                if(x%2 != y%2) {
                        board[y][x] = pos;
                        if(y<3 || y>4) board[y][x] |= Piece; else board[y][x] |= Empty;
                        if(y<3) board[y][x] |= Red; 
                        if(y>4) board[y][x] |= White;
                        pos++;
                } else board[y][x] = 0;
        }
    endgame = 0;
}

/* Add a move to the legal move list */
void AddMove(char move[12])
{
    int i;

    for(i=0; i<12; i++) movelist[numLegalMoves][i] = move[i];
    numLegalMoves++;
}

/* Finds legal non-jump moves for the King at position x,y */
void FindKingMoves(char board[8][8], int x, int y) 
{
    int i,j,x1,y1;
    char move[12];

    memset(move,0,12*sizeof(char));

    /* Check the four adjacent squares */
    for(j=-1; j<2; j+=2)
    for(i=-1; i<2; i+=2)
    {
        y1 = y+j; x1 = x+i;
        /* Make sure we're not off the edge of the board */
        if(y1<0 || y1>7 || x1<0 || x1>7) continue; 
        if(empty(board[y1][x1])) {  /* The square is empty, so we can move there */
            move[0] = number(board[y][x])+1;
            move[1] = number(board[y1][x1])+1;    
            AddMove(move);
        }
    }
}

/* Finds legal non-jump moves for the Piece at position x,y */
void FindMoves(int player, char board[8][8], int x, int y) 
{
    int i,j,x1,y1;
    char move[12];

    memset(move,0,12*sizeof(char));

    /* Check the two adjacent squares in the forward direction */
    if(player == 1) j = 1; else j = -1;
    for(i=-1; i<2; i+=2)
    {
        y1 = y+j; x1 = x+i;
        /* Make sure we're not off the edge of the board */
        if(y1<0 || y1>7 || x1<0 || x1>7) continue; 
        if(empty(board[y1][x1])) {  /* The square is empty, so we can move there */
            move[0] = number(board[y][x])+1;
            move[1] = number(board[y1][x1])+1;    
            AddMove(move);
        }
    }
}

/* Adds a jump sequence the the legal jump list */
void AddJump(char move[12])
{
    int i;
    
    for(i=0; i<12; i++) jumplist[jumpptr][i] = move[i];
    jumpptr++;
}

/* Finds legal jump sequences for the King at position x,y */
int FindKingJump(int player, char board[8][8], char move[12], int len, int x, int y) 
{
    int i,j,x1,y1,x2,y2,FoundJump = 0;
    char one,two,mymove[12],myboard[8][8];

    memcpy(mymove,move,12*sizeof(char));

    /* Check the four adjacent squares */
    for(j=-1; j<2; j+=2)
    for(i=-1; i<2; i+=2)
    {
        y1 = y+j; x1 = x+i;
        y2 = y+2*j; x2 = x+2*i;
        /* Make sure we're not off the edge of the board */
        if(y2<0 || y2>7 || x2<0 || x2>7) continue; 
        one = board[y1][x1];
        two = board[y2][x2];
        /* If there's an enemy piece adjacent, and an empty square after hum, we can jump */
        if(!empty(one) && color(one) != player && empty(two)) {
            /* Update the state of the board, and recurse */
            memcpy(myboard,board,64*sizeof(char));
            myboard[y][x] &= Clear;
            myboard[y1][x1] &= Clear;
            mymove[len] = number(board[y2][x2])+1;
            FoundJump = FindKingJump(player,myboard,mymove,len+1,x+2*i,y+2*j);
            if(!FoundJump) {
                FoundJump = 1;
                AddJump(mymove);
            }
        }
    }
    return FoundJump;
}

/* Finds legal jump sequences for the Piece at position x,y */
int FindJump(int player, char board[8][8], char move[12], int len, int x, int y) 
{
    int i,j,x1,y1,x2,y2,FoundJump = 0;
    char one,two,mymove[12],myboard[8][8];

    memcpy(mymove,move,12*sizeof(char));

    /* Check the two adjacent squares in the forward direction */
    if(player == 1) j = 1; else j = -1;
    for(i=-1; i<2; i+=2)
    {
        y1 = y+j; x1 = x+i;
        y2 = y+2*j; x2 = x+2*i;
        /* Make sure we're not off the edge of the board */
        if(y2<0 || y2>7 || x2<0 || x2>7) continue; 
        one = board[y1][x1];
        two = board[y2][x2];
        /* If there's an enemy piece adjacent, and an empty square after hum, we can jump */
        if(!empty(one) && color(one) != player && empty(two)) {
            /* Update the state of the board, and recurse */
            memcpy(myboard,board,64*sizeof(char));
            myboard[y][x] &= Clear;
            myboard[y1][x1] &= Clear;
            mymove[len] = number(board[y2][x2])+1;
            FoundJump = FindJump(player,myboard,mymove,len+1,x+2*i,y+2*j);
            if(!FoundJump) {
                FoundJump = 1;
                AddJump(mymove);
            }
        }
    }
    return FoundJump;
}

/* Print Board */
void PrintBoard(char board[8][8])
{
    int x,y;

    /* Loop through the board array */
    for(y=0; y<8; y++) {
        for(x=0; x<8; x++) {
            if(x%2 != y%2 && !empty(board[y][x])) {
                if(king(board[y][x])) { /* King */
                    if (color(board[y][x]) == 1) fprintf(stderr, "A");
                    else fprintf(stderr, "B");
                } 
                else if(piece(board[y][x])) { /* Piece */
                    if (color(board[y][x]) == 1) fprintf(stderr, "a");
                    else fprintf(stderr, "b");  
                }
            }
            else if(x%2 == y%2) {
                // can't be here
                fprintf(stderr, "#");

            } else {
                // empty squares
                fprintf(stderr, " ");
            }
        }
        fprintf(stderr, "\n");
    }
}

/* Eval Board */
float EvalBoard(char board[8][8])
{
    int x,y;
    float one=0.0;
    float two=0.0;

    /* Loop through the board array */
    for(y=0; y<8; y++) {
        for(x=0; x<8; x++) {
            if(x%2 != y%2 && !empty(board[y][x])) {
                float pieceValue = piece(board[y][x]) ? 1.0 : 2.0;
                if(y == 0 || y == 7) pieceValue += 0.5;  // Near promotion
                if (InCenter(x, y)) pieceValue += CENTER_BONUS;
                if(color(board[y][x]) == 1) one += pieceValue;
                else two += pieceValue;
            }
        }
    }
    float rVal;
    if (me==1) rVal = one-two;
    else rVal = two-one;

    return rVal;
}

// Check if in center
int InCenter(int x, int y) {
    return (x >= 2 && x <= 5) && (y >= 2 && y <= 5);
}

/* Determines all of the legal moves possible for a given state */
int FindLegalMoves(struct State *state)
{
    int x,y;
    char move[12], board[8][8];

    memset(move,0,12*sizeof(char));
    jumpptr = numLegalMoves = 0;
    memcpy(board,state->board,64*sizeof(char));

    /* Loop through the board array, determining legal moves/jumps for each piece */
    for(y=0; y<8; y++)
    for(x=0; x<8; x++)
    {
        if(x%2 != y%2 && color(board[y][x]) == state->player && !empty(board[y][x])) {
            if(king(board[y][x])) { /* King */
                move[0] = number(board[y][x])+1;
                FindKingJump(state->player,board,move,1,x,y);
                if(!jumpptr) FindKingMoves(board,x,y);
            } 
            else if(piece(board[y][x])) { /* Piece */
                move[0] = number(board[y][x])+1;
                FindJump(state->player,board,move,1,x,y);
                if(!jumpptr) FindMoves(state->player,board,x,y);    
            }
        }    
    }
    if(jumpptr) {
        for(x=0; x<jumpptr; x++) 
        for(y=0; y<12; y++) 
        state->movelist[x][y] = jumplist[x][y];
        state->numLegalMoves = jumpptr;
    } 
    else {
        for(x=0; x<numLegalMoves; x++) 
        for(y=0; y<12; y++) 
        state->movelist[x][y] = movelist[x][y];
        state->numLegalMoves = numLegalMoves;
    }
    return (jumpptr+numLegalMoves);
}

/* Employ your favorite search to find the best move here.  */
/* This example code shows you how to call the FindLegalMoves function */
/* and the PerformMove function */
void FindBestMove(int player)
{
    int bestMoveIndex = 0; 
    struct State state;
    float bestMoveVal = -28;
    float currScore;

    /* Set up the current state */
    state.player = player;
    memcpy(state.board,board,64*sizeof(char));
    memset(bestmove,0,12*sizeof(char));

    /* Find the legal moves for the current state */
    FindLegalMoves(&state);

    for (int i=0; i < state.numLegalMoves; i++) {
        State nextState;
        // Need to setup nextState
        memcpy(&nextState, &state, sizeof(State));// taking all in one state and copying it

        PerformMove(nextState.board, nextState.movelist[i], MoveLength(nextState.movelist[i]));
        if (nextState.player == 1) nextState.player =2;
        else nextState.player =1;

        currScore=minVal(&nextState, 5);
        if (bestMoveVal < currScore) {
            bestMoveVal = currScore;
            bestMoveIndex = i;
        }
    }

    // For now, until you write your search routine, we will just set the best move
    // to be a random (legal) one, so that it plays a legal game of checkers.
    // You *will* want to replace this with a more intelligent move seleciton
    //i = rand()%state.numLegalMoves;

    memcpy(bestmove,state.movelist[bestMoveIndex],MoveLength(state.movelist[bestMoveIndex]));
}

void *timerThread(void *data) {
    int wait = (int)SecPerMove * 1000000-10000;
    usleep(wait);
    return NULL;    
}


void FindBestMoveBase(int player)
{
    pthread_t timer, fbmt;

    depthHit = 0;

    pthread_create(&timer, NULL, timerThread, NULL);

    //pthread_create(&fbmt, NULL, FindBestMoveThread, &player);
    pthread_create(&fbmt, NULL, FindBestMoveAlphaBetaThread, &player);
    pthread_detach(fbmt);
    pthread_join(timer, NULL);
    pthread_cancel(fbmt);

    fprintf(stderr, "We reached depth = %i\n", depthHit);

}

// data is player address
void *FindBestMoveThread(void *data)
{
    int player = *((int*)data);
    int bestMoveIndex = 0; 
    struct State state;
    float bestMoveVal = -28;
    float currScore;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* Set up the current state */
    state.player = player;
    memcpy(state.board,board,64*sizeof(char));
    memset(bestmove,0,12*sizeof(char));

    /* Find the legal moves for the current state */
    FindLegalMoves(&state);

    for (int depth=2;;depth++) {
        bestMoveVal = -28;
        for (int i=0; i < state.numLegalMoves; i++) {
            State nextState;
            // Need to setup nextState
            memcpy(&nextState, &state, sizeof(State));// taking all in one state and copying it

            PerformMove(nextState.board, nextState.movelist[i], MoveLength(nextState.movelist[i]));
            if (nextState.player == 1) nextState.player =2;
            else nextState.player =1;

            // eval move i
            currScore=minVal(&nextState, depth);
            if (bestMoveVal < currScore) {
                bestMoveVal = currScore;
                bestMoveIndex = i;
            }
        }
        // i just finished a search at the current depth limit
        depthHit = depth;
        memset(bestmove, 0, sizeof(bestmove));
        memcpy(bestmove,state.movelist[bestMoveIndex],MoveLength(state.movelist[bestMoveIndex]));

    }
    return NULL;
}


// data is player address
void *FindBestMoveAlphaBetaThread(void *data)
{
    int player = *((int*)data);
    int bestMoveIndex = 0; 
    struct State state;
    float bestMoveVal = -28;
    float currScore;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* Set up the current state */
    state.player = player;
    memcpy(state.board,board,64*sizeof(char));
    memset(bestmove,0,12*sizeof(char));


    for (int depth=2;;depth++) {
        bestMoveVal = -28;
        /* Find the legal moves for the current state */
        FindLegalMoves(&state);
        for (int i=0; i < state.numLegalMoves; i++) {
            State nextState;
            // Need to setup nextState
            memcpy(&nextState, &state, sizeof(State));// taking all in one state and copying it

            PerformMove(nextState.board, nextState.movelist[i], MoveLength(nextState.movelist[i]));
            if (nextState.player == 1) nextState.player =2;
            else nextState.player =1;

            // eval move i
            currScore=minValAlphaBeta(&nextState, depth, bestMoveVal, 28);
            if (bestMoveVal < currScore) {
                bestMoveVal = currScore;
                bestMoveIndex = i;
            }
        }
        // i just finished a search at the current depth limit
        depthHit = depth;
        memset(bestmove, 0, sizeof(bestmove));
        memcpy(bestmove,state.movelist[bestMoveIndex],MoveLength(state.movelist[bestMoveIndex]));

    }
    return NULL;
}



float minVal(State *state, int depth) {
    // 
    //if (!state->numLegalMoves) return minScore;
    if (--depth < 0) {
        return EvalBoard(state->board);
    } else {
        // for each successor to the current state
        // call maxVal on s
        // if maxVal(s) < currentMinVal then set currentMinValue = maxVal(s)

        /* Find the legal moves for the current state */
        FindLegalMoves(&state);

        float minScore = 24;

        for (int i=0; i < state->numLegalMoves; i++) {
            State nextState;
            float currScore;
            // Need to setup nextState
            memcpy(&nextState, state, sizeof(State));// taking all in one state and copying it

            PerformMove(nextState.board, nextState.movelist[i], MoveLength(state->movelist[i]));
            if (nextState.player == 1) nextState.player =2;
            else nextState.player =1;
            currScore=maxVal(&nextState, depth);
            if (minScore > currScore) {
                minScore = currScore;
            }
        }
        return minScore;
    }
}


float maxVal(State *state, int depth) {
    //if (!state->numLegalMoves) return maxScore;
    // 
    if (--depth < 0) {
        return EvalBoard(state->board);
    } else {
        // for each successor to the current state
        // call maxVal on s
        // if maxVal(s) < currentMinVal then set currentMinValue = maxVal(s)

        /* Find the legal moves for the current state */
        FindLegalMoves(&state);

        float maxScore = -24;

        for (int i=0; i < state->numLegalMoves; i++) {
            State nextState;
            float currScore;
            // Need to setup nextState
            memcpy(&nextState, state, sizeof(State));// taking all in one state and copying it

            PerformMove(nextState.board, nextState.movelist[i], MoveLength(nextState.movelist[i]));
            if (nextState.player == 1) nextState.player =2;
            else nextState.player =1;
            currScore=minVal(&nextState, depth);
            if (maxScore < currScore) {
                maxScore = currScore;
            }
        }
        return maxScore;
    }
}


float minValAlphaBeta(State *state, int depth, float alpha, float beta) {
    if (--depth < 0) {
        return EvalBoard(state->board);
    } else {
        
        float cachedScore;
        if (getFromHash((char *)state->board, depth, alpha, beta, &cachedScore)) {
            hashHits++;
            return cachedScore;
        }
        hashMisses++;

        /* Find the legal moves for the current state */
        FindLegalMoves(&state);
        if (state->numLegalMoves == 0) {
            return (me == 1) ? -28 : 28;
        }

        for (int i=0; i < state->numLegalMoves; i++) {
            State nextState;
            memcpy(&nextState, state, sizeof(State));// taking all in one state and copying it

            PerformMove(nextState.board, nextState.movelist[i], MoveLength(state->movelist[i]));
            nextState.player = (nextState.player == 1) ? 2 : 1;

            float currScore = maxValAlphaBeta(&nextState, depth, alpha, beta);
            beta = fmin(beta, currScore);
            if (beta <= alpha) return alpha;
        }
        
        insertToHash((char *)state->board, beta, depth, alpha, beta);
        return beta;
    }
}


float maxValAlphaBeta(State *state, int depth, float alpha, float beta) {
    if (--depth < 0) {
        return EvalBoard(state->board);
    } else {
        // for each successor to the current state
        // call maxVal on s
        // if maxVal(s) < currentMinVal then set currentMinValue = maxVal(s)

        float cachedScore;
        if (getFromHash((char *)state->board, depth, alpha, beta, &cachedScore)) {
            hashHits++;
            return cachedScore;
        }
        hashMisses++;

        /* Find the legal moves for the current state */
        FindLegalMoves(&state);

        if (state->numLegalMoves == 0) {
            return (me == 1) ? 28 : -28;
        }

        for (int i=0; i < state->numLegalMoves; i++) {
            State nextState;
            // Need to setup nextState
            memcpy(&nextState, state, sizeof(State));// taking all in one state and copying it

            PerformMove(nextState.board, nextState.movelist[i], MoveLength(nextState.movelist[i]));
            nextState.player = (nextState.player == 1) ? 2 : 1;

            float currScore=minValAlphaBeta(&nextState, depth, alpha, beta);
            alpha = fmax(alpha, currScore); // this is taking max of current socre and rval of minval
            if (alpha>=beta) return beta;
        }

        insertToHash((char *)state->board, alpha, depth, alpha, beta);
        return alpha;
    }
}

/* Converts a square label to it's x,y position */
void NumberToXY(char num, int *x, int *y)
{
    int i=0,newy,newx;

    for(newy=0; newy<8; newy++)
    for(newx=0; newx<8; newx++)
    {
        if(newx%2 != newy%2) {
            i++;
            if(i==(int) num) {
                *x = newx;
                *y = newy;
                return;
            }
        }
    }
    *x = 0; 
    *y = 0;
}

/* Returns the length of a move */
int MoveLength(char move[12])
{
    int i;

    i = 0;
    while(i<12 && move[i]) i++;
    return i;
}    

/* Converts the text version of a move to its integer array version */
int TextToMove(char *mtext, char move[12])
{
    int i=0,len=0,last;
    char val,num[64];

    while(mtext[i] != '\0') {
        last = i;
        while(mtext[i] != '\0' && mtext[i] != '-') i++;
        strncpy(num,&mtext[last],i-last);
        num[i-last] = '\0';
        val = (char) atoi(num);
        if(val <= 0 || val > 32) return 0;
        move[len] = val;
        len++;
        if(mtext[i] != '\0') i++;
    }
    if(len<2 || len>12) return 0; else return len;
}

/* Converts the integer array version of a move to its text version */
void MoveToText(char move[12], char *mtext)
{
    int i;
    char temp[8];

    mtext[0] = '\0';
    for(i=0; i<12; i++) {
        if(move[i]) {
            sprintf(temp,"%d",(int)move[i]);
            strcat(mtext,temp);
            strcat(mtext,"-");
        }
    }
    mtext[strlen(mtext)-1] = '\0';
}

/* Performs a move on the board, updating the state of the board */
void PerformMove(char board[8][8], char move[12], int mlen)
{
    int i,j,x,y,x1,y1,x2,y2;

    NumberToXY(move[0],&x,&y);
    NumberToXY(move[mlen-1],&x1,&y1);
    CopyState(&board[y1][x1],board[y][x]);
    if(y1 == 0 || y1 == 7) board[y1][x1] |= King;
    board[y][x] &= Clear;
    NumberToXY(move[1],&x2,&y2);
    if(abs(x2-x) == 2) {
        for(i=0,j=1; j<mlen; i++,j++) {
            if(move[i] > move[j]) {
                y1 = -1; 
                if((move[i]-move[j]) == 9) x1 = -1; else x1 = 1;
            }
            else {
                y1 = 1;
                if((move[j]-move[i]) == 7) x1 = -1; else x1 = 1;
            }
            NumberToXY(move[i],&x,&y);
            board[y+y1][x+x1] &= Clear;
        }
    }
}

int main(int argc, char *argv[])
{

    ClearHash();
    
    char buf[1028],move[12];
    int len,mlen,player1;

    /* Convert command line parameters */
    SecPerMove = (float) atof(argv[1]); /* Time allotted for each move */
    MaxDepth = (argc == 4) ? atoi(argv[3]) : -1;

fprintf(stderr, "%s SecPerMove == %lg\n", argv[0], SecPerMove);

    /* Determine if I am player 1 (red) or player 2 (white) */
    //fgets(buf, sizeof(buf), stdin);
    len=read(STDIN_FILENO,buf,1028);
    buf[len]='\0';
    if(!strncmp(buf,"Player1", strlen("Player1"))) 
    {
        fprintf(stderr, "I'm Player 1\n");
        player1 = 1; 
    }
    else 
    {
        fprintf(stderr, "I'm Player 2\n");
        player1 = 0;
    }
    if(player1) me = 1; else me = 2;

    /* Set up the board */ 
    ResetBoard();
    srand((unsigned int)time(0));

    if (player1) {
        start = times(&bff);
        goto determine_next_move;
    }

    for(;;) {
        /* Read the other player's move from the pipe */
        //fgets(buf, sizeof(buf), stdin);
        len=read(STDIN_FILENO,buf,1028);
        buf[len]='\0';
        start = times(&bff);
        memset(move,0,12*sizeof(char));

        /* Update the board to reflect opponents move */
        mlen = TextToMove(buf,move);
        PerformMove(board,move,mlen);
        
determine_next_move:
        fprintf(stderr, "This is the board at the beginning of my turn:\n");
        PrintBoard(board);
        fprintf(stderr, "\n\n");
        fprintf(stderr, "The score for this board is:\n");
        EvalBoard(board);
        fprintf(stderr, "\n\n");
        /* Find my move, update board, and write move to pipe */
        if(player1) FindBestMoveBase(1); else FindBestMoveBase(2);
        if(bestmove[0] != 0) { /* There is a legal move */
            mlen = MoveLength(bestmove);
            PerformMove(board,bestmove,mlen);
            MoveToText(bestmove,buf);
        }
        else exit(1); /* No legal moves available, so I have lost */

        /* Write the move to the pipe */
        //printf("%s", buf);
        write(STDOUT_FILENO,buf,strlen(buf));
        fflush(stdout);
    }

    return 0;
}


