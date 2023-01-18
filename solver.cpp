/* compute optimal solutions for sliding block puzzle. */
#include <SDL2/SDL.h>
//#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <cstdlib>   /* for atexit() */
#include <algorithm>
using std::min;
using std::swap;
#include <cassert>
/* data structures for BFS: */
#include <queue>
using std::queue;
#include <unordered_map>
using std::unordered_map;
#include <vector>
using std::vector;
#include <deque>
using std::deque;
#include <unordered_set>
using std::unordered_set;
#include <set>
using std::set;

/* SDL reference: https://wiki.libsdl.org/CategoryAPI
 * */

/* initial size; will be set to screen size after window creation. */
int SCREEN_WIDTH = 640;
int SCREEN_HEIGHT = 480;
int fcount = 0;
int mousestate = 0;
SDL_Point lastm = {0,0}; /* last mouse coords */
SDL_Rect bframe; /* bounding rectangle of board */
static const int ep = 2; /* epsilon offset from grid lines */
/* for on-screen messages: */
static const char* fontpath = "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf";
//TTF_Font* font;
SDL_Texture* texture = NULL;
int msgStaleness = 0; /* fade text... */
int msgW,msgH;

/* current state of puzzle: */
unsigned char S[5][4];
uint64_t state; /* integer representation of S */
static const int numdefs = 8;
static const uint64_t defstates[numdefs] = {
	679048144404301747,
	94866746461768555,
	821205196810704816,
	815627693329141123,
	797432990330854829,
	315328739230051190,
	94934601274301869,
	83509459525978971
};
#if 0
List of nastiest 3v,2h states:
94866746461768555
317653888046950253
306395232575907693
4935532518795117
4970675774802795
306430375831915371
List of nastiest 4v,1h states:
821205196810704816
805454142989091251
List of nastiest 2v,3h states:
815627693329141123
318988476690687542
List of nastiest 1v,4h states:
797432990330854829
77032971812019629
315328739230051190
316735770516220790
List of nastiest 0v,5h states:
94934601274301869
317526950736784246
List of nastiest 5v,0h states:
83509459525978971
99318737774163693
#endif


bool init(); /* setup SDL */
void setFrame();
int loadState(uint64_t s);
void s2S(uint64_t s, unsigned char S[5][4]);  /* take integer state, fill S */
void S2s(uint64_t& s, unsigned char S[5][4]); /* compute integer state from S */

#define FULLSCREEN_FLAG SDL_WINDOW_FULLSCREEN_DESKTOP
// #define FULLSCREEN_FLAG 0

/* NOTE: no block should have type nul... */
enum bType {hor,ver,ssq,lsq,nul};
struct block {
	SDL_Rect R; /* screen coords + dimensions */
	bType type; /* shape + orientation */
	short index; /* if attached to grid, (index/4,index%4) give
	                the coordinates; else index == -1 */
	void detach() /* clear corresponding cells of board */
	{
		if (index < 0) return;
		int i = index/4;
		int j = index%4;
		assert(index<20);
		switch (type) {
			case hor:
				S[i][j] = S[i][j+1] = 0;
				break;
			case ver:
				S[i][j] = S[i+1][j] = 0;
				break;
			case ssq:
				S[i][j] = 0;
				break;
			case lsq:
				S[i][j]=S[i][j+1]=S[i+1][j]=S[i+1][j+1] = 0;
				break;
			default:
				assert(false);
				return;
		}
		index = -1;
		S2s(state,S);
	}
	void rotate() /* rotate rectangular pieces */
	{
		if (type != hor && type != ver) return;
		detach();
		type = (type==hor)?ver:hor;
		swap(R.w,R.h);
	}
};

#define NBLOCKS 10
block B[NBLOCKS];
block* dragged = NULL;

block* findBlock(int x, int y);
void close(); /* call this at end of main loop to free SDL resources */
SDL_Window* gWindow = 0; /* main window */
/* SDL renderer.  By setting flags, we can make sure what we draw
 * will be frame synchronized.  See init() below. */
SDL_Renderer* gRenderer = 0;

