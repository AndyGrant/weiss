/*
  Weiss is a UCI compliant chess engine.
  Copyright (C) 2023 Terje Kirstihagen

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stddef.h>
#include <stdlib.h>

#include "pyrrhic/tbprobe.h"
#include "noobprobe/noobprobe.h"
#include "onlinesyzygy/onlinesyzygy.h"
#include "tuner/tuner.h"
#include "board.h"
#include "makemove.h"
#include "move.h"
#include "search.h"
#include "tests.h"
#include "threads.h"
#include "time.h"
#include "transposition.h"
#include "uci.h"


extern float LMRNoisyBase;
extern float LMRNoisyDiv;
extern float LMRQuietBase;
extern float LMRQuietDiv;

extern int IIRDepth;
extern int IIRCutDepth;
extern int RFPDepth;
extern int RFPBase;
extern int RFPHistScore;
extern int RFPHistory;
extern int NMPFlat;
extern int NMPDepth;
extern int NMPHist;
extern int NMPRBase;
extern int NMPRDepth;
extern int NMPREvalDiv;
extern int NMPREvalMin;
extern int ProbCut;
extern int ProbCutDepth;
extern int ProbCutReturn;
extern int LMPImp;
extern int LMPNonImp;
extern int HistPruneDepth;
extern int HistPrune;
extern int SEEPruneDepth;
extern int SEEPruneQ;
extern int SEEPruneN;
extern int SingExtDepth;
extern int SingExtTTDepth;
extern int SingExtDouble;
extern int LMRHist;
extern int DeeperBase;
extern int DeeperDepth;

extern int QSFutility;

extern int Aspi;
extern int AspiScoreDiv;
extern int Trend;
extern float TrendDiv;
extern int PruneDiv;
extern int PruneDepthDiv;

extern int HistQDiv;
extern int HistCDiv;
extern int HistNDiv;
extern int HistBonusMax;
extern int HistBonusBase;
extern int HistBonusDepth;
extern int HistMalusMax;
extern int HistMalusBase;
extern int HistMalusDepth;

extern int Tempo;
extern int BasePower;
extern int NPower;
extern int BPower;
extern int RPower;
extern int QPower;
extern int NCPower;
extern int BCPower;
extern int RCPower;
extern int QCPower;
extern int Modifier1;
extern int Modifier2;
extern int Modifier3;
extern int Modifier4;
extern int Modifier5;
extern int Modifier6;
extern int Modifier7;
extern int Modifier8;
extern int PawnScaleBase;
extern int PawnScaleX;
extern int PawnScaleBothSides;
extern int OCBSolo;
extern int OCBDuo;

extern int ScoreMovesLimit;
extern int MPGood;
extern int MPGoodDepth;
extern int MPBad;
extern int MPBadDepth;

// Parses the time controls
static void ParseTimeControl(const char *str, const Position *pos) {

    memset(&Limits, 0, offsetof(SearchLimits, multiPV));
    Limits.start = Now();

    // Parse relevant search constraints
    Limits.infinite = strstr(str, "infinite");
    SetLimit(str, sideToMove == WHITE ? "wtime" : "btime", &Limits.time);
    SetLimit(str, sideToMove == WHITE ? "winc"  : "binc" , &Limits.inc);
    SetLimit(str, "movestogo", &Limits.movestogo);
    SetLimit(str, "movetime",  &Limits.movetime);
    SetLimit(str, "depth",     &Limits.depth);
    SetLimit(str, "mate",      &Limits.mate);

    // Parse searchmoves, assumes they are at the end of the string
    char *searchmoves = strstr(str, "searchmoves ");
    if (searchmoves) {
        char *move = strtok(searchmoves, " ");
        for (int i = 0; (move = strtok(NULL, " ")); ++i)
            Limits.searchmoves[i] = ParseMove(move, pos);
    }

    Limits.timelimit = Limits.time || Limits.movetime;
    Limits.depth = Limits.depth ?: 100;
}

// Parses the given limits and creates a new thread to start the search
INLINE void Go(Position *pos, char *str) {
    ABORT_SIGNAL = false;
    InitTT();
    ParseTimeControl(str, pos);
    StartMainThread(SearchPosition, pos);
}

// Parses a 'position' and sets up the board
static void Pos(Position *pos, char *str) {

    bool isFen = !strncmp(str, "position fen", 12);

    // Set up original position. This will either be a
    // position given as FEN, or the normal start position
    ParseFen(isFen ? str + 13 : START_FEN, pos);

    // Check if there are moves to be made from the initial position
    if ((str = strstr(str, "moves")) == NULL) return;

    // Loop over the moves and make them in succession
    char *move = strtok(str, " ");
    while ((move = strtok(NULL, " "))) {

        // Parse and make move
        MakeMove(pos, ParseMove(move, pos));

        // Keep track of how many moves have been played
        pos->gameMoves += sideToMove == WHITE;

        // Reset histPly so long games don't go out of bounds of arrays
        if (pos->rule50 == 0)
            pos->histPly = 0;
    }

    pos->nodes = 0;
}

// Parses a 'setoption' and updates settings
static void SetOption(char *str) {

    char *optionName  = strstr(str, "name") + 5;
    char *optionValue = strstr(str, "value") + 6;

    #define OptionNameIs(name) (!strncmp(optionName, name, strlen(name)))
    #define BooleanValue       (!strncmp(optionValue, "true", 4))
    #define IntValue           (atoi(optionValue))

    if      (OptionNameIs("Hash"         )) RequestTTSize(IntValue);
    else if (OptionNameIs("Threads"      )) InitThreads(IntValue);
    else if (OptionNameIs("SyzygyPath"   )) tb_init(optionValue);
    else if (OptionNameIs("MultiPV"      )) Limits.multiPV = IntValue;
    else if (OptionNameIs("NoobBookLimit")) noobLimit      = IntValue;
    else if (OptionNameIs("NoobBook"     )) noobbook       = BooleanValue;
    else if (OptionNameIs("UCI_Chess960" )) chess960       = BooleanValue;
    else if (OptionNameIs("OnlineSyzygy" )) onlineSyzygy   = BooleanValue;

    else if (OptionNameIs("LMRNoisyBase" )) LMRNoisyBase = IntValue / 100.0;
    else if (OptionNameIs("LMRNoisyDiv"  )) LMRNoisyDiv  = IntValue / 100.0;
    else if (OptionNameIs("LMRQuietBase" )) LMRQuietBase = IntValue / 100.0;
    else if (OptionNameIs("LMRQuietDiv"  )) LMRQuietDiv  = IntValue / 100.0;

    else if (OptionNameIs("IIRDepth"     )) IIRDepth   = IntValue;
    else if (OptionNameIs("IIRCutDepth"  )) IIRCutDepth= IntValue;
    else if (OptionNameIs("RFPDepth"     )) RFPDepth   = IntValue;
    else if (OptionNameIs("RFPBase"      )) RFPBase    = IntValue;
    else if (OptionNameIs("RFPHistScore" )) RFPHistScore = IntValue;
    else if (OptionNameIs("RFPHistory"   )) RFPHistory = IntValue;
    else if (OptionNameIs("NMPFlat"      )) NMPFlat    = IntValue;
    else if (OptionNameIs("NMPDepth"     )) NMPDepth   = IntValue;
    else if (OptionNameIs("NMPHist"      )) NMPHist    = IntValue;
    else if (OptionNameIs("NMPRBase"     )) NMPRBase   = IntValue;
    else if (OptionNameIs("NMPRDepth"    )) NMPRDepth  = IntValue;
    else if (OptionNameIs("NMPREvalDiv"  )) NMPREvalDiv= IntValue;
    else if (OptionNameIs("NMPREvalMin"  )) NMPREvalMin= IntValue;
    else if (OptionNameIs("ProbCut"      )) ProbCut    = IntValue;
    else if (OptionNameIs("ProbCutDepth" )) ProbCutDepth = IntValue;
    else if (OptionNameIs("ProbCutReturn" )) ProbCutReturn = IntValue;
    else if (OptionNameIs("LMPImp"       )) LMPImp     = IntValue;
    else if (OptionNameIs("LMPNonImp"    )) LMPNonImp  = IntValue;
    else if (OptionNameIs("HistPruneDepth")) HistPruneDepth = IntValue;
    else if (OptionNameIs("HistPrune"    )) HistPrune  = IntValue;
    else if (OptionNameIs("SEEPruneDepth")) SEEPruneDepth = IntValue;
    else if (OptionNameIs("SEEPruneQ"    )) SEEPruneQ  = IntValue;
    else if (OptionNameIs("SEEPruneN"    )) SEEPruneN  = IntValue;
    else if (OptionNameIs("SingExtDepth" )) SingExtDepth = IntValue;
    else if (OptionNameIs("SingExtTTDepth")) SingExtTTDepth = IntValue;
    else if (OptionNameIs("SingExtDouble")) SingExtDouble = IntValue;
    else if (OptionNameIs("LMRHist"      )) LMRHist    = IntValue;
    else if (OptionNameIs("DeeperBase"   )) DeeperBase = IntValue;
    else if (OptionNameIs("DeeperDepth"  )) DeeperDepth= IntValue;

    else if (OptionNameIs("QSFutility"   )) QSFutility = IntValue;

    else if (OptionNameIs("Aspi"         )) Aspi       = IntValue;
    else if (OptionNameIs("AspiScoreDiv" )) AspiScoreDiv = IntValue;
    else if (OptionNameIs("Trend"        )) Trend      = IntValue;
    else if (OptionNameIs("TrendDiv"     )) TrendDiv   = IntValue / 100.0;
    else if (OptionNameIs("PruneDiv"     )) PruneDiv   = IntValue;
    else if (OptionNameIs("PruneDepthDiv")) PruneDepthDiv = IntValue;

    else if (OptionNameIs("HistQDiv"     )) HistQDiv   = IntValue;
    else if (OptionNameIs("HistCDiv"     )) HistCDiv   = IntValue;
    else if (OptionNameIs("HistNDiv"     )) HistNDiv   = IntValue;
    else if (OptionNameIs("HistBonusMax" )) HistBonusMax = IntValue;
    else if (OptionNameIs("HistBonusBase")) HistBonusBase = IntValue;
    else if (OptionNameIs("HistBonusDepth")) HistBonusDepth = IntValue;
    else if (OptionNameIs("HistMalusMax" )) HistMalusMax = IntValue;
    else if (OptionNameIs("HistMalusBase")) HistMalusBase = IntValue;
    else if (OptionNameIs("HistMalusDepth")) HistMalusDepth = IntValue;

    else if (OptionNameIs("Tempo"        )) Tempo      = IntValue;
    else if (OptionNameIs("BasePower"    )) BasePower  = IntValue;
    else if (OptionNameIs("NPower"       )) NPower     = IntValue;
    else if (OptionNameIs("BPower"       )) BPower     = IntValue;
    else if (OptionNameIs("RPower"       )) RPower     = IntValue;
    else if (OptionNameIs("QPower"       )) QPower     = IntValue;
    else if (OptionNameIs("NCPower"      )) NCPower    = IntValue;
    else if (OptionNameIs("BCPower"      )) BCPower    = IntValue;
    else if (OptionNameIs("RCPower"      )) RCPower    = IntValue;
    else if (OptionNameIs("QCPower"      )) QCPower    = IntValue;
    else if (OptionNameIs("Modifier1"    )) Modifier1  = IntValue;
    else if (OptionNameIs("Modifier2"    )) Modifier2  = IntValue;
    else if (OptionNameIs("Modifier3"    )) Modifier3  = IntValue;
    else if (OptionNameIs("Modifier4"    )) Modifier4  = IntValue;
    else if (OptionNameIs("Modifier5"    )) Modifier5  = IntValue;
    else if (OptionNameIs("Modifier6"    )) Modifier6  = IntValue;
    else if (OptionNameIs("Modifier7"    )) Modifier7  = IntValue;
    else if (OptionNameIs("Modifier8"    )) Modifier8  = IntValue;
    else if (OptionNameIs("PawnScaleBase")) PawnScaleBase = IntValue;
    else if (OptionNameIs("PawnScaleX"   )) PawnScaleX = IntValue;
    else if (OptionNameIs("PawnScaleBothSides")) PawnScaleBothSides = IntValue;
    else if (OptionNameIs("OCBSolo"      )) OCBSolo    = IntValue;
    else if (OptionNameIs("OCBDuo"       )) OCBDuo     = IntValue;

    else if (OptionNameIs("ScoreMovesLimit")) ScoreMovesLimit = IntValue;
    else if (OptionNameIs("MPGood"       )) MPGood     = IntValue;
    else if (OptionNameIs("MPBadDepth"   )) MPGoodDepth = IntValue;
    else if (OptionNameIs("MPBad"        )) MPBad      = IntValue;
    else if (OptionNameIs("MPBadDepth"   )) MPBadDepth = IntValue;

    else puts("info string No such option.");

    fflush(stdout);
}

// Prints UCI info
static void Info() {
    printf("id name %s\n", NAME);
    printf("id author Terje Kirstihagen\n");
    printf("option name Hash type spin default %d min %d max %d\n", HASH_DEFAULT, HASH_MIN, HASH_MAX);
    printf("option name Threads type spin default %d min %d max %d\n", 1, 1, 2048);
    printf("option name SyzygyPath type string default <empty>\n");
    printf("option name MultiPV type spin default 1 min 1 max %d\n", MULTI_PV_MAX);
    printf("option name UCI_Chess960 type check default false\n");
    printf("option name NoobBook type check default false\n");
    printf("option name NoobBookLimit type spin default 0 min 0 max 1000\n");
    printf("option name OnlineSyzygy type check default false\n");

    printf("option name LMRNoisyBase type spin default %d min %d max %d\n", (int)(LMRNoisyBase * 100), -100000, 100000);
    printf("option name LMRNoisyDiv type spin default %d min %d max %d\n", (int)(LMRNoisyDiv * 100), -100000, 100000);
    printf("option name LMRQuietBase type spin default %d min %d max %d\n", (int)(LMRQuietBase * 100), -100000, 100000);
    printf("option name LMRQuietDiv type spin default %d min %d max %d\n", (int)(LMRQuietDiv * 100), -100000, 100000);

    printf("option name IIRDepth type spin default %d min %d max %d\n", IIRDepth, -100000, 100000);
    printf("option name IIRCutDepth type spin default %d min %d max %d\n", IIRCutDepth, -100000, 100000);
    printf("option name RFPDepth type spin default %d min %d max %d\n", RFPDepth, -100000, 100000);
    printf("option name RFPBase type spin default %d min %d max %d\n", RFPBase, -100000, 100000);
    printf("option name RFPHistScore type spin default %d min %d max %d\n", RFPHistScore, -100000, 100000);
    printf("option name RFPHistory type spin default %d min %d max %d\n", RFPHistory, -100000, 100000);
    printf("option name NMPFlat type spin default %d min %d max %d\n", NMPFlat, -100000, 100000);
    printf("option name NMPDepth type spin default %d min %d max %d\n", NMPDepth, -100000, 100000);
    printf("option name NMPHist type spin default %d min %d max %d\n", NMPHist, -100000, 100000);
    printf("option name NMPRBase type spin default %d min %d max %d\n", NMPRBase, -100000, 100000);
    printf("option name NMPRDepth type spin default %d min %d max %d\n", NMPRDepth, -100000, 100000);
    printf("option name NMPREvalDiv type spin default %d min %d max %d\n", NMPREvalDiv, -100000, 100000);
    printf("option name NMPREvalMin type spin default %d min %d max %d\n", NMPREvalMin, -100000, 100000);
    printf("option name ProbCut type spin default %d min %d max %d\n", ProbCut, -100000, 100000);
    printf("option name ProbCutDepth type spin default %d min %d max %d\n", ProbCutDepth, -100000, 100000);
    printf("option name ProbCutReturn type spin default %d min %d max %d\n", ProbCutReturn, -100000, 100000);
    printf("option name LMPImp type spin default %d min %d max %d\n", LMPImp, -100000, 100000);
    printf("option name LMPNonImp type spin default %d min %d max %d\n", LMPNonImp, -100000, 100000);
    printf("option name HistPruneDepth type spin default %d min %d max %d\n", HistPruneDepth, -100000, 100000);
    printf("option name HistPrune type spin default %d min %d max %d\n", HistPrune, -100000, 100000);
    printf("option name SEEPruneDepth type spin default %d min %d max %d\n", SEEPruneDepth, -100000, 100000);
    printf("option name SEEPruneQ type spin default %d min %d max %d\n", SEEPruneQ, -100000, 100000);
    printf("option name SEEPruneN type spin default %d min %d max %d\n", SEEPruneN, -100000, 100000);
    printf("option name SingExtDepth type spin default %d min %d max %d\n", SingExtDepth, -100000, 100000);
    printf("option name SingExtTTDepth type spin default %d min %d max %d\n", SingExtTTDepth, -100000, 100000);
    printf("option name SingExtDouble type spin default %d min %d max %d\n", SingExtDouble, -100000, 100000);
    printf("option name LMRHist type spin default %d min %d max %d\n", LMRHist, -100000, 100000);
    printf("option name DeeperBase type spin default %d min %d max %d\n", DeeperBase, -100000, 100000);
    printf("option name DeeperDepth type spin default %d min %d max %d\n", DeeperDepth, -100000, 100000);

    printf("option name QSFutility type spin default %d min %d max %d\n", QSFutility, -100000, 100000);

    printf("option name Aspi type spin default %d min %d max %d\n", Aspi, -100000, 100000);
    printf("option name AspiScoreDiv type spin default %d min %d max %d\n", AspiScoreDiv, -100000, 100000);
    printf("option name Trend type spin default %d min %d max %d\n", Trend, -100000, 100000);
    printf("option name TrendDiv type spin default %d min %d max %d\n", (int)(TrendDiv * 100), -100000, 100000);
    printf("option name PruneDiv type spin default %d min %d max %d\n", PruneDiv, -100000, 100000);
    printf("option name PruneDepthDiv type spin default %d min %d max %d\n", PruneDepthDiv, -100000, 100000);

    printf("option name HistQDiv type spin default %d min %d max %d\n", HistQDiv, -100000, 100000);
    printf("option name HistCDiv type spin default %d min %d max %d\n", HistCDiv, -100000, 100000);
    printf("option name HistNDiv type spin default %d min %d max %d\n", HistNDiv, -100000, 100000);
    printf("option name HistBonusMax type spin default %d min %d max %d\n", HistBonusMax, -100000, 100000);
    printf("option name HistBonusBase type spin default %d min %d max %d\n", HistBonusBase, -100000, 100000);
    printf("option name HistBonusDepth type spin default %d min %d max %d\n", HistBonusDepth, -100000, 100000);
    printf("option name HistMalusMax type spin default %d min %d max %d\n", HistMalusMax, -100000, 100000);
    printf("option name HistMalusBase type spin default %d min %d max %d\n", HistMalusBase, -100000, 100000);
    printf("option name HistMalusDepth type spin default %d min %d max %d\n", HistMalusDepth, -100000, 100000);

    printf("option name Tempo type spin default %d min %d max %d\n", Tempo, -100000, 100000);
    printf("option name BasePower type spin default %d min %d max %d\n", BasePower, -100000, 100000);
    printf("option name NPower type spin default %d min %d max %d\n", NPower, -100000, 100000);
    printf("option name BPower type spin default %d min %d max %d\n", BPower, -100000, 100000);
    printf("option name RPower type spin default %d min %d max %d\n", RPower, -100000, 100000);
    printf("option name QPower type spin default %d min %d max %d\n", QPower, -100000, 100000);
    printf("option name NCPower type spin default %d min %d max %d\n", NCPower, -100000, 100000);
    printf("option name BCPower type spin default %d min %d max %d\n", BCPower, -100000, 100000);
    printf("option name RCPower type spin default %d min %d max %d\n", RCPower, -100000, 100000);
    printf("option name QCPower type spin default %d min %d max %d\n", QCPower, -100000, 100000);
    printf("option name Modifier1 type spin default %d min %d max %d\n", Modifier1, -100000, 100000);
    printf("option name Modifier2 type spin default %d min %d max %d\n", Modifier2, -100000, 100000);
    printf("option name Modifier3 type spin default %d min %d max %d\n", Modifier3, -100000, 100000);
    printf("option name Modifier4 type spin default %d min %d max %d\n", Modifier4, -100000, 100000);
    printf("option name Modifier5 type spin default %d min %d max %d\n", Modifier5, -100000, 100000);
    printf("option name Modifier6 type spin default %d min %d max %d\n", Modifier6, -100000, 100000);
    printf("option name Modifier7 type spin default %d min %d max %d\n", Modifier7, -100000, 100000);
    printf("option name Modifier8 type spin default %d min %d max %d\n", Modifier8, -100000, 100000);
    printf("option name PawnScaleBase type spin default %d min %d max %d\n", PawnScaleBase, -100000, 100000);
    printf("option name PawnScaleX type spin default %d min %d max %d\n", PawnScaleX, -100000, 100000);
    printf("option name PawnScaleBothSides type spin default %d min %d max %d\n", PawnScaleBothSides, -100000, 100000);
    printf("option name OCBSolo type spin default %d min %d max %d\n", OCBSolo, -100000, 100000);
    printf("option name OCBDuo type spin default %d min %d max %d\n", OCBDuo, -100000, 100000);

    printf("option name ScoreMovesLimit type spin default %d min %d max %d\n", ScoreMovesLimit, -100000, 100000);
    printf("option name MPGood type spin default %d min %d max %d\n", MPGood, -100000, 100000);
    printf("option name MPGoodDepth type spin default %d min %d max %d\n", MPGoodDepth, -100000, 100000);
    printf("option name MPBad type spin default %d min %d max %d\n", MPBad, -100000, 100000);
    printf("option name MPBadDepth type spin default %d min %d max %d\n", MPBadDepth, -100000, 100000);

    printf("uciok\n"); fflush(stdout);
}

// Stops searching
static void Stop() {
    ABORT_SIGNAL = true;
    Wake();
    Wait(&SEARCH_STOPPED);
}

// Signals the engine is ready
static void IsReady() {
    Reinit();
    InitTT();
    puts("readyok");
    fflush(stdout);
}

// Reset for a new game
static void NewGame() {
    ClearTT();
    ResetThreads();
    failedQueries = 0;
}

// Hashes the first token in a string
static int HashInput(char *str) {
    int hash = 0;
    int len = 1;
    while (*str && *str != ' ')
        hash ^= *(str++) ^ len++;
    return hash;
}

// Sets up the engine and follows UCI protocol commands
int main(int argc, char **argv) {

    // Benchmark
    if (argc > 1 && strstr(argv[1], "bench"))
        return Benchmark(argc, argv), 0;

    // Tuner
#ifdef TUNE
    if (argc > 1 && strstr(argv[1], "tune"))
        return Tune(), 0;
#endif

    // Init engine
    InitThreads(1);
    Position pos;
    ParseFen(START_FEN, &pos);

    // Input loop
    char str[INPUT_SIZE];
    while (GetInput(str)) {
        switch (HashInput(str)) {
            case GO         : Go(&pos, str);  break;
            case UCI        : Info();         break;
            case ISREADY    : IsReady();      break;
            case POSITION   : Pos(&pos, str); break;
            case SETOPTION  : SetOption(str); break;
            case UCINEWGAME : NewGame();      break;
            case STOP       : Stop();         break;
            case QUIT       : Stop();         return 0;
#ifdef DEV
            // Non-UCI commands
            case EVAL       : PrintEval(&pos);  break;
            case PRINT      : PrintBoard(&pos); break;
            case PERFT      : Perft(str);       break;
#endif
        }
    }
}

// Translates an internal mate score into distance to mate
INLINE int MateScore(const int score) {
    int d = (MATE - abs(score) + 1) / 2;
    return score > 0 ? d : -d;
}

// Print thinking
void PrintThinking(const Thread *thread, int alpha, int beta) {

    const Position *pos = &thread->pos;

    TimePoint elapsed = TimeSince(Limits.start);
    uint64_t nodes    = TotalNodes(thread);
    uint64_t tbhits   = TotalTBHits(thread);
    int hashFull      = HashFull();
    int nps           = (int)(1000 * nodes / (elapsed + 1));

    Depth seldepth = 128;
    for (; seldepth > 0; --seldepth)
        if (history(seldepth-1).key != 0) break;

    for (int i = 0; i < Limits.multiPV; ++i) {

        const PV *pv = &thread->rootMoves[i].pv;
        int score = thread->rootMoves[i].score;

        // Skip empty pvs that occur when MultiPV > legal moves in root
        if (pv->length == 0) break;

        // Determine whether we have a centipawn or mate score
        char *type = abs(score) >= MATE_IN_MAX ? "mate" : "cp";

        // Determine if score is a lower bound, upper bound or exact
        char *bound = score >= beta  ? " lowerbound"
                    : score <= alpha ? " upperbound"
                                     : "";

        // Translate internal score into printed score
        score = abs(score) >= MATE_IN_MAX ? MateScore(score)
              :    abs(score) <= 8
                && pv->length <= 2        ? 0
                                          : score;

        // Basic info
        printf("info depth %d seldepth %d multipv %d score %s %d%s time %" PRId64
               " nodes %" PRIu64 " nps %d tbhits %" PRIu64 " hashfull %d pv",
                thread->depth, seldepth, i+1, type, score, bound, elapsed,
                nodes, nps, tbhits, hashFull);

        // Principal variation
        for (int j = 0; j < pv->length; j++)
            printf(" %s", MoveToStr(pv->line[j]));

        printf("\n");
    }
    fflush(stdout);
}

// Print conclusion of search
void PrintConclusion(const Thread *thread) {
    printf("bestmove %s\n", MoveToStr(thread->rootMoves[0].move));
    fflush(stdout);
}
