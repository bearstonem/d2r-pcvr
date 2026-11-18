"""Compile the real transport ownership expression against the real router fixture.
No copy of the production ownership policy is kept in this test.
Run from the source root with the bundled Python. Generated files stay in build-frame.
"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
transport = (root / 'steamvr/transport.cpp').read_text()
expressions = re.findall(r'c\.actionOwned\s*=\s*([^;]+);', transport)
assert len(expressions) == 1, 'expected one native-client ownership boundary'
fixture = (root / 'steamvr/native_client_input_test.cpp').read_text()
body = r'''
    {Sim s;s.Swing();Check(s.out.skill==1,"motion preemption pending pulse fixture");
     struct Stroke {bool Drawing()const{return false;}} glyphStroke;
     struct Cast {bool Busy()const{return false;}} glyphCast;
     bool glyphPressRt=false;
     spellmotion::Gate gate;float grips[2]={1,1};
     bool motionWatch=gate.Step(grips,true).watch;
     auto ownership=[&](){return OWNERSHIP_EXPRESSION;};
     Check(motionWatch,"both-grip motion gate fixture");
     s.c.actionOwned=ownership();s.Step();
     Check(s.out.skill<0&&!s.out.attack,"pre-cast motion gate cancels pending native attack");
     s.Swing();Check(s.out.skill<0,"motion stroke does not become native melee");
     s.in.quickCast=true;s.in.quickSkill=3;s.Step();
     Check(s.out.skill==3,"explicit motion slot cast preserved");
     s.in.quickCast=false;s.in.quickSkill=-1;
     grips[0]=grips[1]=0;motionWatch=gate.Step(grips,true).watch;
     Check(motionWatch&&ownership(),"motion release frame remains owned");
     s.c.actionOwned=ownership();s.Step();
     motionWatch=gate.Step(grips,true).watch;s.c.actionOwned=ownership();
     Check(!motionWatch&&!s.c.actionOwned,"ownership ends after motion release");
     s.Step(12);Check(s.out.skill<0,"cancelled native pulse never resumes");
     s.Swing();Check(s.out.skill==1,"normal melee resumes after motion");
    }
'''.replace('OWNERSHIP_EXPRESSION', expressions[0])
marker = '    std::printf("native client input:'
assert fixture.count(marker) == 1
fixture = '#include "spell_motion.h"\n' + fixture.replace(marker, body + marker)
work = root / 'build-frame/motion-ownership-test'
work.mkdir(parents=True, exist_ok=True)
source = work / 'test.cpp'
source.write_text(fixture)
exe = work / 'test.exe'
compiler = root / 'deps/llvm-mingw-20260616-ucrt-x86_64/bin/x86_64-w64-mingw32-clang++.exe'
subprocess.run([str(compiler), '-std=c++20', '-UNDEBUG', '-static', '-I'+str(root/'steamvr'), str(source), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