bool init()
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("SDL_Init failed.  Error: %s\n", SDL_GetError());
		return false;
	}
	if(!SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1")) {
		printf("Warning: vsync hint didn't work.\n");
	}
	/* create main window */
	gWindow = SDL_CreateWindow("Sliding block puzzle solver",
								SDL_WINDOWPOS_UNDEFINED,
								SDL_WINDOWPOS_UNDEFINED,
								SCREEN_WIDTH, SCREEN_HEIGHT,
								SDL_WINDOW_SHOWN|FULLSCREEN_FLAG);
	if(!gWindow) {
		printf("Failed to create main window. SDL Error: %s\n", SDL_GetError());
		return false;
	}
	/* set width and height */
	SDL_GetWindowSize(gWindow, &SCREEN_WIDTH, &SCREEN_HEIGHT);
	/* setup renderer with frame-sync'd drawing: */
	gRenderer = SDL_CreateRenderer(gWindow, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(!gRenderer) {
		printf("Failed to create renderer. SDL Error: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetRenderDrawBlendMode(gRenderer,SDL_BLENDMODE_BLEND);

	/* setup fonts */
	// if (TTF_Init() != 0) {
	// 	fprintf(stderr, "TTF_Init: %s\n",TTF_GetError());
	// 	return false;
	// }
	// font = TTF_OpenFont(fontpath,24);
	// if (!font) {
	// 	fprintf(stderr, "could not open font %s\n",fontpath);
	// 	return false;
	// }

	setFrame();
	// loadState(679048144404301747);
	loadState(94866746461768555);
	/* setup RNG */
	unsigned int seed;
	FILE* f = fopen("/dev/urandom","rb");
	fread(&seed,sizeof(unsigned int),1,f);
	fclose(f);
	srand(seed);

	return true;
}

void s2S(uint64_t s, unsigned char S[5][4])
{
	/* +-------+   +---+   +---+   +-------+
	 * | 1 | 2 |   | 3 |   | 5 |   | 6 | 6 |
	 * +-------+   +---+   +---+   +---+---+
	 *             | 4 |           | 6 | 6 |
	 *             +---+           +-------+  */
	/* NOTE: empty space is 0.  We then treat the entire grid as an integer
	 * in base 8 (for convenience in conversion).  Since the board has only
	 * 20 spaces, this amounts to at most 60 bits (20 octal digits).  */
	for (size_t i = 0; i < 20; i++) {
		S[i/4][i%4] = s & 0x7;
		s = s>>3;
	}
}

void S2s(uint64_t& s, unsigned char S[5][4])
{
	s = 0;
	for (int i = 19; i >= 0; i--) {
		s = s<<3;
		s += S[i/4][i%4];
	}
}

int loadState(uint64_t s)
{
	if (s == 0) return 0;
	/* counters for different shapes: */
	int cr  = 0;
	int css = 0;
	int cls = 0;
	block* RR = B;
	block* ss = B+5;
	block* SS = B+9;
	/* unit width and height: */
	int uw = bframe.w/4;
	int uh = bframe.h/5;
	int h = SCREEN_HEIGHT*3/4;
	int u = h/5-2*ep;
	/* load board values from s into S and try to apply to the
	 * shapes.  if anything goes wrong, undo the whole transaction. */
	unsigned char ST[5][4];
	s2S(s,ST);
	int i,j,k=0;
	while (k<20) {
		i = k/4;
		j = k%4;
		switch (ST[i][j]) {
			case 0: /* empty */
				k++;
				break;
			case 1: /* start of h-rectangle */
				if (j < 3 && ST[i][j+1] == 2 && cr < 5) {
					/* setup RR[cr++] */
					RR[cr].index = k;
					RR[cr].type = hor;
					RR[cr].R.x = bframe.x + j*uw + ep;
					RR[cr].R.y = bframe.y + i*uh + ep;
					RR[cr].R.w = 2*(u+ep);
					RR[cr].R.h = u;
					cr++;
					k+=2;
				} else {
					return 0;
				}
				break;
			case 2: /* should never happen, actually */
				return 0;
			case 3: /* start of v-rectangle */
				if (i < 4 && ST[i+1][j] == 4 && cr < 5) {
					/* setup RR[cr++] */
					RR[cr].index = k;
					RR[cr].type = ver;
					RR[cr].R.x = bframe.x + j*uw + ep;
					RR[cr].R.y = bframe.y + i*uh + ep;
					RR[cr].R.w = u;
					RR[cr].R.h = 2*(u+ep);
					cr++;
					k++;
				} else {
					return 0;
				}
				break;
			case 4: /* bottom of v-rectangle */
				if (i == 0 || ST[i-1][j] != 3) return 0;
				k++;
				break;
			case 5: /* small square */
				if (css < 4) {
					/* setup ss[css++] */
					ss[css].index = k;
					ss[css].type = ssq;
					ss[css].R.x = bframe.x + j*uw + ep;
					ss[css].R.y = bframe.y + i*uh + ep;
					ss[css].R.w = ss[css].R.h = u;
					css++;
					k++;
				} else {
					return 0;
				}
				break;
			case 6: /* large square */
				if (cls == 0 && i < 4 && j < 3 &&
						ST[i][j+1] == 6 &&
						ST[i+1][j] == 6 &&
						ST[i+1][j+1] == 6) {
					/* setup SS[cls++] */
					SS[cls].index = k;
					SS[cls].type = lsq;
					SS[cls].R.x = bframe.x + j*uw + ep;
					SS[cls].R.y = bframe.y + i*uh + ep;
					SS[cls].R.w = SS[cls].R.h = 2*(u+ep);
					cls++;
					k+=2;
				} else if (cls == 1 && i > 0 && j < 3 &&
						ST[i-1][j] == 6 &&
						ST[i-1][j+1] == 6 &&
						ST[i][j+1] == 6) {
					k+=2;
					/* NOTE: since we move k by 2 when finding a large square
					 * the only valid 6 to find would be bottom left. */
				} else {
					return 0;
				}
				break;
			default:
				/* invalid state; return 0 */
				return 0;
		}
	}
	/* check: did we use all blocks? */
	if (cr != 5 || css != 4 || cls != 1) {
		return 0;
	}
	/* seems like a legit state, so copy to global and redraw blocks. */
	state = s;
	s2S(state,S);
	return 1;
}

void setFrame()
{
	int& W = SCREEN_WIDTH;
	int& H = SCREEN_HEIGHT;
	int h = H*3/4;
	int w = 4*h/5;

	/* setup bounding rectangle of the board: */
	bframe.x = (W-w)/2;
	bframe.y = (H-h)/2;
	bframe.w = w;
	bframe.h = h;
}

void drawBlocks()
{
	/* rectangles */
	SDL_SetRenderDrawColor(gRenderer, 0x43, 0x4c, 0x5e, 0xff);
	for (size_t i = 0; i < 5; i++) {
		SDL_RenderFillRect(gRenderer,&B[i].R);
	}
	/* small squares */
	SDL_SetRenderDrawColor(gRenderer, 0x5e, 0x81, 0xac, 0xff);
	for (size_t i = 5; i < 9; i++) {
		SDL_RenderFillRect(gRenderer,&B[i].R);
	}
	/* large square */
	SDL_SetRenderDrawColor(gRenderer, 0xa3, 0xbe, 0x8c, 0xff);
	SDL_RenderFillRect(gRenderer,&B[9].R);
}

/* NOTE: we'll just setup the texture; the render function will
 * compute the coords, transparency, etc. */
// void setMessage(const char* msg)
// {
// 	if (texture) SDL_DestroyTexture(texture);
// 	SDL_Color color = {0xbf,0x61,0x6a};
// 	//SDL_Surface* surface = TTF_RenderText_Blended(font,msg,color);
// 	texture = SDL_CreateTextureFromSurface(gRenderer,surface);
// 	msgW = surface->w;
// 	msgH = surface->h;
// 	SDL_FreeSurface(surface);
// 	msgStaleness = 0;
// }

/* return a block containing (x,y), or NULL if none exists. */
block* findBlock(int x, int y)
{
	/* NOTE: we go backwards to be compatible with z-order */
	for (int i = NBLOCKS-1; i >= 0; i--) {
		if (B[i].R.x <= x && x <= B[i].R.x + B[i].R.w &&
				B[i].R.y <= y && y <= B[i].R.y + B[i].R.h)
			return (B+i);
	}
	return NULL;
}

void close()
{
	//TTF_CloseFont(font); font = NULL;
	SDL_DestroyRenderer(gRenderer); gRenderer = NULL;
	SDL_DestroyWindow(gWindow); gWindow = NULL;
	SDL_Quit();
}

void nbrs(uint64_t v, vector<uint64_t>& N)
{
	/* NOTE: we make a local state matrix that shadows the
	 * global one because we are apparently not capable of
	 * typing two letters before brackets. */
	N.clear();
	unsigned char S[5][4];
	s2S(v,S);
	uint64_t u; /* hold a neighbor */
	/* now find the 0's and see what could be moved there. */
	for (int k = 0; k < 20; k++) {
		int i = k/4;
		int j = k%4;
		if (S[i][j] != 0) continue;
		/* look left, right, up and down */
		if (j > 0) { /* OK to look left */
			switch (S[i][j-1]) {
				case 5:
					swap(S[i][j],S[i][j-1]);
					S2s(u,S);
					N.push_back(u);
					swap(S[i][j],S[i][j-1]);
					break;
				case 0:
					/* look for 5 or 2 two spaces left */
					if (j == 1) break;
					if (S[i][j-2] == 5) {
						swap(S[i][j],S[i][j-2]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i][j-2]);
					} else if (S[i][j-2] == 2) {
						S[i][j] = 2; S[i][j-1] = 1;
						S[i][j-2] = 0; S[i][j-3] = 0;
						S2s(u,S);
						N.push_back(u);
						S[i][j] = 0; S[i][j-1] = 0;
						S[i][j-2] = 2; S[i][j-3] = 1;
					}
					break;
				case 3:
					if (S[i+1][j] == 0) {
						swap(S[i][j],S[i][j-1]);
						swap(S[i+1][j],S[i+1][j-1]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i][j-1]);
						swap(S[i+1][j],S[i+1][j-1]);
					}
					break;
				case 6:
					if (i < 4 && S[i+1][j] == 0 && S[i+1][j-1] == 6) {
						swap(S[i][j],S[i][j-2]);
						swap(S[i+1][j],S[i+1][j-2]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i][j-2]);
						swap(S[i+1][j],S[i+1][j-2]);
					}
					break;
				case 2:
					S[i][j] = 2; S[i][j-1] = 1; S[i][j-2] = 0;
					S2s(u,S);
					N.push_back(u);
					S[i][j] = 0; S[i][j-1] = 2; S[i][j-2] = 1;
					break;
				default:
					/* NOTE: do not need to account for case 4, as this
					 * would have been covered earlier in a case 3, and
					 * 1 is impossible to see left. */
					break;
			}
		}
		if (j < 3) { /* OK to look right */
			switch (S[i][j+1]) {
				case 5:
					swap(S[i][j],S[i][j+1]);
					S2s(u,S);
					N.push_back(u);
					swap(S[i][j],S[i][j+1]);
					break;
				case 0:
					/* look for 5 or 1 two spaces right */
					if (j == 2) break;
					if (S[i][j+2] == 5) {
						swap(S[i][j],S[i][j+2]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i][j+2]);
					} else if (S[i][j+2] == 1) {
						S[i][j] = 1; S[i][j+1] = 2;
						S[i][j+2] = 0; S[i][j+3] = 0;
						S2s(u,S);
						N.push_back(u);
						S[i][j] = 0; S[i][j+1] = 0;
						S[i][j+2] = 1; S[i][j+3] = 2;
					}
					break;
				case 3:
					if (S[i+1][j] == 0) {
						swap(S[i][j],S[i][j+1]);
						swap(S[i+1][j],S[i+1][j+1]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i][j+1]);
						swap(S[i+1][j],S[i+1][j+1]);
					}
					break;
				case 6:
					if (i < 4 && S[i+1][j] == 0 && S[i+1][j+1] == 6) {
						swap(S[i][j],S[i][j+2]);
						swap(S[i+1][j],S[i+1][j+2]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i][j+2]);
						swap(S[i+1][j],S[i+1][j+2]);
					}
					break;
				case 1:
					S[i][j] = 1; S[i][j+1] = 2; S[i][j+2] = 0;
					S2s(u,S);
					N.push_back(u);
					S[i][j] = 0; S[i][j+1] = 1; S[i][j+2] = 2;
					break;
				default:
					/* NOTE: do not need to account for case 4, as this
					 * would have been covered earlier in a case 3, and
					 * 2 is impossible to see to the right. */
					break;
			}
		}
		if (i < 4) { /* OK to look down */
			switch (S[i+1][j]) {
				case 5:
					swap(S[i][j],S[i+1][j]);
					S2s(u,S);
					N.push_back(u);
					swap(S[i][j],S[i+1][j]);
					break;
				case 3:
					S[i][j] = 3; S[i+1][j] = 4; S[i+2][j] = 0;
					S2s(u,S);
					N.push_back(u);
					S[i][j] = 0; S[i+1][j] = 3; S[i+2][j] = 4;
					break;
				case 0:
					/* look for a 5 or 3 two spaces down */
					if (i == 3) break;
					if (S[i+2][j] == 5) {
						swap(S[i][j],S[i+2][j]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i+2][j]);
					} else if (S[i+2][j] == 3) {
						S[i][j] = 3; S[i+1][j] = 4;
						S[i+2][j] = 0; S[i+3][j] = 0;
						S2s(u,S);
						N.push_back(u);
						S[i][j] = 0; S[i+1][j] = 0;
						S[i+2][j] = 3; S[i+3][j] = 4;
					}
					break;
				case 6:
					if (j < 3 && S[i][j+1] == 0 && S[i+1][j+1] == 6) {
						swap(S[i][j],S[i+2][j]);
						swap(S[i][j+1],S[i+2][j+1]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i+2][j]);
						swap(S[i][j+1],S[i+2][j+1]);
					}
					break;
				case 1:
					if (S[i][j+1] == 0) {
						swap(S[i][j],S[i+1][j]);
						swap(S[i][j+1],S[i+1][j+1]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i+1][j]);
						swap(S[i][j+1],S[i+1][j+1]);
					}
					break;
				default:
					/* NOTE: do not need to account for case 2, as this
					 * would have been covered earlier in a case 1, and
					 * 4 is impossible to see downward. */
					break;
			}
		}
		if (i > 0) { /* OK to look up */
			switch (S[i-1][j]) {
				case 5:
					swap(S[i][j],S[i-1][j]);
					S2s(u,S);
					N.push_back(u);
					swap(S[i][j],S[i-1][j]);
					break;
				case 4:
					S[i][j] = 4; S[i-1][j] = 3; S[i-2][j] = 0;
					S2s(u,S);
					N.push_back(u);
					S[i][j] = 0; S[i-1][j] = 4; S[i-2][j] = 3;
					break;
				case 0:
					/* look for a 5 or 4 two spaces up */
					if (i == 1) break;
					if (S[i-2][j] == 5) {
						swap(S[i][j],S[i-2][j]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i-2][j]);
					} else if (S[i-2][j] == 4) {
						S[i][j] = 4; S[i-1][j] = 3;
						S[i-2][j] = 0; S[i-3][j] = 0;
						S2s(u,S);
						N.push_back(u);
						S[i][j] = 0; S[i-1][j] = 0;
						S[i-2][j] = 4; S[i-3][j] = 3;
					}
					break;
				case 6:
					if (j < 3 && S[i][j+1] == 0 && S[i-1][j+1] == 6) {
						swap(S[i][j],S[i-2][j]);
						swap(S[i][j+1],S[i-2][j+1]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i-2][j]);
						swap(S[i][j+1],S[i-2][j+1]);
					}
					break;
				case 1:
					if (S[i][j+1] == 0) {
						swap(S[i][j],S[i-1][j]);
						swap(S[i][j+1],S[i-1][j+1]);
						S2s(u,S);
						N.push_back(u);
						swap(S[i][j],S[i-1][j]);
						swap(S[i][j+1],S[i-1][j+1]);
					}
					break;
				default:
					/* NOTE: do not need to account for case 2, as this
					 * would have been covered earlier in a case 1, and
					 * 3 is impossible to see upward. */
					break;
			}
		}
	}
}

