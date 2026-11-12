# The game-drawn ceiling's Granny remesher (test-only)

- `granny_remesh.h`: from https://github.com/natpoh/diablo2-vr-port at commit
  `91cb1e85466c730652eecb665f5355e92b56d1dc`, `shared/granny_remesh.h`. MIT, Copyright (c) 2026 natpoh; see `LICENSE`.
  Unchanged apart from a three-line note at the top.
- `rans.h`: pybg3's BitKnit2 decoder, which that header includes. It was taken by that repository from
  https://github.com/eiz/pybg3 at commit `3b02e59bb4484c27e49e51ee9d83e8a9d09df4ed`, `src/rans.h`. MIT, Copyright (c) 2024
  Mackenzie Straight; see `pybg3-LICENSE`.

Neither is built into the mod. `steamvr/game_ceiling_golden.cpp` compiles them only for
`steamvr/prepare_game_ceiling_test.py`, which checks that the installer's Python port (`steamvr/granny_write.py`) writes
the same bytes.
