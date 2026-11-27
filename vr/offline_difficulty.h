#pragma once
namespace offlinedifficulty {
// 0 uses the game's own setting; 1..8 replaces its effective offline count.
// Settings/update thread only. Does not write the game's global preferences.
void Configure(int players, bool vr);
void Install();
void Report();
// The native client mode getter (verified by Install) reads 0: a local
// offline game, not TCP/IP. False until Install has verified it.
bool Offline();
// The native client mode the same getter answers: 0 a local offline game, 5 or 7 TCP/IP (hosted or joined through
// D2RLoader's multiplayer panel); -1 until its bytes are verified (Install, apart from the player-count hook).
// Any thread. The wrist chat's multiplayer test (wristchat::Available).
int ClientMode();
}