bool isSoln(uint64_t s)
{
	unsigned char S[5][4];
	s2S(s,S);
	return (S[4][1] == 6 && S[4][2] == 6);
}

/* lazily explore graph via BFS to find a shortest solution.
 * we'll export a structure containing the distances of all
 * discovered states (D contains (state,dist) pairs). */
typedef std::pair<uint64_t,size_t> sdpair;
int solve(uint64_t s, deque<uint64_t>& L, vector<sdpair>& D)
{
	unordered_map<uint64_t,uint64_t> P; /* P will store predecessors */
	D.clear(); /* clear distances */
	L.clear(); /* store solution in L */
	D.push_back(sdpair(s,0));
	if (isSoln(s)) {
		L.push_front(s);
		return 1;
	}
	queue<sdpair> Q;
	Q.push(sdpair(s,0));
	P[s] = 0; /* 0 state used to denote no parent. */
	vector<uint64_t> N;
	while (!Q.empty()) {
		sdpair vp = Q.front();
		uint64_t v = vp.first;
		size_t d = vp.second;
		Q.pop();
		/* explore the neightbors of v */
		nbrs(v,N);
		/* for each element of N that is not yet discovered, add
		 * it to the queue, and mark v as the parent. */
		for (size_t i = 0; i < N.size(); i++) {
			if (P.find(N[i]) == P.end()) {
				P[N[i]] = v;
				D.push_back(sdpair(N[i],d+1));
				Q.push(sdpair(N[i],d+1));
				if (isSoln(N[i])) {
					/* add solution steps to list and quit */
					uint64_t u = N[i];
					L.push_front(u);
					while (P[u]) {
						u = P[u];
						L.push_front(u);
					}
					return 1;
				}
			}
		}
	}
	return 0;
}

