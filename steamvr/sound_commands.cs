using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Media;
using System.Threading;
using System.Text;
using System.IO.MemoryMappedFiles;
using System.Web.Script.Serialization;

public sealed class SoundConfig {
    public int Microphone {get;set;} public int Slot {get;set;} public int Sample {get;set;}
    public int Tolerance {get;set;} public string Storage {get;set;} public List<Binding> Bindings {get;set;}
}
public static class SoundFeatures {
    // Mel-band cepstra suppress loudness and fine pitch differences; DTW handles
    // nonuniform timing. Scores are distances, not probabilities.
    static readonly double[,] filters=Filters();
    static double[,] Filters(){var f=new double[24,257];var bins=new int[26];
        double lo=2595*Math.Log10(1+100.0/700),hi=2595*Math.Log10(1+7600.0/700);
        for(int i=0;i<26;i++)bins[i]=(int)(512*700*(Math.Pow(10,(lo+(hi-lo)*i/25)/2595)-1)/16000);
        for(int m=0;m<24;m++)for(int k=bins[m];k<=bins[m+2]&&k<257;k++)f[m,k]=k<bins[m+1]?(double)(k-bins[m])/Math.Max(1,bins[m+1]-bins[m]):(double)(bins[m+2]-k)/Math.Max(1,bins[m+2]-bins[m+1]);return f;}
    static void Fft(double[] re,double[] im){int n=re.Length;for(int i=1,j=0;i<n;i++){int bit=n>>1;for(;(j&bit)!=0;bit>>=1)j^=bit;j^=bit;if(i<j){double t=re[i];re[i]=re[j];re[j]=t;}}
        for(int len=2;len<=n;len<<=1){double angle=-2*Math.PI/len;for(int i=0;i<n;i+=len)for(int j=0;j<len/2;j++){double c=Math.Cos(angle*j),s=Math.Sin(angle*j),r=re[i+j+len/2]*c-im[i+j+len/2]*s,v=re[i+j+len/2]*s+im[i+j+len/2]*c;re[i+j+len/2]=re[i+j]-r;im[i+j+len/2]=im[i+j]-v;re[i+j]+=r;im[i+j]+=v;}}}
    public static double[][] Extract(byte[] pcm){
        if(pcm.Length<6400||pcm.Length>112000||pcm.Length%2!=0)throw new Exception("Use a sound between 0.2 and 3 seconds.");
        int n=pcm.Length/2;var samples=new double[n];double energy=0;int clipped=0;
        for(int i=0;i<n;i++){short s=(short)(pcm[2*i]|pcm[2*i+1]<<8);samples[i]=s/32768.0;energy+=samples[i]*samples[i];if(Math.Abs((int)s)>32000)clipped++;}
        if(Math.Sqrt(energy/n)<.004)throw new Exception("Too quiet. Move closer to the microphone and try again.");
        if(clipped>n*.03)throw new Exception("Microphone clipping. Lower its gain and record again.");
        var frames=new List<double[]>();
        for(int start=0;start+400<=n;start+=320){var re=new double[512];var im=new double[512];double rms=0;
            for(int j=0;j<400;j++){double s=samples[start+j];rms+=s*s;re[j]=(s-(start+j>0?.97*samples[start+j-1]:0))*(.5-.5*Math.Cos(2*Math.PI*j/399));}
            if(rms/400<.000004)continue;Fft(re,im);var mel=new double[24];
            for(int m=0;m<24;m++){double power=0;for(int k=0;k<257;k++)power+=filters[m,k]*(re[k]*re[k]+im[k]*im[k]);mel[m]=Math.Log(Math.Max(1e-12,power));}
            var feature=new double[12];double norm=0;for(int c=1;c<=12;c++){double value=0;for(int m=0;m<24;m++)value+=mel[m]*Math.Cos(Math.PI*c*(m+.5)/24);feature[c-1]=value;norm+=value*value;}
            norm=Math.Sqrt(norm);if(norm>1e-8){for(int c=0;c<12;c++)feature[c]/=norm;frames.Add(feature);}
        }
        if(frames.Count<8)throw new Exception("Sound too short or quiet. Hold it a little longer.");return frames.ToArray();
    }
    public static double Distance(double[][] a,double[][] b){
        if(a.Length==0||b.Length==0||(double)a.Length/b.Length>2.2||(double)b.Length/a.Length>2.2)return 2;
        var previous=Enumerable.Repeat(1e6,b.Length+1).ToArray();previous[0]=0;
        for(int i=1;i<=a.Length;i++){var current=Enumerable.Repeat(1e6,b.Length+1).ToArray();for(int j=1;j<=b.Length;j++){
            if(Math.Abs((double)i/a.Length-(double)j/b.Length)>.30)continue;
            double dot=0;for(int k=0;k<12;k++)dot+=a[i-1][k]*b[j-1][k];double cost=Math.Max(0,1-dot);
            current[j]=Math.Min(previous[j-1]+2*cost,Math.Min(previous[j]+cost+.025,current[j-1]+cost+.025));
        }previous=current;}return previous[b.Length]/(a.Length+b.Length);
    }
    public static int Match(double[][] sound,List<double[][]>[] profiles,int tolerance,out double best,out bool ambiguous){
        double threshold=new[]{.12,.19,.27}[Math.Max(0,Math.Min(2,tolerance))];int winner=-1;best=2;double second=2;
        for(int i=0;i<profiles.Length;i++){if(profiles[i].Count==0)continue;var distances=profiles[i].Select(p=>Distance(sound,p)).OrderBy(x=>x).ToArray();double d=(distances[0]+distances[Math.Min(1,distances.Length-1)])*.5;
            if(d<best){second=best;best=d;winner=i;}else second=Math.Min(second,d);
        }
        ambiguous=winner>=0&&(second-best<.035||best>second*.82);
        return best<=threshold&&!ambiguous?winner:-1;
    }
}
public sealed class SoundSegmenter {
    List<byte> active=new List<byte>();Queue<byte[]> lead=new Queue<byte[]>();double noise=.002;int quiet=0,loud=0;bool speaking=false,overlong=false;
    public int Level;public bool Started {get{return speaking;}}
    public void Reset(){active.Clear();lead.Clear();quiet=loud=0;speaking=overlong=false;}
    public byte[] Feed(byte[] pcm){double power=0;for(int i=0;i+1<pcm.Length;i+=2){short s=(short)(pcm[i]|pcm[i+1]<<8);power+=(double)s*s/(32768*32768.0);}double rms=Math.Sqrt(power/Math.Max(1,pcm.Length/2));Level=Math.Min(100,(int)(rms*500));
        bool voiced=rms>Math.Max(.007,noise*3);if(!speaking){if(!voiced)noise=.98*noise+.02*Math.Min(rms,.01);lead.Enqueue(pcm);while(lead.Count>3)lead.Dequeue();loud=voiced?loud+pcm.Length:0;if(loud<3200)return null;speaking=true;foreach(var b in lead)active.AddRange(b);lead.Clear();}
        else active.AddRange(pcm);
        quiet=voiced?0:quiet+pcm.Length;if(active.Count>112000){overlong=true;active.Clear();}if(quiet<12800)return null;
        byte[] result=overlong?new byte[0]:active.Take(Math.Max(0,active.Count-quiet+1600)).ToArray();Reset();return result;
    }
}
public static class SoundStore {
    public static string FileName(string directory,int slot,int sample){if(slot<0||slot>=6||sample<0||sample>=3)throw new Exception("Invalid sample slot");return Path.Combine(directory,"sound-"+(slot+1)+"-"+(sample+1)+".wav");}
    public static byte[] Wave(byte[] pcm){using(var stream=new MemoryStream())using(var w=new BinaryWriter(stream)){w.Write(Encoding.ASCII.GetBytes("RIFF"));w.Write(36+pcm.Length);w.Write(Encoding.ASCII.GetBytes("WAVEfmt "));w.Write(16);w.Write((short)1);w.Write((short)1);w.Write(16000);w.Write(32000);w.Write((short)2);w.Write((short)16);w.Write(Encoding.ASCII.GetBytes("data"));w.Write(pcm.Length);w.Write(pcm);return stream.ToArray();}}
    public static byte[] Read(string file){byte[] b=System.IO.File.ReadAllBytes(file);if(b.Length<44||b.Length>112044||Encoding.ASCII.GetString(b,0,4)!="RIFF"||Encoding.ASCII.GetString(b,8,8)!="WAVEfmt "||BitConverter.ToInt32(b,16)!=16||BitConverter.ToInt16(b,20)!=1||BitConverter.ToInt16(b,22)!=1||BitConverter.ToInt32(b,24)!=16000||BitConverter.ToInt16(b,34)!=16||Encoding.ASCII.GetString(b,36,4)!="data"||BitConverter.ToInt32(b,40)!=b.Length-44)throw new Exception("Invalid recorded WAV sample");return b.Skip(44).ToArray();}
    public static void Save(string file,byte[] pcm){Directory.CreateDirectory(Path.GetDirectoryName(file));string temp=file+".tmp";System.IO.File.WriteAllBytes(temp,Wave(pcm));if(System.IO.File.Exists(file))System.IO.File.Replace(temp,file,null);else System.IO.File.Move(temp,file);}
    public static List<double[][]>[] Load(string directory){var result=new List<double[][]>[6];for(int slot=0;slot<6;slot++){result[slot]=new List<double[][]>();for(int sample=0;sample<3;sample++){string path=FileName(directory,slot,sample);if(System.IO.File.Exists(path))result[slot].Add(SoundFeatures.Extract(Read(path)));}}return result;}
}
public static class RecordedSounds {
    public static int Run(int pid,Func<int,ISoundInput> capture=null,Action<string> playback=null){
        if(capture==null)capture=id=>new Microphone(id);
        if(playback==null)playback=file=>{using(var player=new SoundPlayer(file)){player.Load();player.PlaySync();}};
        using(var parent=System.Diagnostics.Process.GetProcessById(pid))
        using(var map=MemoryMappedFile.OpenExisting(@"Local\D2RVRVoiceIntegrated"+pid))
        using(var view=map.CreateViewAccessor(0,8192))
        using(var mutex=Mutex.OpenExisting(@"Local\D2RVRVoiceIntegrated"+pid+"Mutex"))
        using(var bridge=new GameBridge("-game-"+pid)){
            Action<Action> locked=delegate(Action action){bool held=false;try{try{held=mutex.WaitOne(0);}catch(AbandonedMutexException){held=true;}if(held)action();}finally{if(held)mutex.ReleaseMutex();}};
            Action<string> report=delegate(string text){byte[] bytes=Encoding.UTF8.GetBytes(text);locked(delegate{byte[] line=new byte[240];Buffer.BlockCopy(bytes,0,line,0,Math.Min(239,bytes.Length));view.WriteArray(16,line,0,line.Length);});};
            uint resultSerial=0,completed=0;Action finished=()=>{++completed;};
            Action<string,int,double> resultReport=delegate(string text,int slot,double distance){locked(delegate{view.Write(7696,slot);view.Write(7700,++resultSerial);view.Write(7704,(float)distance);byte[] line=new byte[240];byte[] bytes=Encoding.UTF8.GetBytes(text);Buffer.BlockCopy(bytes,0,line,0,Math.Min(239,bytes.Length));view.WriteArray(7708,line,0,line.Length);view.WriteArray(16,line,0,line.Length);});};
            uint generation=0,mode=0;int lastRecognized=-1;ISoundInput mic=null;SoundConfig config=null;List<double[][]>[] profiles=null;var segment=new SoundSegmenter();ulong started=0,lastAudio=0,lastSent=0,lastAck=0,session=0,meterAt=0,heardAt=0;bool wasAllowed=false;
            try{while(!parent.HasExited){
                uint incoming=0,nextMode=0;bool exit=false;string json=null;
                locked(delegate{exit=view.ReadUInt32(12)!=0;incoming=view.ReadUInt32(0);nextMode=view.ReadUInt32(4);uint n=view.ReadUInt32(8);if(incoming!=generation&&n>0&&n<7424){byte[] data=new byte[n];view.ReadArray(256,data,0,data.Length);json=Encoding.UTF8.GetString(data);}});
                if(exit)break;
                if(incoming!=0&&incoming!=generation&&json!=null){
                    generation=incoming;mode=nextMode;bridge.Tick(false);if(mic!=null)mic.Dispose();mic=null;segment.Reset();wasAllowed=false;
                    try{config=new JavaScriptSerializer().Deserialize<SoundConfig>(json);
                        if(config==null||config.Bindings==null||config.Bindings.Count!=6||String.IsNullOrEmpty(config.Storage))throw new Exception("Invalid sound configuration");
                        string file=SoundStore.FileName(config.Storage,config.Slot,config.Sample);
                        if(mode==0)report("Recorded sound recognition off");
                        else if(mode==4){report("Playing sample - casting paused");playback(file);mode=0;report("Preview finished.");finished();}
                        else if(mode==5){if(System.IO.File.Exists(file))System.IO.File.Delete(file);mode=0;report("Sample deleted. Record a replacement.");finished();}
                        else {
                            if(mode!=3)profiles=SoundStore.Load(config.Storage);
                            if(mode!=3&&!profiles.Any(p=>p.Count>=1))throw new Exception("Record at least one sample of a sound first.");
                            mic=capture(config.Microphone);started=lastAudio=Native.GetTickCount64();
                            report(mode==3?"Recording: make your sound, then pause (10 seconds to start)":mode==1?"Validating automatically - no casts":"Listening - close settings to cast");heardAt=started;
                        }
                    }catch(Exception e){if(mode>=3)finished();mode=0;resultReport(e.Message,-1,2);}
                }
                bridge.Tick(mode==2&&mic!=null);
                locked(delegate{view.Write(7680,0x534e4432u);view.Write(7684,mic!=null?mode:0u);view.Write(7688,mic!=null?(uint)segment.Level:0u);view.Write(7692,mic!=null&&segment.Started?1u:0u);view.Write(7948,completed);view.Write(7952,Native.GetTickCount64());});
                if(mic==null){Thread.Sleep(50);continue;}
                try{ulong now=Native.GetTickCount64();if(mode==3&&now-started>14000)throw new Exception("Recording timed out. Press Record and try again.");
                    byte[] bytes=mic.Read();if(bytes==null){if(now-lastAudio>3000)throw new Exception("Microphone stopped. Check Virtual Desktop forwarding.");Thread.Sleep(5);continue;}lastAudio=now;
                    bool allowed=mode!=2||bridge.Ready;
                    if(allowed!=wasAllowed||session!=bridge.Session){segment.Reset();wasAllowed=allowed;session=bridge.Session;}
                    if(!allowed&&now-meterAt>1000){report("Casting paused - validation still listens");meterAt=now;}
                    byte[] sound=segment.Feed(bytes);
                    if(now-meterAt>500&&now-heardAt>3500){report((mode==3?"Recording":mode==1?"Testing":!bridge.Ready?"Validating (casts blocked)":"Listening")+" - mic level "+segment.Level+"%");meterAt=now;}
                    if(sound!=null){heardAt=now;if(sound.Length==0){resultReport("Sound too long. Use a short command under 3 seconds.",-1,2);continue;}
                        double[][] feature;try{feature=SoundFeatures.Extract(sound);}catch(Exception e){resultReport(e.Message,-1,2);continue;}
                        if(mode==3){SoundStore.Save(SoundStore.FileName(config.Storage,config.Slot,config.Sample),sound);mic.Dispose();mic=null;mode=0;report("Saved sound "+(config.Slot+1)+", sample "+(config.Sample+1)+". Preview it or record another example.");finished();continue;}
                        double distance;bool ambiguous;int slot=SoundFeatures.Match(feature,profiles,config.Tolerance,out distance,out ambiguous);
                        string result=slot<0?(ambiguous?"Ambiguous sound - no cast. Use more distinct commands.":"No sound matched. Adjust sensitivity or add an example."):"Matched sound "+(slot+1)+" (distance "+distance.ToString("F2")+")";
                        if(slot>=0&&mode==2){int skill=config.Bindings[slot].Skill;if(!bridge.Ready)result+=" - casting blocked (settings / game busy)";else if(skill<0)result+=" - no skill assigned";else if(lastSent!=0&&now-lastSent<1500)result+=" - cooldown";else if(bridge.Send(skill,session,now)){lastSent=now;result+=" - sent";}else result+=" - game not ready";}
                        if(slot>=0)lastRecognized=slot;
                        resultReport(result,slot,distance);
                    }
                    if(bridge.Ack!=0&&bridge.Ack!=lastAck){lastAck=bridge.Ack;resultReport("Sound "+(lastRecognized+1)+": "+bridge.Status,lastRecognized,0);heardAt=now;}
                }catch(Exception e){resultReport(e.Message,-1,2);mic.Dispose();mic=null;if(mode>=3)finished();mode=0;bridge.Tick(false);}
            }}finally{if(mic!=null)mic.Dispose();}
        }return 0;
    }
}

