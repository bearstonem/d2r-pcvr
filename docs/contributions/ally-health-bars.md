# Mercenary and party health bars

Health bars for actual party members and the player's owned mercenary. Choose **Off / Wrist / Overhead / Both** under **VR Settings > Interface > Wrist > Allies**. Wrist rows sit below the companion strip; overhead text maintains an approximately constant apparent size. Unknown or stale identity, health or position is hidden rather than guessed.

Server sampling remains game-thread-only; copied health values use their own networking channel. Initialization and ticking are retained for desktop hosts as well as VR hosts.

## Verification

Included in the PCVR 0.3.35 combined multiplayer build: 267/267 automated tests passed, followed by a successful complete-install flatscreen-host/VR-client gameplay test. This is not exhaustive validation of every health display mode or terrain case. See [multiplayer support](multiplayer-support.md) for the current test scope.

## Known limitations

The mercenary label is generic. Overhead bars may show through scenery; uneven-terrain placement needs further gameplay testing. Individual portraits and mercenary names are not included. These bars are separate from native enemy bars.