/* to begin, let's enumerate the states if given fixed numbers of
 * horizontal / vertical pieces.  Before the first call, B should have
 * all entries 0, and C should have the counts of the different types
 * of pieces (usually C[ssq] = 4, C[lsq] = 1, C[nul] = 2), and k
 * should be 0 (first location in board). */
void enumerate(unordered_set<uint64_t>& T,
		unsigned char B[5][4], unsigned char* C, int k)
{
	/* NOTE: B should point to an array of size 20 that gives the
	 * current (usually partial) board state / configuration.
	 * C is an array of size 5 which gives the count of remaining
	 * pieces, using the bType encoding.  Parameter k tells us the
	 * location where we should add the next piece.  In broad strokes,
	 * we will, for each category of block, try to place it at location
	 * k (updating B,C in accord) and make a recursive call. */
	/* First check if either we've added the last piece (signaled
	 * by each entry of C being 0).  In this case, we add the state
	 * to T and return: */
	if (C[0]+C[1]+C[2]+C[3]+C[4] == 0) {
		uint64_t result;
		S2s(result,B);
		T.insert(result);
		return;
	}
	/* if we have a block left, but ran out of space, just return: */
	if (k > 19) return;
	/* now check on the feasibility of placing each type of available
	 * piece at location k.  NOTE: we will use the convention that
	 * location k is always empty when this function is called (so the
	 * caller advances k to the next plausible posiition for the callee). */
	int i = k/4;
	int j = k%4;
	if (C[hor] > 0 && j < 3) {
		if (B[i][j+1] == 0) {
			B[i][j] = 1; B[i][j+1] = 2;
			C[hor]--;
			/* find next open spot */
			k += 2;
			while (k < 20 && B[k/4][k%4]) k++;
			enumerate(T,B,C,k);
			B[i][j] = 0; B[i][j+1] = 0;
			C[hor]++;
			k = i*4 + j;
		}
	}
	if (C[ver] > 0 && i < 4) {
		if (B[i+1][j] == 0) { /* XXX always true? */
			B[i][j] = 3; B[i+1][j] = 4;
			C[ver]--;
			/* find next open spot */
			k++;
			while (k < 20 && B[k/4][k%4]) k++;
			enumerate(T,B,C,k);
			B[i][j] = 0; B[i+1][j] = 0;
			C[ver]++;
			k = i*4 + j;
		}
	}
	if (C[ssq] > 0) {
		B[i][j] = 5;
		C[ssq]--;
		k++;
		while (k < 20 && B[k/4][k%4]) k++;
		enumerate(T,B,C,k);
		B[i][j] = 0;
		C[ssq]++;
		k = i*4 + j;
	}
	if (C[lsq] > 0 && i < 4 && j < 3) {
		if (B[i+1][j] + B[i][j+1] + B[i+1][j+1] == 0) {
			B[i][j] = B[i+1][j] = B[i][j+1] = B[i+1][j+1] = 6;
			C[lsq]--;
			k += 2;
			while (k < 20 && B[k/4][k%4]) k++;
			enumerate(T,B,C,k);
			B[i][j] = B[i+1][j] = B[i][j+1] = B[i+1][j+1] = 0;
			C[lsq]++;
			k = i*4 + j;
		}
	}
	if (C[nul] > 0) {
		C[nul]--;
		k++;
		while (k < 20 && B[k/4][k%4]) k++;
		enumerate(T,B,C,k);
		C[nul]++;
		k = i*4 + j;
	}
}

/* TODO: do the computations again, using the alternate metric of
 * how many states were explored before finding a solution. */

const char* nodefill  = "darkolivegreen3";
const char* bgcolor   = "black";
const char* edges     = "white";

/* output entire graph in dot format @_@ */
typedef std::pair<uint64_t,uint64_t> edge;
void printGraph(unsigned char nhor)
{
	unsigned char B[5][4];
	/* clear board: */
	for (size_t k = 0; k < 20; k++) {
		B[k/4][k%4] = 0;
	}
	/* initialize counts for each type of block: */
	unsigned char C[5] = {nhor,(unsigned char)(5-nhor),4,1,2};
	unordered_set<uint64_t> T;
	enumerate(T,B,C,0);
	printf("graph G{\n");
	printf("  bgcolor=%s\n  edge [color=%s]\n",bgcolor,edges);
	printf("  node [style=filled color=%s fillcolor=%s shape=circle]\n",
			edges, nodefill);
	/* first print nodes so that we can color the solution states and
	 * give each node a shorter label */
	size_t count=0;
	const char* ncolor = "\0,fillcolor=blue";
	for (auto i = T.begin(); i != T.end(); i++) {
		count++;
		int sln = isSoln(*i);
		printf("  %lu [label=%lu%s]\n",*i,count,ncolor+sln);
	}
	/* now print all the edges... */
	set<edge> E;
	vector<uint64_t> N;
	for (auto i = T.begin(); i != T.end(); i++) {
		nbrs(*i,N);
		for (size_t j = 0; j < N.size(); j++) {
			/* force pairs (u,v) to satisfy u < v  */
			if (*i < N[j]) {
				E.insert(edge(*i,N[j]));
			} else {
				E.insert(edge(N[j],*i));
			}
		}
	}
	for (auto i = E.begin(); i != E.end(); i++) {
		printf("  %lu -- %lu\n",i->first,i->second);
	}
	printf("}\n");
}

