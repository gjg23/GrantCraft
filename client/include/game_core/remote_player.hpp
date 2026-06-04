#pragma once
// ------------------------------------------
// player.hpp
// Client's copy of the player state
// ------------------------------------------

#include "world/shared_player_state.hpp"

struct RemotePlayer {
    SharedPlayerState state;
};