"""Lifecycle guards for host-assisted loot beams; native readers tested separately."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
v=(root/'vr/vrcam.cpp').read_text()
s=(root/'vr/loot_beam_scan.cpp').read_text()
m=(root/'vr/melee.cpp').read_text()
assert 'lootscan::InitNetwork(ctx);' in v, 'loot channel must initialize for desktop hosts'
assert v.index('lootscan::InitNetwork(ctx);') < v.index('if(pcvrdesktop::Enabled())return StartDesktop(ctx);')
a=v.index('targetlock::TickNetwork(gamestate::LocalPlayer(),unit);')
assert v.index('lootscan::TickNetwork(',a) < v.index('if(pcvrdesktop::Enabled()){',a)
assert 'lootscan::Stop();' in v
assert 'lootreports::Init(ctx,&ScanPlayer)' in s
assert 'melee::ReadOnlyPlayer(playerId,game,player)' in s
assert 'serverloot::Scan(' in s and 'lootreports::Get(' in s
assert 'GetLevelIdFromUnitInfo.ordinal' in s
assert 'gameThread.load()!=GetCurrentThreadId()' in m[m.index('bool ReadOnlyPlayer'):m.index('bool PlayerTargets')]
print('loot lifecycle: desktop host init/tick, unload, read-only host sampler and client consumption passed')