/* print subgraph of nodes in D */
/* https://www.graphviz.org/doc/info/attrs.html#k:color
 * https://en.wikipedia.org/wiki/HSL_and_HSV#HSV
 * TODO: should make distance transition more noticable by
 * modifying H in addition to S.  Or perhaps try fixing S
 * and only modifying H.  You could vary S just for the soln.
 * */
void printSG(const vector<sdpair>& D, const deque<uint64_t>& L)
{
	printf("graph G{\n");
	printf("  overlap=false\n");
	printf("  bgcolor=%s\n  edge [color=%s]\n",bgcolor,edges);
	printf("  node [style=filled color=%s fillcolor=%s shape=circle]\n",
			edges, nodefill);
	/* first print nodes so that we can color the solution states and
	 * give each node a shorter label.  Also let's avoid printing edge
	 * neighbors that weren't explored. */
	unordered_set<uint64_t> S;
	unordered_set<uint64_t> SL(L.begin(),L.end()); /* setify L */
	S.insert(D[0].first);
	printf("  %lu [label=START,fillcolor=blue]\n",D[0].first);
	float h=0.25,s,v=0.75;
	size_t M = D[D.size()-1].second; /* max distance */
	for (size_t i = 1; i < D.size()-1; i++) {
		s = D[i].second / (double)M;
		if (SL.find(D[i].first) != SL.end()) {
			printf("  %lu [label=%lu fillcolor=\"%.3f %.3f %.3f\"]\n",
					D[i].first,i,1-h,1-s,v);
		} else {
			printf("  %lu [label=%lu fillcolor=\"%.3f %.3f %.3f\"]\n",
					D[i].first,i,h,s,v);
		}
		S.insert(D[i].first);
	}
	printf("  %lu [label=END,fillcolor=blue]\n",D[D.size()-1].first);
	/* now print all the edges... */
	set<edge> E;
	vector<uint64_t> N;
	for (size_t i = 0; i < D.size(); i++) {
		nbrs(D[i].first,N);
		for (size_t j = 0; j < N.size(); j++) {
			if (S.find(N[j]) == S.end()) continue;
			/* force pairs (u,v) to satisfy u < v  */
			if (D[i].first < N[j]) {
				E.insert(edge(D[i].first,N[j]));
			} else {
				E.insert(edge(N[j],D[i].first));
			}
		}
	}
	for (auto i = E.begin(); i != E.end(); i++) {
		printf("  %lu -- %lu\n",i->first,i->second);
	}
	printf("}\n");
}

size_t countStates(unsigned char nhor)
{
	unsigned char B[5][4];
	/* clear board: */
	for (size_t k = 0; k < 20; k++) {
		B[k/4][k%4] = 0;
	}
	/* initialize counts for each type of block: */
	unsigned char C[5] = {nhor,(unsigned char)(5-nhor),4,1,2};
	unordered_set<uint64_t> T;
	enumerate(T,B,C,0); /* count total number of configurations O_O */
	return T.size();
	/* the following could take 5 minutes of CPU time, so for safety
	 * we leave it as dead code. */
	printf("computing nastiest of the %lu states...\n",T.size());
	/* now store a set of maximally bad states: */
	unordered_set<uint64_t> nasty;
	size_t N = 0;
	vector<sdpair> D;
	deque<uint64_t> L;
	for (unordered_set<uint64_t>::iterator i = T.begin(); i != T.end(); i++) {
		solve(*i,L,D);
		if (L.size() > N) {
			N = L.size();
			nasty.clear();
			nasty.insert(*i);
		} else if (L.size() == N) {
			nasty.insert(*i);
		}
	}
	printf("vvvvvvvvvv nasty states (%lu steps)\n",N);
	for (unordered_set<uint64_t>::iterator i = nasty.begin(); i != T.end(); i++) {
		printf("%lu\n",*i);
	}
	printf("^^^^^^^^^^ nasty states (%lu steps)\n",N);
	return T.size();
}

void render()
{
	/* draw entire screen to be black: */
	SDL_SetRenderDrawColor(gRenderer, 0x00, 0x00, 0x00, 0xff);
	SDL_RenderClear(gRenderer);

	/* first, draw the frame: */
	int& W = SCREEN_WIDTH;
	int& H = SCREEN_HEIGHT;
	int w = bframe.w;
	int h = bframe.h;
	SDL_SetRenderDrawColor(gRenderer, 0x39, 0x39, 0x39, 0xff);
	SDL_RenderDrawRect(gRenderer, &bframe);
	/* make a double frame */
	SDL_Rect rframe(bframe);
	int e = 3;
	rframe.x -= e; 
	rframe.y -= e;
	rframe.w += 2*e;
	rframe.h += 2*e;
	SDL_RenderDrawRect(gRenderer, &rframe);

	/* draw some grid lines: */
	SDL_Point p1,p2;
	SDL_SetRenderDrawColor(gRenderer, 0x19, 0x19, 0x1a, 0xff);
	/* vertical */
	p1.x = (W-w)/2;
	p1.y = (H-h)/2;
	p2.x = p1.x;
	p2.y = p1.y + h;
	for (size_t i = 1; i < 4; i++) {
		p1.x += w/4;
		p2.x += w/4;
		SDL_RenderDrawLine(gRenderer,p1.x,p1.y,p2.x,p2.y);
	}
	/* horizontal */
	p1.x = (W-w)/2;
	p1.y = (H-h)/2;
	p2.x = p1.x + w;
	p2.y = p1.y;
	for (size_t i = 1; i < 5; i++) {
		p1.y += h/5;
		p2.y += h/5;
		SDL_RenderDrawLine(gRenderer,p1.x,p1.y,p2.x,p2.y);
	}
	SDL_SetRenderDrawColor(gRenderer, 0xd8, 0xde, 0xe9, 0x7f);
	// SDL_SetRenderDrawColor(gRenderer, 0xbf, 0x61, 0x6a, 0x7f);
	SDL_Rect goal = {bframe.x + w/4 + ep, bframe.y + 3*h/5 + ep,
	                 w/2 - 2*ep, 2*h/5 - 2*ep};
	SDL_RenderDrawRect(gRenderer,&goal);

	/* now iterate through and draw the blocks */
	drawBlocks();
	/* draw text at bottom center, if present. */
	if (texture && msgStaleness < 256) {
		msgStaleness++;
		SDL_SetTextureAlphaMod(texture, 256 - msgStaleness);
		/* set up R using msg{W,H}, from setMessage */
		/* TODO: check to make sure this will fit on screen. */
		SDL_Rect R;
		R.x = (W-msgW)/2;
		R.y = rframe.y + rframe.h + 5*e;
		R.w = msgW;
		R.h = msgH;
		SDL_RenderCopy(gRenderer,texture,NULL,&R);
	}

	/* finally render contents on screen, which should happen once every
	 * vsync for the display */
	SDL_RenderPresent(gRenderer);
}

