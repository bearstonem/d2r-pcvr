from pathlib import Path
r=Path(__file__).resolve().parent.parent
m=(r/'vr/melee.cpp').read_text()
v=(r/'vr/vrcam.cpp').read_text()
t=(r/'steamvr/transport.cpp').read_text()
assert 'meleed ispatch' not in m
assert 'meleed ispatch' not in v
assert 'meleed'+'ispatch::Send' in m, 'original detector must forward its sweep'
assert 'bool ApplyRemoteContact(' in m, 'host must execute remote contacts'
assert 'ResolveContacts(&request,1,' in m, 'remote must call the same complete PCVR resolver'
assert 'RemoteScope' in m, 'remote histories must not overwrite the host'
assert 'c.physicalContacts=true' in t, 'joining swings must not synthesize attack buttons'
assert 'melee::TickNetwork();' in v and 'melee::StopNetwork();' in v
assert v.count('melee::InstallRemoteHost();')==2, 'desktop startup and retry must install the contact resolver'
assert 'hero[1]==5' not in m, 'do not exclude Druid'
assert 'servertarget::ReadBody(' in m, 'host must fill body dimensions required by client vibration'
assert 'BasicWeapon(' not in m, 'do not restrict the original detector to basic weapons'
feel=m[m.index('void Feel('):m.index('// Bow bash',m.index('void Feel('))]
assert 'meleedispatch::NotifyContact(' in feel, 'host contact must return exact physical-sweep feedback'
assert 'meleedispatch::Predicted(value,target)' in m, 'prediction and receipts must share duplicate suppression'
assert 'meleedispatch::TakeFeedback(' in m, 'confirmed contacts must reach the joining controller'
assert 'current[lane]' in m and 'feedback.epoch' in m and 'feedback.equipment' in m, 'old equipment/tracking must reject stale feedback'
print('client swing wiring: original detector/resolver plus correlated, deduplicated contact feedback passed')
