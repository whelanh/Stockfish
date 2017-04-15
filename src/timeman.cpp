/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "search.h"
#include "timeman.h"
#include "uci.h"

TimeManagement Time; // Our global time management object

// move_importance() is a skew-logistic function based on naive statistical
// analysis of "how many games are still undecided after n half-moves". Game
// is considered "undecided" as long as neither side has >275cp advantage.
// Data was extracted from the CCRL game database with some simple filtering criteria.

double move_importance(int ply) {

    const double XScale = 7.64;
    const double XShift = 58.4;
    const double Skew   = 0.183;

    return pow((1 + exp((ply - XShift) / XScale)), -Skew) + DBL_MIN; // Ensure non-zero
}

int remaining(int myTime, int movesToGo, int ply) {

    double moveImportance = move_importance(ply);
    double otherMovesImportance = 0;

    for (int i = 1; i < movesToGo; ++i)
        otherMovesImportance += move_importance(ply + 2 * i);

    return int(myTime * moveImportance / (moveImportance + otherMovesImportance));  // Intel C++ asks for an explicit cast
}


/// init() is called at the beginning of the search and calculates the allowed
/// thinking time out of the time control and current game ply. We support four
/// different kinds of time controls, passed in 'limits':
///
///  inc == 0 && movestogo == 0 means: x basetime  [sudden death!]
///  inc == 0 && movestogo != 0 means: x moves in y minutes
///  inc >  0 && movestogo == 0 means: x basetime + z increment
///  inc >  0 && movestogo != 0 means: x moves in y minutes + z increment

void TimeManagement::init(Search::LimitsType& limits, Color us, int ply) {

  int minThinkingTime = Options["Minimum Thinking Time"];
  int moveOverhead    = Options["Move Overhead"];
  int npmsec          = Options["nodestime"];
  int MoveHorizon     = 50;   // Plan time management at most this many moves ahead

  // If we have to play in 'nodes as time' mode, then convert from time
  // to nodes, and use resulting values in time management formulas.
  // WARNING: Given npms (nodes per millisecond) must be much lower then
  // the real engine speed to avoid time losses.
  if (npmsec)
  {
      if (!availableNodes) // Only once at game start
          availableNodes = npmsec * limits.time[us]; // Time is in msec

      // Convert from millisecs to nodes
      limits.time[us] = (int)availableNodes;
      limits.inc[us] *= npmsec;
      limits.npmsec = npmsec;
  }

  startTime = limits.startTime;
  optimumTime = maximumTime = std::max(limits.time[us] - 3 * moveOverhead, minThinkingTime);

  if (limits.movestogo)
      MoveHorizon = limits.movestogo - 1;
  // Calculate thinking time for hypothetical "moves to go"-value
  int hypMyTime = limits.time[us] + (limits.inc[us] - moveOverhead) * MoveHorizon;

  optimumTime = std::min(remaining(hypMyTime, MoveHorizon, ply), optimumTime);
  maximumTime = std::min(63 * optimumTime / 10, maximumTime);


  if (Options["Ponder"])
      optimumTime += optimumTime / 4;
}