void snap(block* b)
{
	assert(b != NULL);
	/* upper left of grid element (i,j) will be at
	 * bframe.{x,y} + (j*bframe.w/4,i*bframe.h/5) */
	/* unattach block if it was attached; update state: */
	b->detach();
	/* translate the corner of the bounding box of the board to (0,0). */
	int x = b->R.x - bframe.x;
	int y = b->R.y - bframe.y;
	int uw = bframe.w/4;
	int uh = bframe.h/5;
	/* NOTE: in a perfect world, the above would be equal. */
	int i = (y+uh/2)/uh; /* row */
	int j = (x+uw/2)/uw; /* col */
	bool attach = false;
	if (0 <= i && i < 5 && 0 <= j && j < 4) {
		/* if relevant slots are open, move the block */
		switch (b->type) {
			case hor:
				if (j < 3 && S[i][j] + S[i][j+1] == 0) {
					S[i][j] = 1;
					S[i][j+1] = 2;
					attach = true;
				}
				break;
			case ver:
				if (i < 4 && S[i][j] + S[i+1][j] == 0) {
					S[i][j] = 3;
					S[i+1][j] = 4;
					attach = true;
				}
				break;
			case ssq:
				if (S[i][j] == 0) {
					S[i][j] = 5;
					attach = true;
				}
				break;
			case lsq:
				if (i<4 && j<3 &&
					S[i][j]+S[i][j+1]+S[i+1][j]+S[i+1][j+1] == 0) {
					S[i][j]=S[i][j+1]=S[i+1][j]=S[i+1][j+1] = 6;
					attach = true;
				}
				break;
			default:
				assert(false);
				return;
		}
		if (attach) {
			S2s(state,S);
			/* update block's index and coords: */
			b->index = i*4 + j;
			b->R.x = bframe.x + j*uw + ep;
			b->R.y = bframe.y + i*uh + ep;
		}
	}
}

int main(int argc, char *argv[])
{
	/* start SDL; create window and such: */
	if(!init()) {
		printf( "Failed to initialize from main().\n" );
		return 1;
	}
	atexit(close);
	/* set this to exit main loop. */
	bool quit = false;
	int k; /* counter used in switch */
	size_t N; /* number of states explored */
	vector<sdpair> D; /* explored states + distances */
	deque<uint64_t> L; /* store solution, if found */
	size_t step = 0;   /* step to show next */
	SDL_Event e;
	/* main loop: */
	while(!quit) {
		/* handle events */
		while(SDL_PollEvent(&e) != 0) {
			/* meta-q in i3, for example: */
			if(e.type == SDL_MOUSEMOTION) {
				if (mousestate == 1 && dragged) {
					int dx = e.button.x - lastm.x;
					int dy = e.button.y - lastm.y;
					lastm.x = e.button.x;
					lastm.y = e.button.y;
					dragged->R.x += dx;
					dragged->R.y += dy;
				}
			} else if (e.type == SDL_MOUSEBUTTONDOWN) {
				if (e.button.button == SDL_BUTTON_RIGHT) {
					block* b = findBlock(e.button.x,e.button.y);
					if (b) b->rotate();
				} else {
					mousestate = 1;
					lastm.x = e.button.x;
					lastm.y = e.button.y;
					dragged = findBlock(e.button.x,e.button.y);
				}
				/* XXX wtf happens if during a drag, someone presses yet
				 * another mouse button??  Probably we should ignore it. */
			} else if (e.type == SDL_MOUSEBUTTONUP) {
				if (e.button.button == SDL_BUTTON_LEFT) {
					mousestate = 0;
					lastm.x = e.button.x;
					lastm.y = e.button.y;
					if (dragged) {
						/* snap to grid if nearest location is empty. */
						snap(dragged);
					}
					dragged = NULL;
				}
			} else if (e.type == SDL_QUIT) {
				quit = true;
			} else if (e.type == SDL_KEYDOWN) {
				switch (e.key.keysym.sym) {
					case SDLK_ESCAPE:
					case SDLK_q:
						quit = true;
						break;
					case SDLK_LEFT:
						if (step > 0) {
							loadState(L[--step]);
						}
						break;
					case SDLK_RIGHT:
						if (step+1 < L.size()) {
							loadState(L[++step]);
						}
						break;
					case SDLK_e:
						/* enumerate all states */
						N = countStates(2);
						fprintf(stderr,"found %lu states.\n",N);
						char message[32]; message[31] = 0;
						snprintf(message,31,"%lu states",N);
						//setMessage(message);
						break;
					case SDLK_p:
						/* print the state to stdout */
						fprintf(stderr, "state == %lu\n",state);
						for (size_t i = 0; i < 5; i++) {
							for (size_t j = 0; j < 4; j++) {
								fprintf(stderr,"%hi ",S[i][j]);
							}
							fprintf(stderr,"\n");
						}
						break;
					case SDLK_g:
						/* print entire graph to stdout (yikes) */
						// printGraph(1);
						printSG(D,L);
						break;
					case SDLK_r:
						/* load random (difficult) state */
						loadState(defstates[rand() % numdefs]);
						/* TODO: add option to load actual random state? */
						break;
					case SDLK_s:
						/* try to find a solution */
						k = 0;
						step = 0;
						while (k < NBLOCKS && B[k].index != -1) k++;
						if (k == NBLOCKS && solve(state,L,D)) {
							N = D.size();
							fprintf(stderr,"found solution of %lu steps"
									" (explored %lu states)\n", L.size()-1,N);
							char message[32]; message[31] = 0;
							snprintf(message,31,"%lu (%lu)",L.size()-1,N);
							//setMessage(message);
						} else if (k == NBLOCKS) {
							fprintf(stderr,"no solution found"
									" (%lu states explored)\n",D.size());
							//setMessage("no solution");
						} else {
							fprintf(stderr,"Please place all blocks.\n");
						}
						break;
					default:
						break;
				}
			}
		}
		fcount++;
		render();
	}

	fprintf(stderr,"total frames rendered: %i\n",fcount);
	return 0;
}