public static class SoundTests {
    static byte[] Sound(int type,double speed=1,double gain=1,double pitch=1){int n=(int)(16000*1.0/speed);var bytes=new byte[n*2];for(int i=0;i<n;i++){double t=(double)i/16000,phase=(double)i/n;double f=type==0?(phase<.5?340:780):type==1?(phase<.4?1700:2400):3000;double envelope=Math.Min(1,Math.Min(phase*20,(1-phase)*20));double value=(Math.Sin(2*Math.PI*f*pitch*t)+.4*Math.Sin(2*Math.PI*f*2*pitch*t)+.2*Math.Sin(2*Math.PI*f*3*pitch*t))*.16*gain*envelope;short sample=(short)(value*32767);bytes[2*i]=(byte)sample;bytes[2*i+1]=(byte)(sample>>8);}return bytes;}
    static void Check(bool v,string message){if(!v)throw new Exception(message);}
    sealed class Input:ISoundInput {
        byte[] bytes;int offset;public Input(byte[] pcm){bytes=pcm;}
        public byte[] Read(){Thread.Sleep(5);var block=new byte[1600];int count=Math.Min(block.Length,bytes.Length-offset);if(count>0){Buffer.BlockCopy(bytes,offset,block,0,count);offset+=count;}return block;}
        public void Dispose(){}
    }
    static void Pipeline(){
        int pid=System.Diagnostics.Process.GetCurrentProcess().Id;
        string directory=Path.Combine(Path.GetTempPath(),"d2r-pipeline-"+Guid.NewGuid().ToString("N"));
        string name=@"Local\D2RVRVoiceIntegrated"+pid;Exception failure=null;Thread worker=null;int played=0,opened=0,selectedSlot=0;bool ready=true;
        using(var mutex=new Mutex(false,name+"Mutex"))
        using(var map=MemoryMappedFile.CreateOrOpen(name,8192))using(var view=map.CreateViewAccessor())
        using(var gameMutex=new Mutex(false,@"Local\D2RVRVoicePrototypeMutex1-game-"+pid))
        using(var gameMap=MemoryMappedFile.CreateOrOpen(@"Local\D2RVRVoicePrototype1-game-"+pid,14208))using(var game=gameMap.CreateViewAccessor()){
            Action<Action> control=a=>{mutex.WaitOne();try{a();}finally{mutex.ReleaseMutex();}};
            Action heartbeat=()=>{gameMutex.WaitOne();try{game.Write(0,0x564f4931u);game.Write(8,Native.GetTickCount64());game.Write(24,71UL);game.Write(33,(byte)(ready?1:0));}finally{gameMutex.ReleaseMutex();}};
            Func<ulong> sequence=()=>{gameMutex.WaitOne();try{return game.ReadUInt64(14040);}finally{gameMutex.ReleaseMutex();}};
            Func<string> status=()=>{string result="";control(()=>{byte[] data=new byte[240];view.ReadArray(16,data,0,240);int n=Array.IndexOf(data,(byte)0);result=Encoding.UTF8.GetString(data,0,n<0?240:n);});return result;};
            uint generation=0;
            Action<int,int> change=(mode,sample)=>{var config=new SoundConfig{Storage=directory,Slot=selectedSlot,Sample=sample,Tolerance=1,Microphone=-1,Bindings=Enumerable.Range(0,6).Select(i=>new Binding{Skill=149+i}).ToList()};byte[] json=Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(config));control(()=>{view.WriteArray(256,json,0,json.Length);view.Write(8,(uint)json.Length);view.Write(4,(uint)mode);view.Write(12,0u);view.Write(16,(byte)0);view.Write(0,++generation);});};
            Action<string> expect=text=>{ulong end=Native.GetTickCount64()+6000;while(Native.GetTickCount64()<end){heartbeat();if(failure!=null)throw new Exception("Worker failed",failure);if(status().Contains(text))return;Thread.Sleep(5);}throw new Exception("Pipeline expected "+text+"; got "+status());};
            try{
                control(()=>{view.WriteArray(0,new byte[8192],0,8192);});heartbeat();
                worker=new Thread(()=>{try{RecordedSounds.Run(pid,id=>{int n=Interlocked.Increment(ref opened);return new Input(Sound(0,n<=3?1:1.2,n<=3?1:.55,n<=3?1:1.04));},file=>{Check(SoundStore.Read(file).Length>6400,"Preview receives saved WAV");Interlocked.Increment(ref played);});}catch(Exception e){failure=e;}});worker.IsBackground=true;worker.Start();
                for(int sample=0;sample<3;sample++){change(3,sample);expect("Saved sound 1, sample "+(sample+1));Check(File.Exists(SoundStore.FileName(directory,0,sample)),"Recording saved");Check(sequence()==0,"Recording never casts");}
                change(4,0);expect("Preview finished");Check(played==1&&sequence()==0,"Preview never casts");
                // Completion is durable telemetry, not a one-shot write that can be lost.
                control(()=>view.Write(7948,0u));
                Thread.Sleep(200);
                control(()=>Check(view.ReadUInt32(7948)==4u,"Action completion is republished until observed"));
                change(1,0);expect("Matched sound 1");Check(sequence()==0,"Test never casts");
                control(()=>{Check(view.ReadUInt32(7680)==0x534e4432u&&view.ReadInt32(7696)==0&&view.ReadUInt32(7700)>0,"Live validation publishes persistent match telemetry");});
                change(2,0);expect(" - sent");Check(sequence()!=0,"Match publishes native command");
                gameMutex.WaitOne();try{Check(game.ReadInt32(14064)==149&&game.ReadUInt64(14056)==71,"Assigned skill and active session sent");}finally{gameMutex.ReleaseMutex();}
                ulong sent=sequence();ready=false;heartbeat();change(2,0);expect("casting blocked");for(int i=0;i<80;i++){heartbeat();Thread.Sleep(5);}Check(sequence()==sent,"Busy game never casts");
                change(0,0);expect("recognition off");Check(sequence()==sent,"Off never casts");
                // Exercise the last binding through the same live worker, not just its array index.
                for(int sample=0;sample<3;++sample)File.Delete(SoundStore.FileName(directory,0,sample));
                selectedSlot=5;change(3,0);expect("Saved sound 6, sample 1");
                change(1,0);expect("Matched sound 6");Check(sequence()==sent,"Sixth binding validates without casting");
                ready=true;heartbeat();Thread.Sleep(1600);change(2,0);expect(" - sent");
                gameMutex.WaitOne();try{Check(game.ReadInt32(14064)==154,"Sixth binding sends exactly its assigned skill");}finally{gameMutex.ReleaseMutex();}
                change(0,0);expect("recognition off");
            }finally{
                control(()=>{view.Write(12,1u);});if(worker!=null)Check(worker.Join(5000),"Worker shuts down");
                if(Directory.Exists(directory))Directory.Delete(directory,true);
            }
        }
    }
    public static int Run(){try{
        var profiles=new List<double[][]>[6];for(int i=0;i<6;i++)profiles[i]=new List<double[][]>();
        for(int i=0;i<2;i++)foreach(double pitch in new[]{.98,1.0,1.02})profiles[i].Add(SoundFeatures.Extract(Sound(i,1,1,pitch)));
        double score;bool ambiguous;
        for(int i=0;i<2;i++)foreach(double speed in new[]{.8,1.0,1.25})foreach(double gain in new[]{.4,1.0,1.6}){
            int match=SoundFeatures.Match(SoundFeatures.Extract(Sound(i,speed,gain,1.04)),profiles,1,out score,out ambiguous);
            Check(match==i,"Natural timing / gain / pitch variation: "+i+" -> "+match+" score "+score);
        }
        Check(SoundFeatures.Match(SoundFeatures.Extract(Sound(2)),profiles,0,out score,out ambiguous)<0,"Unrelated sound rejected");
        var single=new List<double[][]>[6];for(int i=0;i<6;i++)single[i]=new List<double[][]>();single[0].Add(profiles[0][1]);
        Check(SoundFeatures.Match(SoundFeatures.Extract(Sound(0,1.2,.6,1.04)),single,1,out score,out ambiguous)==0,"A single recording can recognize a varied repetition");
        profiles[2]=profiles[0].ToList();Check(SoundFeatures.Match(SoundFeatures.Extract(Sound(0)),profiles,2,out score,out ambiguous)<0&&ambiguous,"Duplicate sounds reject as ambiguous even at Loose tolerance");
        var segment=new SoundSegmenter();byte[] clip=Sound(0);byte[] recognized=null;
        for(int i=0;i<clip.Length;i+=1600){var block=clip.Skip(i).Take(1600).ToArray();var result=segment.Feed(block);if(result!=null)recognized=result;}
        for(int i=0;i<10;i++){var result=segment.Feed(new byte[1600]);if(result!=null)recognized=result;}
        Check(recognized!=null&&recognized.Length>6400,"Voice activity captures utterance after pause");
        segment.Reset();for(int i=0;i<100;i++){Check(segment.Feed(new byte[1600])==null,"Silence never triggers");}
        segment.Reset();byte[] longResult=null;for(int i=0;i<100;i++){var result=segment.Feed(clip.Skip(8000).Take(1600).ToArray());if(result!=null)longResult=result;}for(int i=0;i<10;i++){var result=segment.Feed(new byte[1600]);if(result!=null)longResult=result;}Check(longResult!=null&&longResult.Length==0,"Overlong audio discarded, not matched as a fragment");
        string directory=Path.Combine(Path.GetTempPath(),"d2r-sound-test-"+Guid.NewGuid().ToString("N"));try{string file=SoundStore.FileName(directory,0,0);SoundStore.Save(file,clip);Check(SoundStore.Read(file).SequenceEqual(clip),"Playback WAV preserves recording");SoundStore.Save(file,Sound(1));Check(SoundStore.Read(file).SequenceEqual(Sound(1)),"Re-record replaces previous sample atomically");}finally{if(Directory.Exists(directory))Directory.Delete(directory,true);}
        Pipeline();
        Console.WriteLine("Recording-to-cast pipeline, test/playback suppression and busy-game rejection passed");
        Console.WriteLine("Recorded sound timing/gain/pitch tolerance, ambiguity, VAD, overlong rejection and WAV round-trip passed");return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