// /* compute optimal solutions for sliding block puzzle. */
// #include <SDL2/SDL.h>
// // #include <SDL2/SDL_ttf.h>
// #include <stdio.h>
// #include <cstdlib>   /* for atexit() */
// #include <algorithm>
// using std::swap;
// #include <cassert>

// /* SDL reference: https://wiki.libsdl.org/CategoryAPI */

// /* initial size; will be set to screen size after window creation. */
// int SCREEN_WIDTH = 640;
// int SCREEN_HEIGHT = 480;
// int fcount = 0;
// int mousestate = 0;
// SDL_Point lastm = {0,0}; /* last mouse coords */
// SDL_Rect bframe; /* bounding rectangle of board */
// static const int ep = 2; /* epsilon offset from grid lines */

// bool init(); /* setup SDL */
// void initBlocks();

// //#define FULLSCREEN_FLAG SDL_WINDOW_FULLSCREEN_DESKTOP
//  #define FULLSCREEN_FLAG 0

// /* NOTE: ssq == "small square", lsq == "large square" */
// enum bType {hor,ver,ssq,lsq};
// struct block {
// 	SDL_Rect R; /* screen coords + dimensions */
// 	bType type; /* shape + orientation */
// 	/* TODO: you might want to add other useful information to
// 	 * this struct, like where it is attached on the board.
// 	 * (Alternatively, you could just compute this from R.x and R.y,
// 	 * but it might be convenient to store it directly.) */
// 	void rotate() /* rotate rectangular pieces */
// 	{
// 		if (type != hor && type != ver) return;
// 		type = (type==hor)?ver:hor;
// 		swap(R.w,R.h);
// 	}
// };

// #define NBLOCKS 10
// block B[NBLOCKS];
// block* dragged = NULL;

// block* findBlock(int x, int y);
// void close(); /* call this at end of main loop to free SDL resources */
// SDL_Window* gWindow = 0; /* main window */
// SDL_Renderer* gRenderer = 0;

// bool init()
// {
// 	if(SDL_Init(SDL_INIT_VIDEO) < 0) {
// 		printf("SDL_Init failed.  Error: %s\n", SDL_GetError());
// 		return false;
// 	}
// 	/* NOTE: take this out if you have issues, say in a virtualized
// 	 * environment: */
// 	if(!SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1")) {
// 		printf("Warning: vsync hint didn't work.\n");
// 	}
// 	/* create main window */
// 	gWindow = SDL_CreateWindow("Sliding block puzzle solver",
// 								SDL_WINDOWPOS_UNDEFINED,
// 								SDL_WINDOWPOS_UNDEFINED,
// 								SCREEN_WIDTH, SCREEN_HEIGHT,
// 								SDL_WINDOW_SHOWN|FULLSCREEN_FLAG);
// 	if(!gWindow) {
// 		printf("Failed to create main window. SDL Error: %s\n", SDL_GetError());
// 		return false;
// 	}
// 	/* set width and height */
// 	SDL_GetWindowSize(gWindow, &SCREEN_WIDTH, &SCREEN_HEIGHT);
// 	/* setup renderer with frame-sync'd drawing: */
// 	gRenderer = SDL_CreateRenderer(gWindow, -1,
// 			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
// 	if(!gRenderer) {
// 		printf("Failed to create renderer. SDL Error: %s\n", SDL_GetError());
// 		return false;
// 	}
// 	SDL_SetRenderDrawBlendMode(gRenderer,SDL_BLENDMODE_BLEND);

// 	initBlocks();
// 	return true;
// }

// /* TODO: you'll probably want a function that takes a state / configuration
//  * and arranges the blocks in accord.  This will be useful for stepping
//  * through a solution.  Be careful to ensure your underlying representation
//  * stays in sync with what's drawn on the screen... */

// void initBlocks()
// {
// 	int& W = SCREEN_WIDTH;
// 	int& H = SCREEN_HEIGHT;
// 	int h = H*3/4;
// 	int w = 4*h/5;
// 	int u = h/5-2*ep;
// 	int mw = (W-w)/2;
// 	int mh = (H-h)/2;

// 	/* setup bounding rectangle of the board: */
// 	bframe.x = (W-w)/2;
// 	bframe.y = (H-h)/2;
// 	bframe.w = w;
// 	bframe.h = h;

// 	/* NOTE: there is a tacit assumption that should probably be
// 	 * made explicit: blocks 0--4 are the rectangles, 5-8 are small
// 	 * squares, and 9 is the big square.  This is assumed by the
// 	 * drawBlocks function below. */

// 	for (size_t i = 0; i < 5; i++) {
// 		B[i].R.x = (mw-2*u)/2;
// 		B[i].R.y = mh + (i+1)*(u/5) + i*u;
// 		B[i].R.w = 2*(u+ep);
// 		B[i].R.h = u;
// 		B[i].type = hor;
// 	}
// 	B[4].R.x = mw+ep;
// 	B[4].R.y = mh+ep;
// 	B[4].R.w = 2*(u+ep);
// 	B[4].R.h = u;
// 	B[4].type = hor;
// 	/* small squares */
// 	for (size_t i = 0; i < 4; i++) {
// 		B[i+5].R.x = (W+w)/2 + (mw-2*u)/2 + (i%2)*(u+u/5);
// 		B[i+5].R.y = mh + ((i/2)+1)*(u/5) + (i/2)*u;
// 		B[i+5].R.w = u;
// 		B[i+5].R.h = u;
// 		B[i+5].type = ssq;
// 	}
// 	B[9].R.x = B[5].R.x + u/10;
// 	B[9].R.y = B[7].R.y + u + 2*u/5;
// 	B[9].R.w = 2*(u+ep);
// 	B[9].R.h = 2*(u+ep);
// 	B[9].type = lsq;
// }

// void drawBlocks()
// {
// 	/* rectangles */
// 	SDL_SetRenderDrawColor(gRenderer, 0x43, 0x4c, 0x5e, 0xff);
// 	for (size_t i = 0; i < 5; i++) {
// 		SDL_RenderFillRect(gRenderer,&B[i].R);
// 	}
// 	/* small squares */
// 	SDL_SetRenderDrawColor(gRenderer, 0x5e, 0x81, 0xac, 0xff);
// 	for (size_t i = 5; i < 9; i++) {
// 		SDL_RenderFillRect(gRenderer,&B[i].R);
// 	}
// 	/* large square */
// 	SDL_SetRenderDrawColor(gRenderer, 0xa3, 0xbe, 0x8c, 0xff);
// 	SDL_RenderFillRect(gRenderer,&B[9].R);
// }

// /* return a block containing (x,y), or NULL if none exists. */
// block* findBlock(int x, int y)
// {
// 	/* NOTE: we go backwards to be compatible with z-order */
// 	for (int i = NBLOCKS-1; i >= 0; i--) {
// 		if (B[i].R.x <= x && x <= B[i].R.x + B[i].R.w &&
// 				B[i].R.y <= y && y <= B[i].R.y + B[i].R.h)
// 			return (B+i);
// 	}
// 	return NULL;
// }

// void close()
// {
// 	SDL_DestroyRenderer(gRenderer); gRenderer = NULL;
// 	SDL_DestroyWindow(gWindow); gWindow = NULL;
// 	SDL_Quit();
// }

// void render()
// {
// 	/* draw entire screen to be black: */
// 	SDL_SetRenderDrawColor(gRenderer, 0x00, 0x00, 0x00, 0xff);
// 	SDL_RenderClear(gRenderer);

// 	/* first, draw the frame: */
// 	int& W = SCREEN_WIDTH;
// 	int& H = SCREEN_HEIGHT;
// 	int w = bframe.w;
// 	int h = bframe.h;
// 	SDL_SetRenderDrawColor(gRenderer, 0x39, 0x39, 0x39, 0xff);
// 	SDL_RenderDrawRect(gRenderer, &bframe);
// 	/* make a double frame */
// 	SDL_Rect rframe(bframe);
// 	int e = 3;
// 	rframe.x -= e; 
// 	rframe.y -= e;
// 	rframe.w += 2*e;
// 	rframe.h += 2*e;
// 	SDL_RenderDrawRect(gRenderer, &rframe);

// 	/* draw some grid lines: */
// 	SDL_Point p1,p2;
// 	SDL_SetRenderDrawColor(gRenderer, 0x19, 0x19, 0x1a, 0xff);
// 	/* vertical */
// 	p1.x = (W-w)/2;
// 	p1.y = (H-h)/2;
// 	p2.x = p1.x;
// 	p2.y = p1.y + h;
// 	for (size_t i = 1; i < 4; i++) {
// 		p1.x += w/4;
// 		p2.x += w/4;
// 		SDL_RenderDrawLine(gRenderer,p1.x,p1.y,p2.x,p2.y);
// 	}
// 	/* horizontal */
// 	p1.x = (W-w)/2;
// 	p1.y = (H-h)/2;
// 	p2.x = p1.x + w;
// 	p2.y = p1.y;
// 	for (size_t i = 1; i < 5; i++) {
// 		p1.y += h/5;
// 		p2.y += h/5;
// 		SDL_RenderDrawLine(gRenderer,p1.x,p1.y,p2.x,p2.y);
// 	}
// 	SDL_SetRenderDrawColor(gRenderer, 0xd8, 0xde, 0xe9, 0x7f);
// 	SDL_Rect goal = {bframe.x + w/4 + ep, bframe.y + 3*h/5 + ep,
// 	                 w/2 - 2*ep, 2*h/5 - 2*ep};
// 	SDL_RenderDrawRect(gRenderer,&goal);

// 	/* now iterate through and draw the blocks */
// 	drawBlocks();
// 	/* finally render contents on screen, which should happen once every
// 	 * vsync for the display */
// 	SDL_RenderPresent(gRenderer);
// }

// void snap(block* b)
// {
// 	/* TODO: once you have established a representation for configurations,
// 	 * you should update this function to make sure the configuration is
// 	 * updated when blocks are placed on the board, or taken off.  */
// 	assert(b != NULL);
// 	/* upper left of grid element (i,j) will be at
// 	 * bframe.{x,y} + (j*bframe.w/4,i*bframe.h/5) */
// 	/* translate the corner of the bounding box of the board to (0,0). */
// 	int x = b->R.x - bframe.x;
// 	int y = b->R.y - bframe.y;
// 	int uw = bframe.w/4;
// 	int uh = bframe.h/5;
// 	/* NOTE: in a perfect world, the above would be equal. */
// 	int i = (y+uh/2)/uh; /* row */
// 	int j = (x+uw/2)/uw; /* col */
// 	if (0 <= i && i < 5 && 0 <= j && j < 4) {
// 		b->R.x = bframe.x + j*uw + ep;
// 		b->R.y = bframe.y + i*uh + ep;
// 	}
// }

// int main(int argc, char *argv[])
// {
// 	/* TODO: add option to specify starting state from cmd line? */
// 	/* start SDL; create window and such: */
// 	if(!init()) {
// 		printf( "Failed to initialize from main().\n" );
// 		return 1;
// 	}
// 	atexit(close);
// 	bool quit = false; /* set this to exit main loop. */
// 	SDL_Event e;
// 	/* main loop: */
// 	while(!quit) {
// 		/* handle events */
// 		while(SDL_PollEvent(&e) != 0) {
// 			/* meta-q in i3, for example: */
// 			if(e.type == SDL_MOUSEMOTION) {
// 				if (mousestate == 1 && dragged) {
// 					int dx = e.button.x - lastm.x;
// 					int dy = e.button.y - lastm.y;
// 					lastm.x = e.button.x;
// 					lastm.y = e.button.y;
// 					dragged->R.x += dx;
// 					dragged->R.y += dy;
// 				}
// 			} else if (e.type == SDL_MOUSEBUTTONDOWN) {
// 				if (e.button.button == SDL_BUTTON_RIGHT) {
// 					block* b = findBlock(e.button.x,e.button.y);
// 					if (b) b->rotate();
// 				} else {
// 					mousestate = 1;
// 					lastm.x = e.button.x;
// 					lastm.y = e.button.y;
// 					dragged = findBlock(e.button.x,e.button.y);
// 				}
// 				/* XXX happens if during a drag, someone presses yet
// 				 * another mouse button??  Probably we should ignore it. */
// 			} else if (e.type == SDL_MOUSEBUTTONUP) {
// 				if (e.button.button == SDL_BUTTON_LEFT) {
// 					mousestate = 0;
// 					lastm.x = e.button.x;
// 					lastm.y = e.button.y;
// 					if (dragged) {
// 						/* snap to grid if nearest location is empty. */
// 						snap(dragged);
// 					}
// 					dragged = NULL;
// 				}
// 			} else if (e.type == SDL_QUIT) {
// 				quit = true;
// 			} else if (e.type == SDL_KEYDOWN) {
// 				switch (e.key.keysym.sym) {
// 					case SDLK_ESCAPE:
// 					case SDLK_q:
// 						quit = true;
// 						break;
// 					case SDLK_LEFT:
// 						/* TODO: show previous step of solution */
// 						break;
// 					case SDLK_RIGHT:
// 						/* TODO: show next step of solution */
// 						break;
// 					case SDLK_p:
// 						/* TODO: print the state to stdout
// 						 * (maybe for debugging purposes...) */
// 						break;
// 					case SDLK_s:
// 						/* TODO: try to find a solution */
// 						break;
// 					default:
// 						break;
// 				}
// 			}
// 		}
// 		fcount++;
// 		render();
// 	}

// 	printf("total frames: %i\n",fcount);
// 	return 0;
// }